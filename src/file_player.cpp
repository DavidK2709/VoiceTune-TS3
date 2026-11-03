
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
 
#include "file_player.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace {

// Media Foundation wird zur Laufzeit geladen: Auf Windows-"N"-Editionen ohne Media Feature Pack fehlen die DLLs, das Plugin muss trotzdem laden
// (dann bleibt nur der ffmpeg-Weg fuer Dateien).
struct MfApi {
    HMODULE plat = nullptr, rw = nullptr;
    HRESULT(WINAPI* Startup)(ULONG, DWORD)                                           = nullptr;
    HRESULT(WINAPI* Shutdown)()                                                      = nullptr;
    HRESULT(WINAPI* CreateMediaType)(IMFMediaType**)                                 = nullptr;
    HRESULT(WINAPI* CreateReaderFromURL)(LPCWSTR, IMFAttributes*, IMFSourceReader**) = nullptr;
    bool ok = false;
};
const MfApi& Mf()
{
    static const MfApi api = [] {
        MfApi a;
        a.plat = LoadLibraryW(L"mfplat.dll");
        a.rw   = LoadLibraryW(L"mfreadwrite.dll");
        if (!a.plat || !a.rw) return a;
        auto fn = [](HMODULE m, const char* name) { return reinterpret_cast<void*>(GetProcAddress(m, name)); };
        a.Startup             = reinterpret_cast<decltype(a.Startup)>(fn(a.plat, "MFStartup"));
        a.Shutdown            = reinterpret_cast<decltype(a.Shutdown)>(fn(a.plat, "MFShutdown"));
        a.CreateMediaType     = reinterpret_cast<decltype(a.CreateMediaType)>(fn(a.plat, "MFCreateMediaType"));
        a.CreateReaderFromURL = reinterpret_cast<decltype(a.CreateReaderFromURL)>(fn(a.rw, "MFCreateSourceReaderFromURL"));
        a.ok                  = a.Startup && a.Shutdown && a.CreateMediaType && a.CreateReaderFromURL;
        return a;
    }();
    return api;
}

constexpr size_t kMaxBuffered = 14400; // 300 ms Vorrat im Ring, mehr wird nicht vorausdekodiert
constexpr int    kRate        = 48000;

template <class T>
void SafeRelease(T*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// Media-Foundation-Reader mit Ausgabe 48 kHz / 16 bit / Stereo PCM. nullptr bei Fehler.
IMFSourceReader* OpenReader(const std::wstring& path)
{
    IMFSourceReader* reader = nullptr;
    if (FAILED((Mf().ok ? Mf().CreateReaderFromURL(path.c_str(), nullptr, &reader) : E_FAIL)) || !reader) return nullptr;
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    if (FAILED(reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE))) {
        reader->Release();
        return nullptr;
    }
    IMFMediaType* type = nullptr;
    HRESULT       hr   = Mf().CreateMediaType(&type);
    if (SUCCEEDED(hr)) {
        type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        type->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        type->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, 2);
        type->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, kRate);
        type->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        type->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, 4);
        type->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, kRate * 4);
        type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
        hr = reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, type);
        type->Release();
    }
    if (FAILED(hr)) {
        reader->Release();
        return nullptr;
    }
    return reader;
}

double ReaderDuration(IMFSourceReader* reader)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    double d = -1;
    if (SUCCEEDED(reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &pv)) && pv.vt == VT_UI8)
        d = static_cast<double>(pv.uhVal.QuadPart) / 1e7;
    PropVariantClear(&pv);
    return d;
}

bool SeekReader(IMFSourceReader* reader, double seconds)
{
    PROPVARIANT pv;
    PropVariantInit(&pv);
    pv.vt             = VT_I8;
    pv.hVal.QuadPart  = static_cast<LONGLONG>(std::max(0.0, seconds) * 1e7);
    const HRESULT hr  = reader->SetCurrentPosition(GUID_NULL, pv);
    PropVariantClear(&pv);
    return SUCCEEDED(hr);
}

} // namespace

void FilePlayer::Start(const std::wstring& path, double startSec)
{
    Stop();
    stop_ = false;
    ended_ = failed_ = false;
    paused_          = false;
    seekReq_         = -1;
    duration_        = -1;
    baseFrames_      = static_cast<long long>(std::max(0.0, startSec) * kRate);
    writtenFrames_   = 0;
    {
        std::lock_guard<std::mutex> lk(errMutex_);
        error_.clear();
    }
    running_ = true;
    thread_  = std::thread(&FilePlayer::Run, this, path, startSec);
}

