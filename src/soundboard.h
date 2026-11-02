#pragma once
// Soundboard: kurze Clips (Jingles) in 8 Slots. Clips werden komplett in den Speicher dekodiert (48 kHz / 16 bit / Stereo, hoechstens 30 s) und beim
// Ausloesen als "Stimme" ueber das Mikrofon-Signal gemischt (bis zu 4 gleichzeitig). Ausloesen (Play/StopAll) darf aus jedem Thread kommen; Mix() laeuft
// im Audio-Thread und blockiert nie (nur Atomics).
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Soundboard {
public:
    static constexpr int    kSlots      = 8;
    static constexpr int    kVoices     = 4;
    static constexpr double kMaxSeconds = 30.0;

    enum class State { Empty, Loading, Ready, Failed };

    Soundboard();  // startet den Lade-Thread
    ~Soundboard(); // beendet ihn, falls noch nicht geschehen
    // Lade-Thread beenden bzw. (nach Stop) wieder starten. Das Plugin muss ihn in ts3plugin_shutdown beenden: Beim Entladen der DLL mit dem Prozessende
    // gibt es keine anderen Threads mehr, ein join im Destruktor wuerde dann ewig warten.
    void Start();
    void Stop();
    Soundboard(const Soundboard&)            = delete;
    Soundboard& operator=(const Soundboard&) = delete;

    // Slot mit einer Datei belegen: der Slot wird sofort geleert, dekodiert wird im Hintergrund (State Loading -> Ready/Failed).
    void Load(int slot, const std::wstring& path);
    // Slot direkt aus PCM (48 kHz / Stereo interleaved) belegen (Tests).
    void LoadMemory(int slot, std::vector<int16_t> pcm);
    void Clear(int slot);

    State  GetState(int slot) const;
    double Seconds(int slot) const; // Laenge des Clips, 0 wenn nicht bereit

    // Ausloesen (jeder Thread). Ein laufender Clip desselben Slots startet von vorn.
    void Play(int slot);
    void StopAll();
    // Laeuft oder steht gerade ein Clip an? Fuer "Quelle aktiv" (Sende-Bit, Musikmodus). Auch aus dem Audio-Thread.
    bool Active() const { return active_.load(std::memory_order_relaxed) > 0 || pending_.load(std::memory_order_relaxed) > 0; }

    // Audio-Thread: addiert alle laufenden Stimmen (mit Verstaerkung `gain`) in `io` (interleaved Stereo, Saettigung). true = es wurde etwas gemischt.
    bool Mix(int16_t* io, int frames, float gain);

private:
    struct Slot {
        std::shared_ptr<const std::vector<int16_t>> clip; // nur ueber std::atomic_load/_store
        std::atomic<State>                          state{State::Empty};
        std::atomic<double>                         seconds{0};
        std::atomic<uint32_t>                       generation{0}; // verwirft Ergebnisse veralteter Ladevorgaenge
        std::atomic<uint32_t>                       trigger{0};    // offene Play()-Aufrufe
    };
    struct Voice {
        std::shared_ptr<const std::vector<int16_t>> clip;
        size_t                                      pos  = 0; // in Frames
        int                                         slot = -1;
    };
    struct Request {
        int          slot;
        uint32_t     generation;
        std::wstring path;
    };

    void Publish(int slot, uint32_t generation, std::vector<int16_t>&& pcm);
    void WorkerMain();

    Slot                    slots_[kSlots];
    Voice                   voices_[kVoices]; // nur Audio-Thread
    std::atomic<int>        active_{0};
    std::atomic<int>        pending_{0};
    std::atomic<bool>       stopAll_{false};

    std::mutex              mutex_; // Ladeauftraege
    std::condition_variable cv_;
    std::deque<Request>     queue_;
    bool                    quit_ = false;
    std::thread             worker_;
};
