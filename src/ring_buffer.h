
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
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

// Ring-Buffer fuer interleaved Stereo-Frames (int16, 48 kHz). Ein Writer (Capture-Thread),
// ein Reader (TS3-Audio-Thread); die kritischen Abschnitte sind winzig. Ueberlauf verwirft die aeltesten Frames.
class FrameRing {
public:
    static constexpr size_t kChannels = 2;

    explicit FrameRing(size_t capacityFrames) : buf_(capacityFrames * kChannels), cap_(capacityFrames) {}

    // src == nullptr schreibt Stille.
    void Write(const int16_t* src, size_t frames)
    {
        std::lock_guard<std::mutex> lk(m_);
        if (frames > cap_) {
            if (src) src += (frames - cap_) * kChannels;
            frames = cap_;
        }
        if (size_ + frames > cap_) DropLocked(size_ + frames - cap_);
        size_t tail  = (head_ + size_) % cap_;
        size_t first = std::min(frames, cap_ - tail);
        CopyIn(tail, src, first);
        CopyIn(0, src ? src + first * kChannels : nullptr, frames - first);
        size_ += frames;
        written_ += frames;
    }

    // Liest bis zu `maxFrames` Frames. Rueckgabe: Anzahl gelesener Frames.
    size_t ReadUpTo(int16_t* dst, size_t maxFrames)
    {
        std::lock_guard<std::mutex> lk(m_);
        size_t n = std::min(maxFrames, size_);
        if (!n) return 0;
        size_t first = std::min(n, cap_ - head_);
        std::memcpy(dst, &buf_[head_ * kChannels], first * kChannels * sizeof(int16_t));
        std::memcpy(dst + first * kChannels, &buf_[0], (n - first) * kChannels * sizeof(int16_t));
        head_ = (head_ + n) % cap_;
        size_ -= n;
        return n;
    }

    // Gesamtzahl je geschriebener Frames (zeigt, ob die Quelle noch Daten liefert).
    uint64_t TotalWritten() const
    {
        std::lock_guard<std::mutex> lk(m_);
        return written_;
    }

    // Liest genau `frames` Frames oder gar nichts. Rueckgabe: Anzahl gelesener Frames.
    size_t ReadExact(int16_t* dst, size_t frames)
    {
        std::lock_guard<std::mutex> lk(m_);
        if (size_ < frames) return 0;
        size_t first = std::min(frames, cap_ - head_);
        std::memcpy(dst, &buf_[head_ * kChannels], first * kChannels * sizeof(int16_t));
        std::memcpy(dst + first * kChannels, &buf_[0], (frames - first) * kChannels * sizeof(int16_t));
        head_ = (head_ + frames) % cap_;
        size_ -= frames;
        return frames;
    }

    // Verwirft die aeltesten Frames, sodass hoechstens `maxFrames` uebrig bleiben (begrenzt die Latenz).
    void TrimTo(size_t maxFrames)
    {
        std::lock_guard<std::mutex> lk(m_);
        if (size_ > maxFrames) DropLocked(size_ - maxFrames);
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lk(m_);
        head_ = size_ = 0;
    }

    size_t Size() const
    {
        std::lock_guard<std::mutex> lk(m_);
        return size_;
    }

private:
    void DropLocked(size_t n)
    {
        n     = std::min(n, size_);
        head_ = (head_ + n) % cap_;
        size_ -= n;
    }
    void CopyIn(size_t posFrames, const int16_t* src, size_t frames)
    {
        if (!frames) return;
        int16_t* d = &buf_[posFrames * kChannels];
        if (src) std::memcpy(d, src, frames * kChannels * sizeof(int16_t));
        else std::memset(d, 0, frames * kChannels * sizeof(int16_t));
    }

    mutable std::mutex   m_;
    std::vector<int16_t> buf_;
    size_t               cap_;
    size_t               head_ = 0;
    size_t               size_ = 0;
    uint64_t             written_ = 0;
};