void FilePlayer::Stop()
{
    stop_ = true;
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

std::wstring FilePlayer::Error() const
{
    std::lock_guard<std::mutex> lk(errMutex_);
    return error_;
}

double FilePlayer::Position() const
{
    const double decoded = static_cast<double>(baseFrames_.load() + writtenFrames_.load()) / kRate;
    return std::max(0.0, decoded - static_cast<double>(ring_.Size()) / kRate);
}

bool FilePlayer::WaitForRoom()
{
    while (!stop_) {
        if (seekReq_.load() >= 0) return true; // Springen gleich bearbeiten, auch wenn pausiert
        if (!paused_ && ring_.Size() < kMaxBuffered) return true;
        Sleep(8);
    }
    return false;
}

void FilePlayer::Run(std::wstring path, double startSec)
{
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool    mf    = Mf().ok && SUCCEEDED(Mf().Startup(MF_VERSION, MFSTARTUP_LITE));

    Result r = mf ? RunMediaFoundation(path, startSec) : Result::OpenFailed;
    if (r == Result::OpenFailed && !stop_) r = RunFfmpeg(path, startSec);
    if (r == Result::OpenFailed) {
        failed_ = true;
        std::lock_guard<std::mutex> lk(errMutex_);
        error_ = L"Die Datei konnte nicht gelesen werden (Format nicht unterstützt?)";
    }

    if (mf) Mf().Shutdown();
    if (SUCCEEDED(hrCom)) CoUninitialize();
    running_ = false;
    if (r != Result::Stopped && !stop_) {
        ended_ = true;
        if (onEnd_) onEnd_();
    }
}

bool FilePlayer::WriteThrottled(const int16_t* data, size_t frames)
{
    constexpr size_t kSlice = 4800;
    while (frames > 0 && !stop_) {
        if (seekReq_.load() >= 0) return false;
        if (paused_ || ring_.Size() >= kMaxBuffered) {
            Sleep(8);
            continue;
        }
        const size_t n = std::min(frames, kSlice);
        ring_.Write(data, n);
        writtenFrames_ += static_cast<long long>(n);
        data += n * 2;
        frames -= n;
    }
    return !stop_;
}

FilePlayer::Result FilePlayer::RunMediaFoundation(const std::wstring& path, double startSec)
{
    IMFSourceReader* reader = OpenReader(path);
    if (!reader) return Result::OpenFailed;
    duration_ = ReaderDuration(reader);
    if (startSec > 0.01) SeekReader(reader, startSec);

    Result              result = Result::Ended;
    std::vector<int16_t> pending;
    while (!stop_) {
        if (!WaitForRoom()) {
            result = Result::Stopped;
            break;
        }
        const double seek = seekReq_.exchange(-1);
        if (seek >= 0) {
            SeekReader(reader, seek);
            ring_.Clear();
            baseFrames_    = static_cast<long long>(seek * kRate);
            writtenFrames_ = 0;
            continue;
        }
        if (paused_ || ring_.Size() >= kMaxBuffered) continue;

        DWORD        flags = 0;
        IMFSample*   sample = nullptr;
        const HRESULT hr = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample);
        if (FAILED(hr)) {
            result = writtenFrames_ > 0 ? Result::Ended : Result::OpenFailed; // gleich am Anfang kaputt -> Reserve probieren
            break;
        }
        if (sample) {
            IMFMediaBuffer* buf = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
                BYTE* data = nullptr;
                DWORD len  = 0;
                if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                    pending.assign(reinterpret_cast<const int16_t*>(data), reinterpret_cast<const int16_t*>(data) + (len / 4) * 2);
                    buf->Unlock();
                }
                buf->Release();
            }
            sample->Release();
        }
        if (!pending.empty()) WriteThrottled(pending.data(), pending.size() / 2); // bei Sprung/Stop wird der Rest verworfen; die Schleife behandelt es oben
        pending.clear();
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (flags & MF_SOURCE_READERF_ERROR) {
            result = writtenFrames_ > 0 ? Result::Ended : Result::OpenFailed;
            break;
        }
    }
    reader->Release();
    return stop_ ? Result::Stopped : result;
}

