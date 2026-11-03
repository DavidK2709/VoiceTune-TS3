
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
 
#include "opus_preview.h"

#include <algorithm>

#ifdef VOICETUNE_HAS_OPUS
#include <opus/opus.h>
#endif

bool OpusPreview::Available()
{
#ifdef VOICETUNE_HAS_OPUS
    return true;
#else
    return false;
#endif
}

#ifdef VOICETUNE_HAS_OPUS

std::shared_ptr<OpusPreview> OpusPreview::Create(int channels, int bitrateBps)
{
    if ((channels != 1 && channels != 2) || bitrateBps < 6000 || bitrateBps > 512000) return nullptr;
    int  err = 0;
    auto p   = std::shared_ptr<OpusPreview>(new OpusPreview());
    p->enc_  = opus_encoder_create(48000, channels, channels == 1 ? OPUS_APPLICATION_VOIP : OPUS_APPLICATION_AUDIO, &err);
    if (err != OPUS_OK || !p->enc_) return nullptr;
    p->dec_ = opus_decoder_create(48000, channels, &err);
    if (err != OPUS_OK || !p->dec_) return nullptr;
    opus_encoder_ctl(p->enc_, OPUS_SET_BITRATE(bitrateBps));
    opus_encoder_ctl(p->enc_, OPUS_SET_VBR(1));
    opus_encoder_ctl(p->enc_, OPUS_SET_COMPLEXITY(9));
    opus_encoder_ctl(p->enc_, OPUS_SET_SIGNAL(channels == 1 ? OPUS_AUTO : OPUS_SIGNAL_MUSIC));
    p->channels_ = channels;
    p->bitrate_  = bitrateBps;
    p->out_.assign(static_cast<size_t>(kFrame) * 2, 0); // 20 ms Vorlauf, damit Ein- und Ausgabe gleich viele Frames haben
    return p;
}

OpusPreview::~OpusPreview()
{
    if (enc_) opus_encoder_destroy(enc_);
    if (dec_) opus_decoder_destroy(dec_);
}

void OpusPreview::Process(int16_t* stereo, int frames)
{
    if (frames <= 0 || !enc_ || !dec_) return;
    in_.insert(in_.end(), stereo, stereo + static_cast<size_t>(frames) * 2);

    int16_t       pcm[kFrame * 2], dec[kFrame * 2];
    unsigned char packet[1500];
    size_t        used = 0;
    while (in_.size() - used >= static_cast<size_t>(kFrame) * 2) {
        const int16_t* src = in_.data() + used;
        if (channels_ == 1) // Voice: Mono-Summe
            for (int i = 0; i < kFrame; ++i) pcm[i] = static_cast<int16_t>((src[2 * i] + src[2 * i + 1]) / 2);
        else
            std::copy(src, src + kFrame * 2, pcm);
        used += static_cast<size_t>(kFrame) * 2;

        const int n = opus_encode(enc_, pcm, kFrame, packet, static_cast<opus_int32>(sizeof(packet)));
        if (n < 0) { // Fehler: Frame unveraendert durchreichen
            if (channels_ == 1) for (int i = 0; i < kFrame; ++i) dec[2 * i] = dec[2 * i + 1] = pcm[i];
            else std::copy(pcm, pcm + kFrame * 2, dec);
        } else if (opus_decode(dec_, packet, n, dec, kFrame, 0) != kFrame) {
            std::fill(dec, dec + kFrame * 2, int16_t{0});
        } else if (channels_ == 1) { // mono -> Stereo (links = rechts), von hinten, damit nichts ueberschrieben wird
            for (int i = kFrame - 1; i >= 0; --i) {
                dec[2 * i + 1] = dec[i];
                dec[2 * i]     = dec[i];
            }
        }
        out_.insert(out_.end(), dec, dec + kFrame * 2);
    }
    in_.erase(in_.begin(), in_.begin() + static_cast<std::ptrdiff_t>(used));

    const size_t need = static_cast<size_t>(frames) * 2;
    const size_t have = std::min(need, out_.size());
    std::copy(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(have), stereo);
    std::fill(stereo + have, stereo + need, int16_t{0});
    out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(have));
}

#else // ohne libopus

std::shared_ptr<OpusPreview> OpusPreview::Create(int, int) { return nullptr; }
OpusPreview::~OpusPreview() {}
void OpusPreview::Process(int16_t*, int) {}

#endif
