#include "process_capture.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propidl.h>

#include <cstdio>

// Die MinGW-Header enthalten die Process-Loopback-Strukturen nicht immer.
#if __has_include(<audioclientactivationparams.h>)
#include <audioclientactivationparams.h>
#else
typedef enum { AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0, AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1 } AUDIOCLIENT_ACTIVATION_TYPE;
typedef enum { PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0, PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1 } PROCESS_LOOPBACK_MODE;
typedef struct {
    DWORD                 TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;
typedef struct {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union {
        AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
    };
} AUDIOCLIENT_ACTIVATION_PARAMS;
#endif
#ifndef VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif

namespace {

constexpr WORD  kChannels   = 2;
constexpr DWORD kSampleRate = 48000;
constexpr WORD  kBits       = 16;

// Completion-Handler fuer ActivateAudioInterfaceAsync; muss agil (IAgileObject) sein, weil er auf einem Worker-Thread laeuft.
class ActivateHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    ActivateHandler() : event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivateHandler()
    {
        if (client_) client_->Release();
        CloseHandle(event_);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (riid == IID_IUnknown || riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        else if (riid == IID_IAgileObject) *ppv = static_cast<IAgileObject*>(this);
        else {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override
    {
        ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }

    STDMETHODIMP ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override
    {
        HRESULT   hrActivate = E_FAIL;
        IUnknown* unk        = nullptr;
        HRESULT   hr         = op->GetActivateResult(&hrActivate, &unk);
        result_              = FAILED(hr) ? hr : hrActivate;
        if (SUCCEEDED(result_) && unk) result_ = unk->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client_));
        if (unk) unk->Release();
        SetEvent(event_);
        return S_OK;
    }

    // Wartet auf das Ergebnis; Rueckgabe: IAudioClient (Besitz geht an den Aufrufer) oder nullptr.
    IAudioClient* Wait(DWORD timeoutMs, HRESULT* hr)
    {
        if (WaitForSingleObject(event_, timeoutMs) != WAIT_OBJECT_0) {
            *hr = HRESULT_FROM_WIN32(WAIT_TIMEOUT);
            return nullptr;
        }
        *hr           = result_;
        IAudioClient* c = client_;
        client_       = nullptr;
        return c;
    }

private:
    std::atomic<ULONG> refs_{1};
    HANDLE             event_;
    HRESULT            result_ = E_FAIL;
    IAudioClient*      client_ = nullptr;
};

using ActivateFn = HRESULT(WINAPI*)(LPCWSTR, REFIID, PROPVARIANT*, IActivateAudioInterfaceCompletionHandler*, IActivateAudioInterfaceAsyncOperation**);

IAudioClient* ActivateProcessLoopback(DWORD pid, HRESULT* hr)
{
    HMODULE mm = LoadLibraryW(L"mmdevapi.dll");
    auto    fn = mm ? reinterpret_cast<ActivateFn>(reinterpret_cast<void*>(GetProcAddress(mm, "ActivateAudioInterfaceAsync"))) : nullptr;
    if (!fn) {
        *hr = HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
        return nullptr;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType                                   = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId            = pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode        = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv{};
    pv.vt             = VT_BLOB;
    pv.blob.cbSize    = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);

    auto*                                 handler = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* op     = nullptr;
    *hr                                           = fn(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv, handler, &op);
    IAudioClient* client = nullptr;
    if (SUCCEEDED(*hr)) client = handler->Wait(5000, hr);
    if (op) op->Release();
    handler->Release();
    return client;
}

// Process-Loopback gibt es ab Windows 10 Version 2004 (Build 19041). Aeltere Windows-10-Versionen nehmen stattdessen den Gesamt-Ton des Standard-Ausgabegeraets auf.
bool ProcessLoopbackSupported()
{
    if (GetEnvironmentVariableW(L"VOICETUNE_FORCE_SYSTEM", nullptr, 0) > 0) return false; // Test: Fallback auf den Gesamt-Ton erzwingen
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto    fn = nt ? reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(nt, "RtlGetVersion"))) : nullptr;
    OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (!fn || fn(&vi) != 0) return true; // unbekannt: den normalen Weg versuchen
    return vi.dwMajorVersion > 10 || (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= 19041);
}

IAudioClient* ActivateSystemLoopback(HRESULT* hr)
{
    IMMDeviceEnumerator* en  = nullptr;
    IMMDevice*           dev = nullptr;
    IAudioClient*        client = nullptr;
    *hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en));
    if (SUCCEEDED(*hr)) *hr = en->GetDefaultAudioEndpoint(eRender, eConsole, &dev);
    if (SUCCEEDED(*hr)) *hr = dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&client));
    if (dev) dev->Release();
    if (en) en->Release();
    return client;
}

} // namespace