// Reserve: ffmpeg.exe (falls im PATH) dekodiert nach rohem PCM und schreibt es auf eine Pipe.
FilePlayer::Result FilePlayer::RunFfmpeg(const std::wstring& path, double startSec)
{
    wchar_t ffmpeg[MAX_PATH * 2] = {};
    if (!SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH * 2, ffmpeg, nullptr)) return Result::OpenFailed;

    double       from = std::max(0.0, startSec);
    Result       result = Result::OpenFailed;
    bool         gotData = false;
    for (;;) { // eine Runde je Start bzw. Sprung (ffmpeg wird mit -ss neu gestartet)
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        HANDLE              rd = nullptr, wr = nullptr;
        if (!CreatePipe(&rd, &wr, &sa, 1 << 16)) return result;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);

        wchar_t secs[32];
        swprintf(secs, 32, L"%.3f", from);
        std::wstring cmd = L"\"" + std::wstring(ffmpeg) + L"\" -v quiet -nostdin -ss " + secs + L" -i \"" + path + L"\" -vn -f s16le -ar 48000 -ac 2 pipe:1";
        STARTUPINFOW si{};
        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdOutput = wr;
        si.hStdError  = nullptr;
        si.hStdInput  = nullptr;
        PROCESS_INFORMATION pi{};
        const BOOL          ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
        CloseHandle(wr);
        if (!ok) {
            CloseHandle(rd);
            return result;
        }

        baseFrames_    = static_cast<long long>(from * kRate);
        writtenFrames_ = 0;
        std::vector<char> buf(16384);
        size_t            carry = 0; // Rest eines unvollstaendigen Frames (4 Byte)
        bool              restart = false;
        while (!stop_) {
            if (!WaitForRoom()) break;
            const double seek = seekReq_.exchange(-1);
            if (seek >= 0) {
                from    = seek;
                restart = true;
                ring_.Clear();
                break;
            }
            if (paused_ || ring_.Size() >= kMaxBuffered) continue;
            DWORD got = 0;
            if (!ReadFile(rd, buf.data() + carry, static_cast<DWORD>(buf.size() - carry), &got, nullptr) || got == 0) { // Ende der Datei
                result = Result::Ended;
                break;
            }
            gotData = true;
            const size_t total = carry + got, frames = total / 4;
            WriteThrottled(reinterpret_cast<const int16_t*>(buf.data()), frames);
            carry = total - frames * 4;
            if (carry) std::memmove(buf.data(), buf.data() + frames * 4, carry);
        }
        TerminateProcess(pi.hProcess, 0);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(rd);
        if (restart && !stop_) continue;
        if (stop_) return Result::Stopped;
        return gotData ? Result::Ended : Result::OpenFailed;
    }
}

double FilePlayer::ProbeDuration(const std::wstring& path)
{
    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    double        d     = -1;
    if (Mf().ok && SUCCEEDED(Mf().Startup(MF_VERSION, MFSTARTUP_LITE))) {
        if (IMFSourceReader* reader = OpenReader(path)) {
            d = ReaderDuration(reader);
            reader->Release();
        }
        Mf().Shutdown();
    }
    if (SUCCEEDED(hrCom)) CoUninitialize();
    return d;
}

bool FilePlayer::DecodeToMemory(const std::wstring& path, double maxSeconds, std::vector<int16_t>& out)
{
    out.clear();
    const size_t maxFrames = static_cast<size_t>(std::max(0.1, maxSeconds) * kRate);

    const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (Mf().ok && SUCCEEDED(Mf().Startup(MF_VERSION, MFSTARTUP_LITE))) {
        if (IMFSourceReader* reader = OpenReader(path)) {
            while (out.size() / 2 < maxFrames) {
                DWORD         flags  = 0;
                IMFSample*    sample = nullptr;
                const HRESULT hr     = reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, nullptr, &sample);
                if (FAILED(hr)) break;
                if (sample) {
                    IMFMediaBuffer* buf = nullptr;
                    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
                        BYTE* data = nullptr;
                        DWORD len  = 0;
                        if (SUCCEEDED(buf->Lock(&data, nullptr, &len))) {
                            out.insert(out.end(), reinterpret_cast<const int16_t*>(data), reinterpret_cast<const int16_t*>(data) + (len / 4) * 2);
                            buf->Unlock();
                        }
                        buf->Release();
                    }
                    sample->Release();
                }
                if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR)) break;
            }
            reader->Release();
        }
        Mf().Shutdown();
    }
    if (SUCCEEDED(hrCom)) CoUninitialize();

    if (out.empty()) { // Reserve: ffmpeg.exe (ogg, opus u.a.)
        wchar_t ffmpeg[MAX_PATH * 2] = {};
        if (SearchPathW(nullptr, L"ffmpeg.exe", nullptr, MAX_PATH * 2, ffmpeg, nullptr)) {
            SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
            HANDLE              rd = nullptr, wr = nullptr;
            if (CreatePipe(&rd, &wr, &sa, 1 << 16)) {
                SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
                wchar_t secs[32];
                swprintf(secs, 32, L"%.3f", maxSeconds);
                std::wstring cmd = L"\"" + std::wstring(ffmpeg) + L"\" -v quiet -nostdin -i \"" + path + L"\" -t " + secs + L" -vn -f s16le -ar 48000 -ac 2 pipe:1";
                STARTUPINFOW si{};
                si.cb         = sizeof(si);
                si.dwFlags    = STARTF_USESTDHANDLES;
                si.hStdOutput = wr;
                PROCESS_INFORMATION pi{};
                const BOOL          ok = CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
                CloseHandle(wr);
                if (ok) {
                    std::vector<char> buf(1 << 16);
                    std::vector<char> bytes;
                    DWORD             got = 0;
                    while (bytes.size() / 4 < maxFrames && ReadFile(rd, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr) && got > 0) bytes.insert(bytes.end(), buf.data(), buf.data() + got);
                    TerminateProcess(pi.hProcess, 0);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    const size_t frames = bytes.size() / 4;
                    out.resize(frames * 2);
                    if (frames) std::memcpy(out.data(), bytes.data(), frames * 4);
                }
                CloseHandle(rd);
            }
        }
    }
    if (out.size() / 2 > maxFrames) out.resize(maxFrames * 2);
    return !out.empty();
}
