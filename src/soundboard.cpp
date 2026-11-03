
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

void Soundboard::Enqueue(int slot, uint32_t generation, const std::wstring& path)
{
    {
        std::lock_guard<std::mutex> lk(mutex_);
        queue_.push_back({slot, generation, path});
    }
    cv_.notify_one();
}

void Soundboard::Load(int slot, const std::wstring& path)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot& s = slots_[slot];
    const uint32_t gen = s.generation.fetch_add(1) + 1;
    std::atomic_store(&s.clip, std::shared_ptr<const std::vector<int16_t>>());
    bytes_ -= s.size.exchange(0);
    s.seconds       = 0;
    s.playWhenReady = false;
    s.previewWhenReady = false;
    s.rmsDb = s.peakDb = -120.0f;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        s.path = path;
    }
    if (path.empty()) {
        s.state = State::Empty;
        return;
    }
    s.state = State::Loading;
    Enqueue(slot, gen, path);
}

void Soundboard::Register(int slot, const std::wstring& path)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot& s = slots_[slot];
    s.generation.fetch_add(1);
    std::atomic_store(&s.clip, std::shared_ptr<const std::vector<int16_t>>());
    bytes_ -= s.size.exchange(0);
    s.seconds       = 0;
    s.playWhenReady = false;
    s.previewWhenReady = false;
    s.rmsDb = s.peakDb = -120.0f;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        s.path = path;
    }
    s.state = path.empty() ? State::Empty : State::Unloaded;
}

void Soundboard::Clear(int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot& s = slots_[slot];
    s.generation.fetch_add(1);
    std::atomic_store(&s.clip, std::shared_ptr<const std::vector<int16_t>>());
    bytes_ -= s.size.exchange(0);
    s.seconds       = 0;
    s.playWhenReady = false;
    s.previewWhenReady = false;
    s.rmsDb = s.peakDb = -120.0f;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        s.path.clear();
    }
    s.state = State::Empty;
}

void Soundboard::SetVolume(int slot, int pct)
{
    if (slot < 0 || slot >= kSlots) return;
    slots_[slot].volume = std::min(200, std::max(0, pct));
}

void Soundboard::LoadMemory(int slot, std::vector<int16_t> pcm)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot& s = slots_[slot];
    bytes_ -= s.size.exchange(0);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        s.path.clear();
    }
    Publish(slot, s.generation.fetch_add(1) + 1, std::move(pcm));
}

void Soundboard::Publish(int slot, uint32_t generation, std::vector<int16_t>&& pcm)
{
    Slot& s = slots_[slot];
    if (s.generation.load() != generation) return; // inzwischen neu belegt oder geleert
    if (pcm.size() < 2) {
        s.playWhenReady = false;
    s.previewWhenReady = false;
    s.rmsDb = s.peakDb = -120.0f;
        s.state         = State::Failed;
        return;
    }
    const size_t   bytes   = pcm.size() * sizeof(int16_t);
    const double   seconds = static_cast<double>(pcm.size() / 2) / 48000.0;
    const auto     meas    = loudness::Analyze(pcm.data(), pcm.size() / 2);
    s.rmsDb                = meas.valid ? meas.rmsDb : -120.0f;
    s.peakDb               = meas.valid ? meas.peakDb : -120.0f;
    std::atomic_store(&s.clip, std::shared_ptr<const std::vector<int16_t>>(std::make_shared<const std::vector<int16_t>>(std::move(pcm))));
    bytes_ -= s.size.exchange(bytes);
    bytes_ += bytes;
    s.seconds = seconds;
    s.lastUse = ++useCounter_;
    s.state   = State::Ready;
    if (s.playWhenReady.exchange(false)) { // der Nutzer hat schon geklickt, waehrend geladen wurde
        s.trigger.fetch_add(1);
        pending_.fetch_add(1);
    }
    if (s.previewWhenReady.exchange(false)) previewTrigger_ = slot + 1;
    Evict(slot);
}

// Speicherbudget einhalten: die am laengsten nicht gespielten Clips verwerfen (sie werden beim naechsten Klick neu geladen)
void Soundboard::Evict(int keep)
{
    while (bytes_.load() > budget_.load()) {
        int      victim = -1;
        uint64_t oldest = UINT64_MAX;
        for (int i = 0; i < kSlots; ++i) {
            if (i == keep || slots_[i].state.load() != State::Ready || slots_[i].size.load() == 0) continue;
            const uint64_t use = slots_[i].lastUse.load();
            if (use < oldest) {
                oldest = use;
                victim = i;
            }
        }
        if (victim < 0) break; // nichts mehr zu verwerfen
        Slot& v = slots_[victim];
        std::atomic_store(&v.clip, std::shared_ptr<const std::vector<int16_t>>());
        bytes_ -= v.size.exchange(0);
        // ohne Pfad (LoadMemory) kann nichts nachgeladen werden: dann ist der Knopf danach leer
        bool hasPath;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            hasPath = !v.path.empty();
        }
        v.state = hasPath ? State::Unloaded : State::Empty;
    }
}

Soundboard::State Soundboard::GetState(int slot) const { return (slot < 0 || slot >= kSlots) ? State::Empty : slots_[slot].state.load(); }
double            Soundboard::Seconds(int slot) const { return (slot < 0 || slot >= kSlots) ? 0.0 : slots_[slot].seconds.load(); }
double            Soundboard::Position(int slot) const
{
    if (slot < 0 || slot >= kSlots) return -1.0;
    const int64_t p = slots_[slot].playPos.load(std::memory_order_relaxed);
    return p < 0 ? -1.0 : static_cast<double>(p) / 48000.0;
}

