
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
#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ring_buffer.h"

// Spielt eine Audio-Datei in einen FrameRing (48 kHz / 16 bit / Stereo). Dekodiert wird mit den Windows-Bordmitteln (Media Foundation: mp3, wav, flac,
// m4a/aac, wma ...); schlaegt das fehl, wird ffmpeg.exe (falls im PATH) als Reserve benutzt (ogg, opus u.a.). Die Dekodierung laeuft in einem eigenen Thread
// und wird gebremst: es werden hoechstens ~300 ms Vorrat im Ring gehalten.
class FilePlayer {
public:
    explicit FilePlayer(FrameRing& ring) : ring_(ring) {}
    ~FilePlayer() { Stop(); }
    FilePlayer(const FilePlayer&)            = delete;
    FilePlayer& operator=(const FilePlayer&) = delete;

    // Wird (im Decoder-Thread) aufgerufen, wenn die Datei zu Ende ist oder nicht geoeffnet werden konnte; nicht nach Stop().
    void SetOnEnd(std::function<void()> fn) { onEnd_ = std::move(fn); }

    void Start(const std::wstring& path, double startSec = 0);
    void Stop(); // beendet den Thread und wartet darauf

    void SetPaused(bool paused) { paused_ = paused; }
    bool Paused() const { return paused_.load(); }
    void Seek(double seconds) { seekReq_ = seconds; }

    bool         Running() const { return running_.load(); } // Thread laeuft (auch pausiert)
    bool         Ended() const { return ended_.load(); }     // Ende erreicht oder Fehler (siehe Failed)
    bool         Failed() const { return failed_.load(); }
    std::wstring Error() const;
    double       Position() const; // Sekunden (abzueglich des Vorrats im Ring)
    double       Duration() const { return duration_.load(); } // -1 = unbekannt

    // Laenge einer Datei in Sekunden (Media Foundation), -1 wenn nicht ermittelbar. Blockiert kurz; nicht aus dem UI-Thread fuer viele Dateien aufrufen.
    static double ProbeDuration(const std::wstring& path);

    // Dekodiert eine (kurze) Datei komplett in den Speicher: 48 kHz / 16 bit / Stereo, interleaved; hoechstens `maxSeconds` (danach wird abgeschnitten).
    // Blockiert bis zum Ende; nur aus einem Hintergrund-Thread aufrufen. false, wenn nichts dekodiert werden konnte.
    static bool DecodeToMemory(const std::wstring& path, double maxSeconds, std::vector<int16_t>& out);

private:
    enum class Result { Ended, Stopped, OpenFailed };
    void   Run(std::wstring path, double startSec);
    Result RunMediaFoundation(const std::wstring& path, double startSec);
    Result RunFfmpeg(const std::wstring& path, double startSec);
    bool   WaitForRoom(); // bremst, solange pausiert oder der Ring voll ist; false = Stop angefordert
    // Schreibt in Scheiben von 100 ms und bremst dazwischen (grosse Dekoder-Bloecke wuerden sonst den Ring ueberlaufen lassen).
    // false = Stop oder Sprung angefordert, der Rest des Blocks wird verworfen.
    bool   WriteThrottled(const int16_t* data, size_t frames);

    FrameRing&                ring_;
    std::function<void()>     onEnd_;
    std::thread               thread_;
    std::atomic<bool>         stop_{false}, paused_{false}, running_{false}, ended_{false}, failed_{false};
    std::atomic<double>       seekReq_{-1}, duration_{-1};
    std::atomic<long long>    baseFrames_{0};    // Startposition (Frames) seit dem letzten Springen
    std::atomic<long long>    writtenFrames_{0}; // seit dem letzten Springen in den Ring geschrieben
    mutable std::mutex        errMutex_;
    std::wstring              error_;
};
