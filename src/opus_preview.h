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
