
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
// Lokale Opus-Vorschau: schickt Audio durch einen Opus-Encoder und -Decoder, damit "Mithoeren"/"Anhoeren" annaehernd so klingt wie die Uebertragung
// ueber TeamSpeak (Opus Voice = Mono, Opus Music = Stereo). Annaehernd, weil TeamSpeaks genaue Encoder-Einstellungen (Komplexitaet, Bandbreite, Paketverlust)
// nicht bekannt sind; die Bitrate wird nach Moeglichkeit aus TeamSpeak gelesen.
// Ohne libopus (Makro VOICETUNE_HAS_OPUS nicht gesetzt) liefert Create() nullptr.
#include <cstdint>
#include <memory>
#include <vector>

struct OpusEncoder;
struct OpusDecoder;

class OpusPreview {
public:
    static constexpr int kFrame = 960; // 20 ms bei 48 kHz

    static bool Available();
    // channels: 1 = Voice (mono, VOIP), 2 = Music (stereo, AUDIO); bitrateBps z.B. 32000. nullptr bei ungueltigen Werten oder ohne libopus.
    static std::shared_ptr<OpusPreview> Create(int channels, int bitrateBps);

    ~OpusPreview();
    OpusPreview(const OpusPreview&)            = delete;
    OpusPreview& operator=(const OpusPreview&) = delete;

    // Verarbeitet beliebig grosse Bloecke (interleaved Stereo, in place). Intern wird in 20-ms-Frames kodiert; dadurch kommt das Ergebnis 20 ms spaeter.
    void Process(int16_t* stereo, int frames);

    int Channels() const { return channels_; }
    int Bitrate() const { return bitrate_; }

private:
    OpusPreview() = default;
    OpusEncoder*         enc_ = nullptr;
    OpusDecoder*         dec_ = nullptr;
    int                  channels_ = 1, bitrate_ = 0;
    std::vector<int16_t> in_, out_; // FIFOs (interleaved Stereo)
};
