
/*
 * VoiceTune-TS3 - Voicechanger-Plugin für TeamSpeak 3
 * Copyright (C) 2026  David K., Tony S.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
 
#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "ring_buffer.h"

// Liest aus dem FrameRing im Takt des Mikrofons (beliebige Blockgroesse) und gleicht dabei
// Jitter und Taktabweichung zwischen Capture- und Mikrofon-Uhr aus:
//  - Prebuffer: Wiedergabe startet erst bei `target` Frames Vorrat,
//  - Drift: Lesegeschwindigkeit wird per linearer Interpolation um max. +-0,5 % nachgefuehrt (kein Verwerfen, kein Sprung),
//  - Unterlauf: Rest ausspielen, in <= 5 ms ausblenden, neu prebuffern; Einblenden beim Wiedereinstieg,
//  - Notfall-Ueberlauf (> 300 ms): auf `target` springen mit Einblendung.
// Nur ein Thread (Mikrofon-Callback) darf Read()/Reset() aufrufen; die Zaehler sind atomar lesbar.
class JitterReader {
public:
    static constexpr int    kFadeFrames   = 240;   // 5 ms
    static constexpr size_t kMaxFill      = 14400; // 300 ms
    static constexpr int    kIdleFrames   = 9600;  // 200 ms ohne neue Capture-Daten = Quelle ist still (kein Unterlauf)
    static constexpr double kMaxRatioDev  = 0.005;

    std::atomic<uint32_t> underruns{0};
    std::atomic<uint32_t> overruns{0};
    std::atomic<size_t>   minFill{SIZE_MAX}; // seit letztem Zuruecksetzen durch den Leser der Statistik
    std::atomic<size_t>   maxFill{0};
    std::atomic<float>    ratioNow{1.0f};

    static constexpr size_t kMinTarget    = 1920;  // 40 ms: darunter wird der Puffer bei 20-ms-Bloecken instabil
    static constexpr int    kRetargetHold = 7200;  // neues Ziel erst uebernehmen, wenn es 150 ms unveraendert blieb (Regler ziehen)

    // minTarget: kleinster erlaubter Vorrat. Nur senken, wenn gelegentliche Aussetzer weniger stoeren als Verzoegerung (Abhoeren der eigenen Stimme).
    explicit JitterReader(FrameRing& ring, size_t targetFrames = 2880, size_t minTarget = kMinTarget) : ring_(ring), minTarget_(minTarget), target_(std::max(targetFrames, minTarget)), requested_(target_) { Reset(); }

    // Neue Ziel-Verzoegerung (aus beliebigem Thread). Wird im naechsten Read() nach kurzer Beruhigung uebernommen.
    void SetTarget(size_t frames) { requested_.store(std::max(frames, minTarget_), std::memory_order_relaxed); }

    void Reset()
    {
        buf_.clear();
        pos_         = 0;
        prebuffering_ = true;
        ratio_       = 1.0;
        drift_       = 0;
        errSmooth_   = 0;
        target_      = requested_.load(std::memory_order_relaxed);
        pendingReq_  = target_;
        stableFrames_ = 0;
        fadeIn_      = 0;
        idleFrames_  = 0;
        lastWritten_ = ring_.TotalWritten();
        last_[0] = last_[1] = 0;
    }

    size_t Target() const { return requested_.load(std::memory_order_relaxed); }

    // Fuer Quellen, die schneller liefern koennen als abgespielt wird (Dateien): hoechstens `frames` ueber dem Ziel-Vorrat aus dem Ring holen.
    void LimitPullAhead(size_t frames) { maxPullAhead_ = frames; }
    // Aktueller Vorrat im Reader in Frames (fuer Positionsanzeigen), aus beliebigem Thread lesbar.
    std::atomic<size_t> buffered{0};

    // Schreibt genau `frames` Stereo-Frames nach `out`. true = echtes Audio, false = Stille (Prebuffer/Quelle still).
    bool Read(int16_t* out, int frames)
    {
        Pull();
        const uint64_t w = ring_.TotalWritten();
        if (w != lastWritten_) {
            lastWritten_ = w;
            idleFrames_  = 0;
        } else {
            idleFrames_ = std::min<int64_t>(idleFrames_ + frames, 1 << 30);
        }
        const bool idle = idleFrames_ > kIdleFrames;

        // Ziel-Verzoegerung geaendert? Erst uebernehmen, wenn sie kurz stabil ist.
        bool       stall = false; // Ziel wurde deutlich vergroessert: ausblenden und neu prebuffern
        const size_t req = requested_.load(std::memory_order_relaxed);
        if (req != pendingReq_) {
            pendingReq_ = req;
            stableFrames_ = 0;
        } else if (req != target_ && (stableFrames_ += frames) >= kRetargetHold) {
            const size_t oldTarget = target_;
            target_                = req;
            errSmooth_             = 0;
            stableFrames_          = 0;
            if (!prebuffering_) {
                if (target_ > oldTarget + kMinTarget / 4) stall = true;
                else if (Fill() > target_) { // kleiner geworden: Ueberschuss sofort ueberspringen
                    pos_ += static_cast<double>(Fill() - target_);
                    fadeIn_ = kFadeFrames;
                }
            }
        }

        size_t fill = Fill();
        if (prebuffering_) {
            if (fill >= target_) {
                prebuffering_ = false;
                fadeIn_       = kFadeFrames;
                ratio_        = 1.0;
            } else {
                std::memset(out, 0, static_cast<size_t>(frames) * 2 * sizeof(int16_t));
                return false;
            }
        }

        UpdateFillStats(fill);
        // Notfall: alten Vorrat verwerfen (Reserve 300 ms ueber dem Ziel). Nicht bei gedrosselten Quellen (Dateien): dort gibt es keinen Rueckstau,
        // der Reader holt nur ab, was er braucht; ein Sprung wuerde Audio ueberspringen und den Dekoder durch die Datei rasen lassen.
        if (maxPullAhead_ == SIZE_MAX && fill > target_ + kMaxFill - 2880) {
            pos_ += static_cast<double>(fill - target_);
            fill = target_;
            overruns.fetch_add(1);
            fadeIn_ = kFadeFrames;
        }

        // PI-Regler auf dem geglaetteten Fuellstand: der Integralanteil (drift_) lernt die Taktabweichung der beiden Uhren,
        // sodass der Vorrat wieder auf `target` zurueckkehrt statt dauerhaft zu tief/hoch zu stehen.
        if (maxPullAhead_ == SIZE_MAX) {
            errSmooth_ += (static_cast<double>(fill) - static_cast<double>(target_) - errSmooth_) * 0.05;
            const double x = errSmooth_ / static_cast<double>(target_);
            drift_         = std::clamp(drift_ + 0.0003 * x, -kMaxRatioDev, kMaxRatioDev);
            const double desired = 1.0 + std::clamp(drift_ + 0.05 * x, -kMaxRatioDev, kMaxRatioDev);
            ratio_ += (desired - ratio_) * 0.2;
        } else { // Datei: liefert nur auf Abruf, es gibt keine Taktabweichung -> exakt 1:1 abspielen (kein Tonhoehen-Fehler)
            ratio_ = 1.0;
            drift_ = errSmooth_ = 0;
        }
        ratioNow.store(static_cast<float>(ratio_));

        const size_t avail = buf_.size() / 2;
        int          produced = 0;
        for (; produced < frames; ++produced) {
            const size_t i = static_cast<size_t>(pos_);
            if (i + 1 >= avail) break;
            const float f = static_cast<float>(pos_ - static_cast<double>(i));
            for (int c = 0; c < 2; ++c) {
                const float a = buf_[2 * i + c], b = buf_[2 * (i + 1) + c];
                out[2 * produced + c] = static_cast<int16_t>(a + (b - a) * f);
            }
            pos_ += ratio_;
        }

        // Einblenden nach Start/Notfall-Sprung
        for (int k = 0; k < produced && fadeIn_ > 0; ++k, --fadeIn_) {
            const float g = 1.0f - static_cast<float>(fadeIn_) / kFadeFrames;
            out[2 * k]     = static_cast<int16_t>(out[2 * k] * g);
            out[2 * k + 1] = static_cast<int16_t>(out[2 * k + 1] * g);
        }
        if (stall && produced == frames) { // Block ausblenden, danach auf das groessere Ziel prebuffern
            const int n = std::min(frames, kFadeFrames);
            for (int k = 0; k < n; ++k) {
                const float g = 1.0f - static_cast<float>(k + 1) / n;
                out[2 * (frames - n + k)]     = static_cast<int16_t>(out[2 * (frames - n + k)] * g);
                out[2 * (frames - n + k) + 1] = static_cast<int16_t>(out[2 * (frames - n + k) + 1] * g);
            }
            prebuffering_ = true;
        }
        if (produced > 0) {
            last_[0] = out[2 * (produced - 1)];
            last_[1] = out[2 * (produced - 1) + 1];
        }

        if (produced < frames) { // Unterlauf: letzten Wert in <= 5 ms ausblenden
            for (int k = 0; produced + k < frames; ++k) {
                const float g = std::max(0.0f, 1.0f - static_cast<float>(k) / kFadeFrames);
                out[2 * (produced + k)]     = static_cast<int16_t>(last_[0] * g);
                out[2 * (produced + k) + 1] = static_cast<int16_t>(last_[1] * g);
            }
            prebuffering_ = true;
            if (!idle) underruns.fetch_add(1);
        }

        Compact();
        buffered.store(Fill(), std::memory_order_relaxed);
        return true;
    }

private:
    size_t Fill() const
    {
        const size_t avail = buf_.size() / 2, i = static_cast<size_t>(pos_);
        return avail > i ? avail - i : 0;
    }

    void Pull()
    {
        size_t n = ring_.Size();
        if (maxPullAhead_ != SIZE_MAX) { // Dateiquelle: nur so viel abholen, wie fuer Ziel-Vorrat + Reserve noetig ist; der Rest bleibt im Ring und bremst den Dekoder
            const size_t have = Fill(), want = target_ + maxPullAhead_;
            n                 = std::min(n, want > have ? want - have : size_t{0});
        }
        if (!n) return;
        const size_t old = buf_.size();
        buf_.resize(old + n * 2);
        const size_t got = ring_.ReadUpTo(buf_.data() + old, n);
        buf_.resize(old + got * 2);
    }

    void Compact()
    {
        const size_t consumed = static_cast<size_t>(pos_);
        if (consumed >= 2048) {
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(consumed * 2));
            pos_ -= static_cast<double>(consumed);
        }
    }

    void UpdateFillStats(size_t fill)
    {
        if (fill < minFill.load(std::memory_order_relaxed)) minFill.store(fill, std::memory_order_relaxed);
        if (fill > maxFill.load(std::memory_order_relaxed)) maxFill.store(fill, std::memory_order_relaxed);
    }

    FrameRing&           ring_;
    size_t               minTarget_;
    size_t               target_;
    std::atomic<size_t>  requested_;
    size_t               maxPullAhead_ = SIZE_MAX;
    size_t               pendingReq_ = 0;
    int64_t              stableFrames_ = 0;
    std::vector<int16_t> buf_; // interleaved Stereo, bereits aus dem Ring geholt
    double               pos_ = 0;
    double               ratio_ = 1.0;
    double               drift_ = 0;
    double               errSmooth_ = 0;
    bool                 prebuffering_ = true;
    int                  fadeIn_ = 0;
    int64_t              idleFrames_ = 0;
    uint64_t             lastWritten_ = 0;
    int16_t              last_[2] = {0, 0};
};
