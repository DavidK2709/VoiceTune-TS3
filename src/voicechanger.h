
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
// Voicechanger: Klangverarbeitung fuer das Mikrofon-Signal (Oberflaeche: voicechanger_page.{h,cpp}, eingehaengt in Engine::ProcessVoice).
//
// Verfahren: Kurzzeit-Spektrum (STFT, Hop = Fenster / 4) mit Phasenvocoder.
//   - Tonhoehe: spektrale Spitzen samt Umgebung werden verschoben, ihre Phasen fortgeschrieben (Laroche/Dolson), damit die Stimme nicht "verwaschen" klingt.
//   - Formant: die Spektralhuelle (Cepstrum) wird getrennt von der Anregung behandelt. So bleiben Tonhoehe und Stimmcharakter unabhaengig voneinander einstellbar.
//   - Spiegeln: das Spektrum wird um knapp 4 kHz gespiegelt (wie ein Ringmodulator mit Tiefpass, aber ohne Filter-Verzoegerung).
//   - Latenz: genau eine Fensterlaenge, unabhaengig von der Blockgroesse. Kurzes Fenster 21,3 ms, langes 42,7 ms. Das kurze trennt die Obertoene tiefer Stimmen (unter ca. 150 Hz)
//     nicht mehr sauber, die Tonhoehe wird dort ungenau und rauer; das lange Fenster behebt das. Darum waehlbar. Die Originalstimme fuer Dry/Wet wird um dieselbe Zeit verzoegert,
//     sonst gaebe es Kammfilter-Effekte.
//
// Regeln: Process laeuft im Audio-Thread und arbeitet ohne Allokation, Locks oder Datei-/Netz-Zugriff. Eine Instanz gehoert genau einem Audio-Strom.
#include <cstdint>

#include "voice_presets.h"

class VoiceChanger {
public:
    static constexpr int kFftShort = 1024, kFftLong = 2048;

    int LatencyFrames() const { return fft_; }
    // Spiegelachse in Hz (f -> Achse - f)
    static double MirrorHz(bool longWindow) { return (longWindow ? 169 : 85) * 48000.0 / (longWindow ? kFftLong : kFftShort); }

    VoiceChanger();

    // Zustand verwerfen (neuer Audio-Strom). active = sofort voll im Effekt, ohne Ueberblendung vom Originalsignal.
    void Reset(bool active);

    // Verarbeitet interleaved 16 bit, 48 kHz, in place. Mehrkanal-Eingang wird als Mono-Summe verarbeitet und auf alle Kanaele geschrieben.
    // enabled = false blendet in 10 ms zum unveraenderten Signal zurueck. longWindow = langes Fenster (Umschalten verwirft den Puffer). Rueckgabe: true = Block wurde veraendert.
    bool Process(int16_t* io, int frames, int channels, const VoiceParams& params, bool enabled, bool longWindow = false);

    // Noch im Effekt oder in der Ueberblendung
    bool Busy() const { return mix_ > 0.0f; }

private:
    void Configure(int fftSize); // Fenster und Tabellen fuer diese Groesse; ohne Allokation, darf im Audio-Thread laufen
    void ProcessFrame(const VoiceParams& p);
    void ComputeEnvelope();
    void Fft(float* re, float* im) const;

    static constexpr int kMaxFft  = kFftLong;
    static constexpr int kMaxBins = kMaxFft / 2 + 1;

    int fft_ = 0, hop_ = 0, bins_ = 0, mask_ = 0, mirrorBin_ = 0;

    // Vorberechnet
    float window_[kMaxFft];
    float twRe_[kMaxFft / 2], twIm_[kMaxFft / 2]; // fuer kMaxFft; kuerzere Transformationen nehmen jeden n-ten Wert
    int   bitrev_[kMaxFft];
    float liftWindow_[kMaxBins];

    // Audio-Zustand
    float in_[kMaxFft];  // Eingang als Ringpuffer; das aelteste Sample ist zugleich die verzoegerte Originalstimme
    float ola_[kMaxFft]; // Ausgang (Overlap-Add), selber Index wie in_
    int   pos_ = 0, hopCount_ = 0;
    unsigned frameIndex_ = 0;

    // Phasenvocoder
    float prevPhase_[kMaxBins], synPhase_[kMaxBins];
    bool  phaseValid_ = false; // false: kein brauchbares Vorgaenger-Spektrum (Start, Stille, Einsatz nach einer Pause)
    float prevEnergy_ = 0;

    // Arbeitsspeicher eines Frames
    float re_[kMaxFft], im_[kMaxFft];
    float mag_[kMaxBins], phase_[kMaxBins], env_[kMaxBins];
    float yRe_[kMaxBins], yIm_[kMaxBins];
    float cepRe_[kMaxFft], cepIm_[kMaxFft], logMag_[kMaxBins];
    int   peaks_[kMaxBins];

    // Geglaettete Regler (damit Ziehen nicht knackt) und Ueberblendung Original <-> Effekt
    float wet_ = 1, gain_ = 1, polarity_ = 1, mix_ = 0;
    bool  smoothReady_ = false;
};
