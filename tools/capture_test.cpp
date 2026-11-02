// capture_test.exe --list
// capture_test.exe <exe-name|PID> [sekunden=5] [ausgabe.wav]
// Testet App-Erkennung und Process-Loopback-Capture ohne TeamSpeak.
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "app_discovery.h"
#include "process_capture.h"

static void WriteWav(const wchar_t* path, const std::vector<int16_t>& pcm)
{
    FILE* f = _wfopen(path, L"wb");
    if (!f) return;
    uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2), riff = 36 + dataBytes, fmtSize = 16, rate = 48000, byteRate = 48000 * 4;
    uint16_t fmtTag = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtSize, 4, 1, f);
    fwrite(&fmtTag, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f); fwrite(pcm.data(), 2, pcm.size(), f);
    fclose(f);
}

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2 || std::wstring(argv[1]) == L"--list") {
        for (const auto& a : DiscoverApps()) wprintf(L"PID %-6lu %ls\n", a.pid, DisplayName(a).c_str());
        return 0;
    }

    DWORD pid = static_cast<DWORD>(wcstoul(argv[1], nullptr, 10));
    if (pid == 0) {
        auto  apps = DiscoverApps();
        auto* app  = FindByExe(apps, ToLower(argv[1]));
        if (!app) { fwprintf(stderr, L"App '%ls' nicht gefunden (--list zeigt die erkannten Apps)\n", argv[1]); return 1; }
        pid = app->pid;
    }
    int          seconds = argc > 2 ? _wtoi(argv[2]) : 5;
    const wchar_t* out   = argc > 3 ? argv[3] : L"capture.wav";

    FrameRing      ring(48000 * 2);
    ProcessCapture cap(ring);
    std::wstring   err;
    if (!cap.Start(pid, &err)) { fwprintf(stderr, L"%ls\n", err.c_str()); return 2; }
    wprintf(L"Nehme PID %lu fuer %d s auf...\n", pid, seconds);

    std::vector<int16_t> pcm;
    std::vector<int16_t> block(480 * 2);
    DWORD                end = GetTickCount() + seconds * 1000;
    while (GetTickCount() < end && cap.IsRunning()) {
        if (ring.ReadExact(block.data(), 480)) pcm.insert(pcm.end(), block.begin(), block.end());
        else Sleep(5);
    }
    cap.Stop();

    int peak = 0;
    for (int16_t s : pcm) peak = std::max(peak, std::abs(static_cast<int>(s)));
    WriteWav(out, pcm);
    wprintf(L"%zu Frames (%.2f s), Spitzenpegel %d/32767 -> %ls\n", pcm.size() / 2, pcm.size() / 2 / 48000.0, peak, out);
    return peak > 0 ? 0 : 3;
}
