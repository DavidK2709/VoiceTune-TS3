#pragma once
// Soundboard: kurze Clips (Jingles) auf bis zu 400 Knoepfen (Slot = Knopf-ID - 1). Clips werden in den Speicher dekodiert (48 kHz / 16 bit / Stereo, hoechstens
// 30 s) und beim Ausloesen als "Stimme" ueber das Mikrofon-Signal gemischt (bis zu 8 gleichzeitig). Damit viele Knoepfe moeglich sind, gibt es ein Speicherbudget:
// wird es ueberschritten, werden die am laengsten nicht gespielten Clips wieder verworfen (State Unloaded) und beim naechsten Klick im Hintergrund neu geladen
// (der Clip spielt, sobald er da ist). Ausloesen (Play/StopAll) darf aus jedem Thread kommen; Mix() laeuft im Audio-Thread und blockiert nie (nur Atomics).
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
    static constexpr int    kSlots       = 400;
    static constexpr int    kVoices      = 8;
    static constexpr double kMaxSeconds  = 30.0;
    static constexpr size_t kBudgetBytes = 192u * 1024u * 1024u; // dekodierte Clips insgesamt

    enum class State { Empty, Loading, Ready, Unloaded, Failed };

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
    // Slot direkt aus PCM (48 kHz / Stereo interleaved) belegen (Tests; ohne Pfad, daher nicht nachladbar).
    void LoadMemory(int slot, std::vector<int16_t> pcm);
    // Pfad merken, ohne zu dekodieren (State Unloaded): der Clip wird erst beim ersten Ausloesen geladen. Fuer viele Knoepfe beim Start.
    void Register(int slot, const std::wstring& path);
    void Clear(int slot);
    void SetVolume(int slot, int pct); // Lautstaerke dieses Knopfes in Prozent (0..200), wirkt ab dem naechsten Ausloesen

    State  GetState(int slot) const;
    double Seconds(int slot) const; // Laenge des Clips (bleibt auch nach dem Verwerfen bekannt), 0 wenn unbekannt

    // Speicherbudget (Standard kBudgetBytes); nur fuer Tests aendern.
    void   SetBudget(size_t bytes) { budget_ = bytes; }
    size_t CachedBytes() const { return bytes_.load(); }

    // Ausloesen (jeder Thread). Ein laufender Clip desselben Slots startet von vorn. Ein verworfener oder gerade ladender Clip spielt, sobald er geladen ist.
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
        std::atomic<uint32_t>                       generation{0};     // verwirft Ergebnisse veralteter Ladevorgaenge
        std::atomic<uint32_t>                       trigger{0};        // offene Play()-Aufrufe
        std::atomic<int>                            volume{100};       // Prozent
        std::atomic<uint64_t>                       lastUse{0};        // fuer die Verdraengung (groesser = juenger)
        std::atomic<bool>                           playWhenReady{false};
        std::atomic<size_t>                         size{0};           // Bytes des geladenen Clips
        std::wstring                                path;              // unter mutex_
    };
    struct Voice {
        std::shared_ptr<const std::vector<int16_t>> clip;
        size_t                                      pos  = 0; // in Frames
        int                                         slot = -1;
        float                                       mul  = 1.0f; // Lautstaerke des Knopfes
    };
    struct Request {
        int          slot;
        uint32_t     generation;
        std::wstring path;
    };

    void Publish(int slot, uint32_t generation, std::vector<int16_t>&& pcm);
    void Evict(int keep);
    void Enqueue(int slot, uint32_t generation, const std::wstring& path);
    void WorkerMain();

    Slot                    slots_[kSlots];
    Voice                   voices_[kVoices]; // nur Audio-Thread
    std::atomic<int>        active_{0};
    std::atomic<int>        pending_{0};
    std::atomic<bool>       stopAll_{false};
    std::atomic<size_t>     bytes_{0};
    std::atomic<size_t>     budget_{kBudgetBytes};
    std::atomic<uint64_t>   useCounter_{0};

    std::mutex              mutex_; // Ladeauftraege und Pfade
    std::condition_variable cv_;
    std::deque<Request>     queue_;
    bool                    quit_ = false;
    std::thread             worker_;
};