void Soundboard::Play(int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot&       s  = slots_[slot];
    const State st = s.state.load();
    if (st == State::Ready) {
        s.lastUse = ++useCounter_;
        s.trigger.fetch_add(1);
        pending_.fetch_add(1);
    } else if (st == State::Unloaded) { // verworfen: neu laden, spielt danach von selbst
        std::wstring path;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            path = s.path;
        }
        if (path.empty()) return;
        s.playWhenReady = true;
        s.state         = State::Loading;
        Enqueue(slot, s.generation.load(), path);
    } else if (st == State::Loading) {
        s.playWhenReady = true;
    }
}

void Soundboard::StopAll()
{
    stopAll_ = true;
    for (auto& s : slots_) { // noch nicht gestartete Ausloeser verwerfen
        s.playWhenReady = false;
        const uint32_t n = s.trigger.exchange(0);
        if (n) pending_.fetch_sub(static_cast<int>(n));
    }
}

bool Soundboard::Mix(int16_t* io, int frames, float gain)
{
    if (stopAll_.exchange(false)) {
        for (auto& v : voices_) {
            if (v.slot >= 0) slots_[v.slot].playPos.store(-1, std::memory_order_relaxed);
            v = Voice();
        }
        active_ = 0;
    }
    // Neue Ausloeser in Stimmen umsetzen (die Schleife nur, wenn ueberhaupt etwas ansteht)
    if (pending_.load(std::memory_order_relaxed) > 0) {
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
                if (target->slot >= 0 && target->slot != i) slots_[target->slot].playPos.store(-1, std::memory_order_relaxed);
            }
            target->clip = std::move(clip);
            target->pos  = 0;
            target->slot = i;
            target->mul  = static_cast<float>(slots_[i].volume.load()) / 100.0f * NormMul(slots_[i]);
            slots_[i].playPos.store(0, std::memory_order_relaxed);
        }
    }

    bool mixed = false;
    int  live  = 0;
    for (auto& v : voices_) {
        if (!v.clip) continue;
        const size_t total = v.clip->size() / 2;
        const size_t n     = std::min<size_t>(static_cast<size_t>(frames), total - std::min(total, v.pos));
        const int16_t* src = v.clip->data() + v.pos * 2;
        const float    g   = gain * v.mul;
        for (size_t k = 0; k < n * 2; ++k) {
            const int sum = io[k] + static_cast<int>(src[k] * g);
            io[k]         = static_cast<int16_t>(std::clamp(sum, -32768, 32767));
        }
        mixed |= n > 0;
        v.pos += n;
        if (v.pos >= total) {
            slots_[v.slot].playPos.store(-1, std::memory_order_relaxed);
            v = Voice();
        } else {
            slots_[v.slot].playPos.store(static_cast<int64_t>(v.pos), std::memory_order_relaxed);
            ++live;
        }
    }
    active_ = live;
    return mixed;
}

float Soundboard::NormMul(const Slot& s) const
{
    if (!normOn_.load(std::memory_order_relaxed)) return 1.0f;
    loudness::Measure m;
    m.rmsDb  = s.rmsDb.load(std::memory_order_relaxed);
    m.peakDb = s.peakDb.load(std::memory_order_relaxed);
    m.valid  = m.rmsDb > -119.0f;
    return loudness::DbToLinear(loudness::GainDb(m, normTarget_.load(std::memory_order_relaxed)));
}

float Soundboard::Loudness(int slot) const { return (slot < 0 || slot >= kSlots) ? -120.0f : slots_[slot].rmsDb.load(); }

void Soundboard::Preview(int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    Slot&       s  = slots_[slot];
    const State st = s.state.load();
    if (st == State::Ready) {
        s.lastUse       = ++useCounter_;
        previewTrigger_ = slot + 1;
    } else if (st == State::Unloaded) {
        std::wstring path;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            path = s.path;
        }
        if (path.empty()) return;
        s.previewWhenReady = true;
        s.state            = State::Loading;
        Enqueue(slot, s.generation.load(), path);
    } else if (st == State::Loading) {
        s.previewWhenReady = true;
    }
}

bool Soundboard::MixPreview(int16_t* io, int frames, float gain)
{
    if (previewStop_.exchange(false)) {
        preview_ = Voice();
        previewTrigger_ = 0;
        previewLive_    = false;
    }
    if (const int t = previewTrigger_.exchange(0); t > 0) {
        auto clip = std::atomic_load(&slots_[t - 1].clip);
        if (clip) {
            preview_.clip = std::move(clip);
            preview_.pos  = 0;
            preview_.slot = t - 1;
            preview_.mul  = static_cast<float>(slots_[t - 1].volume.load()) / 100.0f * NormMul(slots_[t - 1]);
        }
    }
    if (!preview_.clip) {
        previewLive_ = false;
        return false;
    }
    const size_t total = preview_.clip->size() / 2;
    const size_t n     = std::min<size_t>(static_cast<size_t>(frames), total - std::min(total, preview_.pos));
    const int16_t* src = preview_.clip->data() + preview_.pos * 2;
    const float    g   = gain * preview_.mul;
    for (size_t k = 0; k < n * 2; ++k) io[k] = static_cast<int16_t>(std::clamp(io[k] + static_cast<int>(src[k] * g), -32768, 32767));
    preview_.pos += n;
    const bool done = preview_.pos >= total;
    if (done) preview_ = Voice();
    previewLive_ = !done;
    return n > 0;
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