bool ProcessCapture::Start(DWORD pid, std::wstring* error)
{
    Stop();
    pid_          = pid;
    stopEvent_    = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    startedEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    startResult_  = E_FAIL;
    thread_       = std::thread(&ProcessCapture::ThreadMain, this, pid);

    bool ok = WaitForSingleObject(startedEvent_, 8000) == WAIT_OBJECT_0 && SUCCEEDED(startResult_);
    if (!ok) {
        if (error) {
            wchar_t buf[64];
            swprintf(buf, 64, L"Capture-Start fehlgeschlagen (0x%08lX)", static_cast<unsigned long>(startResult_));
            *error = buf;
        }
        Stop();
    }
    return ok;
}

void ProcessCapture::Stop()
{
    if (stopEvent_) SetEvent(stopEvent_);
    if (thread_.joinable()) thread_.join();
    if (stopEvent_) CloseHandle(stopEvent_);
    if (startedEvent_) CloseHandle(startedEvent_);
    stopEvent_ = startedEvent_ = nullptr;
    running_   = false;
}

void ProcessCapture::ThreadMain(DWORD pid)
{
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    // Audio-Thread-Prioritaet (MMCSS "Pro Audio"), damit Last durch Spiele/Browser keine Luecken in den Capture-Paketen erzeugt.
    HANDLE mmcss = nullptr;
    if (HMODULE avrt = LoadLibraryW(L"avrt.dll")) {
        using AvSet = HANDLE(WINAPI*)(LPCWSTR, LPDWORD);
        auto  fn    = reinterpret_cast<AvSet>(reinterpret_cast<void*>(GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW")));
        DWORD task  = 0;
        if (fn) mmcss = fn(L"Pro Audio", &task);
    }
    if (!mmcss) SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    HRESULT             hr      = E_FAIL;
    const bool          perProcess = ProcessLoopbackSupported();
    systemWide_.store(!perProcess);
    IAudioClient*       client  = perProcess ? ActivateProcessLoopback(pid, &hr) : ActivateSystemLoopback(&hr);
    bool                polling = false; // nur Gesamt-Ton: falls das Geraet keinen Ereignis-Betrieb kann, wird alle 10 ms abgefragt
    IAudioCaptureClient* capture = nullptr;
    HANDLE              sampleEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE              procHandle  = OpenProcess(SYNCHRONIZE, FALSE, pid);

    if (client) {
        WAVEFORMATEX fmt{};
        fmt.wFormatTag      = WAVE_FORMAT_PCM;
        fmt.nChannels       = kChannels;
        fmt.nSamplesPerSec  = kSampleRate;
        fmt.wBitsPerSample  = kBits;
        fmt.nBlockAlign     = kChannels * kBits / 8;
        fmt.nAvgBytesPerSec = kSampleRate * fmt.nBlockAlign;

        const REFERENCE_TIME bufferDuration = 2000000; // 200 ms
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                bufferDuration, 0, &fmt, nullptr);
        if (FAILED(hr)) // aeltere Builds kennen AUTOCONVERTPCM auf dem virtuellen Geraet evtl. nicht
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK, bufferDuration, 0, &fmt, nullptr);
        if (FAILED(hr) && !perProcess) { // Gesamt-Ton ohne Ereignis-Betrieb (Abfrage)
            polling = true;
            hr      = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, bufferDuration, 0, &fmt, nullptr);
        }
        if (SUCCEEDED(hr) && !polling) hr = client->SetEventHandle(sampleEvent);
        if (SUCCEEDED(hr)) hr = client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&capture));
        if (SUCCEEDED(hr)) hr = client->Start();
    }

    startResult_ = (client && capture) ? hr : (FAILED(hr) ? hr : E_FAIL);
    if (SUCCEEDED(startResult_)) running_ = true;
    SetEvent(startedEvent_);

    if (SUCCEEDED(startResult_)) {
        HANDLE waits[3] = {stopEvent_, sampleEvent, procHandle};
        DWORD  nWaits   = procHandle ? 3 : 2;
        bool   alive    = true;
        while (alive) {
            DWORD w = WaitForMultipleObjects(nWaits, waits, FALSE, polling ? 10 : 200);
            if (w == WAIT_OBJECT_0 || w == WAIT_OBJECT_0 + 2 || w == WAIT_FAILED) break; // Stop oder App beendet
            for (;;) {
                UINT32 packet = 0;
                if (FAILED(capture->GetNextPacketSize(&packet))) {
                    alive = false;
                    break;
                }
                if (packet == 0) break;
                BYTE*  data   = nullptr;
                UINT32 frames = 0;
                DWORD  flags  = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                    alive = false;
                    break;
                }
                ring_.Write((flags & AUDCLNT_BUFFERFLAGS_SILENT) ? nullptr : reinterpret_cast<const int16_t*>(data), frames);
                capture->ReleaseBuffer(frames);
            }
        }
        client->Stop();
    }

    running_ = false;
    if (capture) capture->Release();
    if (client) client->Release();
    if (procHandle) CloseHandle(procHandle);
    CloseHandle(sampleEvent);
    if (SUCCEEDED(hrCom)) CoUninitialize();
}
