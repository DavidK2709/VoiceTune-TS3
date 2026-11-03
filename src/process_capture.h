
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
#include <string>
#include <thread>

#include "ring_buffer.h"

// Nimmt den Ton eines Prozesses samt Kindprozessen per WASAPI Process-Loopback auf (Windows 10 2004+ / 11)
// und schreibt ihn als 48 kHz / 16 bit / Stereo in den uebergebenen FrameRing. Auf aelteren Windows-10-Versionen: Fallback auf den Gesamt-Ton.
class ProcessCapture {
public:
    explicit ProcessCapture(FrameRing& ring) : ring_(ring) {}
    ~ProcessCapture() { Stop(); }
    ProcessCapture(const ProcessCapture&)            = delete;
    ProcessCapture& operator=(const ProcessCapture&) = delete;

    // Blockiert bis die Aufnahme laeuft oder fehlgeschlagen ist (max. ~8 s).
    bool Start(DWORD pid, std::wstring* error = nullptr);
    void Stop();

    // false, sobald der Capture-Thread endet (App beendet, Geraet entfernt, Fehler).
    bool  IsRunning() const { return running_.load(); }
    DWORD Pid() const { return pid_; }
    // true: Windows kennt keine App-Aufnahme (vor Windows 10 2004), es wird der Gesamt-Ton des Standard-Ausgabegeraets aufgenommen (auch TeamSpeak-Stimmen!).
    bool  SystemWide() const { return systemWide_.load(); }

private:
    void ThreadMain(DWORD pid);

    FrameRing&        ring_;
    std::thread       thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> systemWide_{false};
    HANDLE            stopEvent_    = nullptr;
    HANDLE            startedEvent_ = nullptr;
    HRESULT           startResult_  = E_FAIL;
    DWORD             pid_          = 0;
};
