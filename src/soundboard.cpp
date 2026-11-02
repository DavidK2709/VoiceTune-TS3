#include "soundboard.h"

#include <algorithm>

#include "file_player.h"

Soundboard::Soundboard() { Start(); }

Soundboard::~Soundboard() { Stop(); }

void Soundboard::Start()
{
    std::lock_guard<std::mutex> lk(mutex_);
    if (worker_.joinable()) return;
    quit_   = false;
    worker_ = std::thread(&Soundboard::WorkerMain, this);
}

void Soundboard::Stop()
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Soundboard::Load(int slot, const std::wstring& path)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot& s = slots_[slot];
    const uint32_t gen = s.generation.fetch_add(1) + 1;
    std::atomic_store(&s.clip, std::shared_ptr<const std::vector<int16_t>>());
    s.seconds = 0;
    if (path.empty()) {
        s.state = State::Empty;
        return;
    }
    s.state = State::Loading;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        queue_.push_back({slot, gen, path});
    }
    cv_.notify_one();
}

void Soundboard::Clear(int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot& s = slots_[slot];
    s.generation.fetch_add(1);
    std::atomic_store(&s.clip, std::shared_ptr<const std::vector<int16_t>>());
    s.seconds = 0;
    s.state   = State::Empty;
}

void Soundboard::LoadMemory(int slot, std::vector<int16_t> pcm)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot& s = slots_[slot];
    Publish(slot, s.generation.fetch_add(1) + 1, std::move(pcm));
}

void Soundboard::Publish(int slot, uint32_t generation, std::vector<int16_t>&& pcm)
{
    Slot& s = slots_[slot];
    if (s.generation.load() != generation) return; // inzwischen neu belegt oder geleert
    if (pcm.size() < 2) {
        s.state = State::Failed;
        return;
    }
    const double seconds = static_cast<double>(pcm.size() / 2) / 48000.0;
    std::atomic_store(&s.clip, std::shared_ptr<const std::vector<int16_t>>(std::make_shared<const std::vector<int16_t>>(std::move(pcm))));
    s.seconds = seconds;
    s.state   = State::Ready;
}

Soundboard::State Soundboard::GetState(int slot) const { return (slot < 0 || slot >= kSlots) ? State::Empty : slots_[slot].state.load(); }
double            Soundboard::Seconds(int slot) const { return (slot < 0 || slot >= kSlots) ? 0.0 : slots_[slot].seconds.load(); }

void Soundboard::Play(int slot)
{
    if (slot < 0 || slot >= kSlots || slots_[slot].state.load() != State::Ready) return;
    slots_[slot].trigger.fetch_add(1);
    pending_.fetch_add(1);
}

void Soundboard::StopAll()
{
    stopAll_ = true;
    for (auto& s : slots_) { // noch nicht gestartete Ausloeser verwerfen
        const uint32_t n = s.trigger.exchange(0);
        if (n) pending_.fetch_sub(static_cast<int>(n));
    }
}

bool Soundboard::Mix(int16_t* io, int frames, float gain)
{
    if (stopAll_.exchange(false)) {
        for (auto& v : voices_) v = Voice();
        active_ = 0;
    }
    // Neue Ausloeser in Stimmen umsetzen
    for (int i = 0; i < kSlots; ++i) {
        const uint32_t n = slots_[i].trigger.exchange(0);
        if (!n) continue;
        pending_.fetch_sub(static_cast<int>(n));
        auto clip = std::atomic_load(&slots_[i].clip);
        if (!clip) continue;
        Voice* target = nullptr;
        for (auto& v : voices_)
            if (v.clip && v.slot == i) target = &v; // derselbe Clip laeuft: von vorn
        if (!target)
            for (auto& v : voices_)
                if (!v.clip) { target = &v; break; }
        if (!target) { // alle belegt: die am weitesten fortgeschrittene ersetzen
            target = &voices_[0];
            for (auto& v : voices_)
                if (v.pos > target->pos) target = &v;
        }
        target->clip = std::move(clip);
        target->pos  = 0;
        target->slot = i;
    }

    bool mixed = false;
    int  live  = 0;
    for (auto& v : voices_) {
        if (!v.clip) continue;
        const size_t total = v.clip->size() / 2;
        const size_t n     = std::min<size_t>(static_cast<size_t>(frames), total - std::min(total, v.pos));
        const int16_t* src = v.clip->data() + v.pos * 2;
        for (size_t k = 0; k < n * 2; ++k) {
            const int sum = io[k] + static_cast<int>(src[k] * gain);
            io[k]         = static_cast<int16_t>(std::clamp(sum, -32768, 32767));
        }
        mixed |= n > 0;
        v.pos += n;
        if (v.pos >= total) v = Voice();
        else ++live;
    }
    active_ = live;
    return mixed;
}

void Soundboard::WorkerMain()
{
    for (;;) {
        Request r;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return quit_ || !queue_.empty(); });
            if (quit_) return;
            r = std::move(queue_.front());
            queue_.pop_front();
        }
        if (slots_[r.slot].generation.load() != r.generation) continue; // schon wieder ueberholt
        std::vector<int16_t> pcm;
        FilePlayer::DecodeToMemory(r.path, kMaxSeconds, pcm);
        Publish(r.slot, r.generation, std::move(pcm));
    }
}
