
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
// Lokale Ausgabe fuer "Selbst hoeren": spielt Soundboard-Clips und den Datei-Player auf dem Windows-Standard-Ausgabegeraet ab (WASAPI, geteilter Modus),
// unabhaengig von TeamSpeaks eigener Wiedergabe (die der Wiedergabe-Hook von TS3 nur unter bestimmten Bedingungen bedient).
// Write() darf aus dem Audio-Thread kommen (nur ein kurzer Mutex im Ring). Ein eigener Thread oeffnet das Geraet, sobald Daten da sind, und gibt es nach
// 3 s Stille wieder frei. Wechselt das Standardgeraet (Kopfhoerer ein-/ausgesteckt), wird beim naechsten Ton das neue geoeffnet.
#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "jitter_reader.h"
#include "ring_buffer.h"

class LocalOut {
public:
    LocalOut() : ring_(48000), reader_(ring_, 1920) {}
    ~LocalOut() { Stop(); }
    LocalOut(const LocalOut&)            = delete;
    LocalOut& operator=(const LocalOut&) = delete;

    void SetLogger(void (*fn)(const char*)) { logger_ = fn; }

    // Thread starten bzw. (in ts3plugin_shutdown, vor dem Entladen der DLL) beenden.
    void Start()
    {
        if (thread_.joinable()) return;
        quit_   = false;
        thread_ = std::thread(&LocalOut::Main, this);
    }
    void Stop()
    {
        quit_ = true;
        if (thread_.joinable()) thread_.join();
    }

    // Stereo-Frames (interleaved int16, 48 kHz) zur Wiedergabe vormerken.
    void Write(const int16_t* stereo, size_t frames) { ring_.Write(stereo, frames); }
    void Clear() { ring_.Clear(); }

private:
    static constexpr UINT32 kQueueFrames = 1920;   // so viel (40 ms) liegt hoechstens im Geraet
    static constexpr int64_t kIdleFrames  = 144000; // 3 s Stille: Geraet freigeben

    struct Dev {
        IAudioClient*       client = nullptr;
        IAudioRenderClient* render = nullptr;
        void Close()
        {
            if (client) client->Stop();
            if (render) render->Release();
            if (client) client->Release();
            render = nullptr;
            client = nullptr;
        }
    };

    void Log(const char* text, long hr = 0)
    {
        if (!logger_) return;
        char line[160];
        if (hr) std::snprintf(line, sizeof(line), "Selbst hoeren: %s (HRESULT 0x%08lX)", text, static_cast<unsigned long>(hr));
        else std::snprintf(line, sizeof(line), "Selbst hoeren: %s", text);
        logger_(line);
    }

    bool Open(Dev& d)
    {
        IMMDeviceEnumerator* en  = nullptr;
        IMMDevice*           dev = nullptr;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en));
        if (SUCCEEDED(hr)) hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
        if (SUCCEEDED(hr)) hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&d.client));
        if (dev) dev->Release();
        if (en) en->Release();
        if (SUCCEEDED(hr)) {
            WAVEFORMATEX fmt{};
            fmt.wFormatTag      = WAVE_FORMAT_PCM;
            fmt.nChannels       = 2;
            fmt.nSamplesPerSec  = 48000;
            fmt.wBitsPerSample  = 16;
            fmt.nBlockAlign     = 4;
            fmt.nAvgBytesPerSec = 48000 * 4;
            // Das Geraet darf ein anderes Format haben: Windows wandelt (Abtastrate und Kanaele) selbst.
            hr = d.client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, 2000000, 0, &fmt, nullptr);
        }
        if (SUCCEEDED(hr)) hr = d.client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(&d.render));
        if (SUCCEEDED(hr)) hr = d.client->Start();
        if (FAILED(hr)) {
            Log("Ausgabegeraet konnte nicht geoeffnet werden", hr);
            d.Close();
            return false;
        }
        return true;
    }

    void Main()
    {
        const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        Dev           dev;
        bool          open     = false;
        int64_t       silent   = 0;
        ULONGLONG     lastFail = 0;
        while (!quit_) {
            if (!open) {
                Sleep(20);
                if (ring_.Size() == 0) continue;
                const ULONGLONG now = GetTickCount64();
                if (lastFail != 0 && now - lastFail < 3000) { // nach einem Fehlschlag nicht in jedem Block neu versuchen
                    ring_.Clear();
                    continue;
                }
                if (Open(dev)) {
                    open   = true;
                    silent = 0;
                    reader_.Reset();
                } else {
                    lastFail = now;
                    ring_.Clear();
                }
                continue;
            }
            Sleep(5);
            UINT32 pad = 0;
            if (FAILED(dev.client->GetCurrentPadding(&pad))) { // Geraet entfernt oder gewechselt
                dev.Close();
                open = false;
                ring_.Clear();
                continue;
            }
            const UINT32 want = pad < kQueueFrames ? kQueueFrames - pad : 0;
            if (want < 240) continue; // erst ab 5 ms nachfuellen
            BYTE* data = nullptr;
            if (FAILED(dev.render->GetBuffer(want, &data))) {
                dev.Close();
                open = false;
                ring_.Clear();
                continue;
            }
            const bool real = reader_.Read(reinterpret_cast<int16_t*>(data), static_cast<int>(want)); // false = Stille (Read schreibt dann Nullen)
            dev.render->ReleaseBuffer(want, 0);
            silent = real ? 0 : silent + want;
            if (silent > kIdleFrames) {
                dev.Close();
                open = false;
                ring_.Clear();
            }
        }
        dev.Close();
        if (SUCCEEDED(hrCom)) CoUninitialize();
    }

    FrameRing           ring_;
    JitterReader        reader_; // nur vom Ausgabe-Thread
    std::thread         thread_;
    std::atomic<bool>   quit_{false};
    void (*logger_)(const char*) = nullptr;
};
