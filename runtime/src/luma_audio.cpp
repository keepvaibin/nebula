// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's audio service (contract services-audio; plan 2.6). Layer 1: a mod's cooked sound effects (sound.md 4, the
// NSFX the game's start cooks from `layers/<l>/assets/<path>.sound.json`), read in place from the store and played as
// voices. Layer 2: streams, rings of float frames the mod fills from the game thread. Both are mixed on the audio
// frame's thread (mix, from NativeGameAudio::frame) into the game's output before the master volume, so the player's
// volumes, the background mute and the mono setting are the game's own.
//
// One lock (g_lock) between the game thread (the calls, the deliveries, a mod's end) and the audio thread (mix). The
// audio thread never allocates and never frees: it reads, mixes, marks a voice ended or silent and posts its event
// node (the kernel's queue links nodes and allocates nothing). Items are made and freed on the game thread only.
#include "galaxy/luma_audio_service.h"

#include "galaxy/asset_store.h"

#include <luma_audio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace galaxy::luma_audio {
namespace {

static_assert(sizeof(LumaAudioPlay) == 24u, "LumaAudioPlay 1.0 is 24 bytes");
static_assert(sizeof(LumaAudioStreamDesc) == 24u, "LumaAudioStreamDesc 1.0 is 24 bytes");

constexpr std::uint32_t kNsfxRate = 48000u;
constexpr std::uint32_t kNsfxHeader = 64u;
constexpr std::uint32_t kNoLoop = 0xFFFFFFFFu;
constexpr float kPitchLeast = 0.25f;
constexpr float kPitchMost = 4.0f;
constexpr std::uint32_t kRampMs = 10u;  // every volume change and every stop: nothing clicks

// audio's own rows (the audio range is 350-399), beside the registry's (100-104).
constexpr std::uint32_t kRowNotASound = 350u;
constexpr std::uint32_t kRowDropped = 351u;
constexpr std::uint32_t kRowNotANumber = 352u;

struct ModState {
    const LumaMod* mod = nullptr;
    float bus[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // g_lock: the mod's own share of each bus (bus_volume)
};

struct Item;

struct Node {
    LumaEventNode event;  // first, and both plain members: the kernel's node is the Node
    Item* item;
};

// A voice (a cooked sound playing) or a stream. Every member is g_lock's but `node`'s kernel half.
struct Item {
    Node node{};
    ModState* owner = nullptr;  // null once the mod is gone
    LumaHandle handle = 0;
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    bool stream = false;
    bool posted = false;  // the node is queued with the kernel
    bool closed = false;  // the handle is closed (stop, close, the last event given): no event from now on
    bool silent = false;  // the mix is done with it: freed once neither queued nor being delivered
    std::uint32_t bus = 0;
    float volume = 1.0f;  // the mod's: the sound's times play's (or voice_set's), or the stream's
    // The audio thread's: the gain heard now, ramped to its target over kRampMs.
    float gain = 0.0f;
    bool gain_started = false;
    // Stopping: fading over fade_ms (fade_total and fade_left set by the mix at its rate).
    bool fading = false;
    std::uint32_t fade_ms = 0;
    std::uint32_t fade_total = 0;
    std::uint32_t fade_left = 0;
    // A voice's.
    std::string name;  // as play was given it (Audio.Dropped)
    const std::uint8_t* samples = nullptr;  // i16, interleaved, in the store's mapping
    std::uint32_t channels = 1;
    std::uint32_t frames = 0;
    bool looping = false;
    std::uint32_t loop_start = 0;
    std::uint32_t loop_end = 0;  // not played
    double place = 0.0;          // in the sound's 48 kHz frames
    float sound_volume = 1.0f;  // the sound's own (voice_set's volume is times this)
    float pitch_pick = 1.0f;    // this play's pick in the sound's pitch range (voice_set's pitch is times this)
    float pitch = 1.0f;         // the pick times play's (or voice_set's)
    std::uint8_t priority = 128;
    std::uint64_t order = 0;     // play's count: the oldest of equal priority drops first
    bool ended = false;          // ENDED to give
    std::int32_t ended_status = LUMA_OK;
    // A stream's.
    std::uint32_t rate = 48000u;
    std::vector<float> ring;     // capacity frames × channels
    std::size_t capacity = 0;    // frames
    std::size_t read = 0;        // frame index of the oldest
    std::size_t count = 0;       // frames held
    std::size_t need_below = 0;  // frames: under this, NEED
    float previous[2] = {0.0f, 0.0f};
    double fraction = 0.0;       // between `previous` and the oldest held
    bool need = false;           // NEED to give
    bool need_armed = false;     // a push since the last NEED
    bool draining = false;       // closed with drain: silent once empty
};

std::mutex g_lock;
std::vector<Item*> g_items;                     // g_lock
std::vector<std::unique_ptr<ModState>> g_mods;  // game thread
const Item* g_delivering = nullptr;             // g_lock: the item whose deliver runs
std::atomic<std::size_t> g_live{0};             // g_items' size: the mix's test with no lock
std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
std::atomic<const assets::AssetStore*> g_store{nullptr};
std::atomic<LumaHandle> g_next_handle{1};
std::uint64_t g_order = 0;                // game thread
std::uint64_t g_random = 0x9E3779B97F4A7C15ull;  // game thread: a play's pick in its pitch range

void report(const LumaMod* mod, const char* where, std::uint32_t code, const char* a = nullptr) noexcept {
    const char* args[1] = {a};
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, code, args, a == nullptr ? 0u : 1u);
    }
}

std::int32_t enter(const LumaMod* mod, const char* where) noexcept {
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel == nullptr) {
        return LUMA_E_CANCELLED;
    }
    if (mod == nullptr || kernel->mod_id(mod) == nullptr) {
        kernel->unknown_mod(where);
        return LUMA_E_ARGUMENT;
    }
    if (kernel->mod_live(mod) == 0) {
        return LUMA_E_CANCELLED;
    }
    if (kernel->game_thread() == 0) {
        report(mod, where, 100u);  // Service.Thread
        return LUMA_E_THREAD;
    }
    return LUMA_OK;
}

std::int32_t argument(const LumaMod* mod, const char* where, const char* name) noexcept {
    report(mod, where, 101u, name);  // Service.Argument
    return LUMA_E_ARGUMENT;
}

std::int32_t bad_handle(const LumaMod* mod, const char* where) noexcept {
    report(mod, where, 102u);  // Service.Handle
    return LUMA_E_HANDLE;
}

std::int32_t struct_size(const LumaMod* mod, const char* where, const char* name, std::uint32_t found,
                         std::uint32_t expected) noexcept {
    const std::string a = std::to_string(found);
    const std::string b = std::to_string(expected);
    const char* args[3] = {name, a.c_str(), b.c_str()};
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire)) {
        kernel->report(mod, where, 103u, args, 3u);  // Service.StructSize
    }
    return LUMA_E_ARGUMENT;
}

ModState* state_of(const LumaMod* mod) noexcept {
    for (const std::unique_ptr<ModState>& state : g_mods) {
        if (state->mod == mod) {
            return state.get();
        }
    }
    return nullptr;
}

[[nodiscard]] bool finite_in(float value, float least, float most) noexcept {
    return std::isfinite(value) && value >= least && value <= most;
}

// With g_lock: queue the item's node for its mod's next frame (once). Any thread.
void post_locked(Item* item) noexcept {
    if (item->posted || item->owner == nullptr) {
        return;
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (kernel != nullptr && kernel->post(&item->node.event) == LUMA_OK) {
        item->posted = true;
    }
}

// Game thread, with g_lock: frees what neither the mix, the kernel's queue nor a delivery holds.
void reap_locked() {
    for (auto it = g_items.begin(); it != g_items.end();) {
        Item* item = *it;
        if (item->silent && !item->posted && item != g_delivering && (item->closed || item->owner == nullptr)) {
            it = g_items.erase(it);
            delete item;
        } else {
            ++it;
        }
    }
    g_live.store(g_items.size(), std::memory_order_release);
}

// With g_lock: the mod's open item of that handle and kind, or null. A voice that ended this frame, its ENDED not yet
// given, is still open: voice_set changes nothing, voice_stop closes it and its ENDED is not given.
Item* find_locked(const ModState* state, LumaHandle handle, bool stream) noexcept {
    for (Item* item : g_items) {
        if (item->owner == state && item->handle == handle && item->stream == stream && !item->closed) {
            return item;
        }
    }
    return nullptr;
}

// With g_lock: an item ends its sound (a voice's natural end, or a fade's): the mix is done with it; a voice with a
// function and no stop gets its ENDED.
void finish_locked(Item* item) noexcept {
    item->silent = true;
    if (!item->stream && item->ended && !item->closed) {
        if (item->fn != nullptr) {
            post_locked(item);
        } else {
            item->closed = true;
        }
    }
}

// ---- The mix (the audio frame's thread, with g_lock) ---------------------------------------------------------------

// The gain at this frame: toward `target` in kRampMs, times the fade's share; false when the fade has ended.
struct Ramp {
    float step = 0.0f;
    float target = 0.0f;
};

Ramp ramp_for(Item* item, float target, std::uint32_t rate) noexcept {
    if (!item->gain_started) {
        item->gain = target;
        item->gain_started = true;
    }
    if (item->fading && item->fade_total == 0u) {
        const std::uint64_t total = static_cast<std::uint64_t>(rate) * item->fade_ms / 1000u;
        item->fade_total = static_cast<std::uint32_t>(std::max<std::uint64_t>(total, 1u));
        item->fade_left = item->fade_total;
    }
    const float ramp = static_cast<float>(std::max<std::uint32_t>(rate * kRampMs / 1000u, 1u));
    return {std::fabs(target - item->gain) / ramp, target};
}

// One frame's gain, the ramp and the fade moved on; negative: the fade is over (silence from here).
float next_gain(Item* item, const Ramp& ramp) noexcept {
    if (item->gain < ramp.target) {
        item->gain = std::min(item->gain + ramp.step, ramp.target);
    } else if (item->gain > ramp.target) {
        item->gain = std::max(item->gain - ramp.step, ramp.target);
    }
    if (!item->fading) {
        return item->gain;
    }
    if (item->fade_left == 0u) {
        return -1.0f;
    }
    const float share = static_cast<float>(item->fade_left) / static_cast<float>(item->fade_total);
    item->fade_left--;
    return item->gain * share;
}

[[nodiscard]] float sample_at(const Item* item, std::uint32_t frame, std::uint32_t channel) noexcept {
    std::int16_t value = 0;
    std::memcpy(&value, item->samples + (static_cast<std::size_t>(frame) * item->channels + channel) * 2u, 2u);
    return static_cast<float>(value) * (1.0f / 32768.0f);
}

bool mix_voice(Item* item, float* stereo, std::size_t frames, std::uint32_t rate, float target) noexcept {
    const Ramp ramp = ramp_for(item, target, rate);
    const double step = static_cast<double>(kNsfxRate) / static_cast<double>(rate) * static_cast<double>(item->pitch);
    const std::uint32_t right = item->channels == 2u ? 1u : 0u;
    for (std::size_t i = 0; i < frames; ++i) {
        if (item->looping) {
            const double length = static_cast<double>(item->loop_end - item->loop_start);
            while (item->place >= static_cast<double>(item->loop_end)) {
                item->place -= length;
            }
        } else if (item->place >= static_cast<double>(item->frames)) {
            item->ended = true;
            item->ended_status = LUMA_OK;
            finish_locked(item);
            return true;
        }
        const float gain = next_gain(item, ramp);
        if (gain < 0.0f) {
            finish_locked(item);
            return true;
        }
        const auto at = static_cast<std::uint32_t>(item->place);
        const float t = static_cast<float>(item->place - static_cast<double>(at));
        std::uint32_t next = at + 1u;
        bool past = false;
        if (item->looping && next >= item->loop_end) {
            next = item->loop_start;
        } else if (next >= item->frames) {
            past = true;  // the sound's last frame toward silence
        }
        const float l0 = sample_at(item, at, 0u);
        const float r0 = sample_at(item, at, right);
        const float l1 = past ? 0.0f : sample_at(item, next, 0u);
        const float r1 = past ? 0.0f : sample_at(item, next, right);
        stereo[i * 2u] += (l0 + (l1 - l0) * t) * gain;
        stereo[i * 2u + 1u] += (r0 + (r1 - r0) * t) * gain;
        item->place += step;
    }
    return true;
}

bool mix_stream(Item* item, float* stereo, std::size_t frames, std::uint32_t rate, float target) noexcept {
    const Ramp ramp = ramp_for(item, target, rate);
    const double step = static_cast<double>(item->rate) / static_cast<double>(rate);
    const std::size_t channels = item->channels;
    const std::size_t right = channels == 2u ? 1u : 0u;
    for (std::size_t i = 0; i < frames; ++i) {
        const float gain = next_gain(item, ramp);
        if (gain < 0.0f) {
            finish_locked(item);
            return true;
        }
        if (item->count == 0u) {
            // Dry: silence, and the next push starts from silence (no step from the last frame heard).
            item->previous[0] = 0.0f;
            item->previous[1] = 0.0f;
            item->fraction = 0.0;
            if (item->draining) {
                finish_locked(item);
                return true;
            }
            continue;
        }
        const float* oldest = item->ring.data() + item->read * channels;
        const auto t = static_cast<float>(item->fraction);
        stereo[i * 2u] += (item->previous[0] + (oldest[0] - item->previous[0]) * t) * gain;
        stereo[i * 2u + 1u] += (item->previous[1] + (oldest[right] - item->previous[1]) * t) * gain;
        item->fraction += step;
        while (item->fraction >= 1.0 && item->count > 0u) {
            const float* gone = item->ring.data() + item->read * channels;
            item->previous[0] = gone[0];
            item->previous[1] = gone[right];
            item->read = (item->read + 1u) % item->capacity;
            item->count--;
            item->fraction -= 1.0;
        }
    }
    if (item->need_armed && !item->closed && item->count < item->need_below && item->fn != nullptr) {
        item->need_armed = false;
        item->need = true;
        post_locked(item);
    }
    return true;
}

// ---- Deliveries (game thread) --------------------------------------------------------------------------------------

void deliver(LumaEventNode* node) {
    Item* const item = reinterpret_cast<Node*>(node)->item;
    LumaEvent event{};
    event.size = sizeof(event);
    LumaEventFn fn = nullptr;
    void* user = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        item->posted = false;
        if (item->owner == nullptr || item->closed) {
            reap_locked();
            return;
        }
        if (!item->stream && item->ended) {
            event.kind = LUMA_AUDIO_EVENT_ENDED;
            event.status = item->ended_status;
            item->closed = true;  // its last event: the handle is closed after it
        } else if (item->stream && item->need) {
            item->need = false;
            event.kind = LUMA_AUDIO_EVENT_NEED;
            event.status = LUMA_OK;
            event.value = item->capacity - item->count;
        } else {
            return;
        }
        event.handle = item->handle;
        fn = item->fn;
        user = item->user;
        g_delivering = item;
    }
    if (const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire); kernel != nullptr && fn != nullptr) {
        kernel->call(node->mod, LUMA_AUDIO_NAME, fn, user, &event);
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    g_delivering = nullptr;
    reap_locked();
}

void drop(LumaEventNode* node) {
    Item* const item = reinterpret_cast<Node*>(node)->item;
    const std::lock_guard<std::mutex> lock(g_lock);
    item->posted = false;
    reap_locked();
}

// ---- The calls (game thread) ---------------------------------------------------------------------------------------

// Begins every call: the mod checked, its state found. LUMA_OK and *state.
std::int32_t begin(const LumaMod* mod, const char* where, ModState** state) noexcept {
    if (const std::int32_t status = enter(mod, where); status != LUMA_OK) {
        return status;
    }
    *state = state_of(mod);
    return *state != nullptr ? LUMA_OK : LUMA_E_CANCELLED;
}

// A cooked sound as sound.md 4 reads it.
struct Sound {
    const std::uint8_t* samples = nullptr;
    std::uint32_t channels = 0;
    std::uint32_t frames = 0;
    std::uint32_t loop_start = kNoLoop;
    std::uint32_t loop_end = 0;
    float volume = 1.0f;
    float pitch_low = 1.0f;
    float pitch_high = 1.0f;
    std::uint8_t priority = 128;
    std::uint8_t bus = 0;
};

template <typename T>
T field(const std::uint8_t* bytes, std::size_t at) noexcept {
    T value{};
    std::memcpy(&value, bytes + at, sizeof(T));
    return value;
}

// False: not a cooked sound (magic, version, rate, channels, length, loop points, volume, pitch or bus wrong).
bool read_sound(const std::uint8_t* bytes, std::uint64_t length, Sound& sound) noexcept {
    if (length < kNsfxHeader || std::memcmp(bytes, "NSFX", 4u) != 0 || field<std::uint32_t>(bytes, 4u) != 1u ||
        field<std::uint32_t>(bytes, 8u) != kNsfxRate) {
        return false;
    }
    sound.channels = field<std::uint32_t>(bytes, 12u);
    sound.frames = field<std::uint32_t>(bytes, 16u);
    sound.loop_start = field<std::uint32_t>(bytes, 20u);
    sound.loop_end = field<std::uint32_t>(bytes, 24u);
    sound.volume = field<float>(bytes, 28u);
    sound.pitch_low = field<float>(bytes, 32u);
    sound.pitch_high = field<float>(bytes, 36u);
    sound.priority = bytes[48];
    sound.bus = bytes[49];
    if (sound.channels != 1u && sound.channels != 2u) {
        return false;
    }
    if (kNsfxHeader + 2ull * sound.frames * sound.channels > length) {
        return false;
    }
    if (sound.loop_start != kNoLoop) {
        const std::uint32_t end = sound.loop_end == 0u ? sound.frames : sound.loop_end;
        if (sound.loop_start >= end || end > sound.frames) {
            return false;
        }
        sound.loop_end = end;
    }
    if (!finite_in(sound.volume, 0.0f, LUMA_AUDIO_VOLUME_MOST) ||
        !finite_in(sound.pitch_low, kPitchLeast, kPitchMost) ||
        !finite_in(sound.pitch_high, sound.pitch_low, kPitchMost) || sound.bus > LUMA_AUDIO_BUS_MUSIC) {
        return false;
    }
    sound.samples = bytes + kNsfxHeader;
    return true;
}

// "<path>" (the mod's own) or "<id>:<path>" as the store's Luma/<id>/<path>.nsfx (luma-format 5, sound.md 1).
bool store_path(const char* id, std::string_view given, std::string& path) {
    const std::size_t colon = given.find(':');
    path = "Luma/";
    if (colon != std::string_view::npos && colon != 0u && given.substr(0, colon).find('/') == std::string_view::npos) {
        path.append(given.substr(0, colon)).append("/");
        given = given.substr(colon + 1u);
    } else {
        path.append(id).append("/");
    }
    if (given.empty()) {
        return false;
    }
    path.append(given).append(".nsfx");
    return true;
}

[[nodiscard]] float pick(float low, float high) noexcept {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 7;
    g_random ^= g_random << 17;
    const float unit = static_cast<float>(g_random >> 40) / static_cast<float>(1ull << 24);
    return low + (high - low) * unit;
}

std::int32_t audio_play(const LumaMod* mod, const char* name, const LumaAudioPlay* how, LumaEventFn fn, void* user,
                        LumaHandle* out) noexcept {
    static constexpr const char* where = "audio.play";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (name == nullptr) {
        return argument(mod, where, "name");
    }
    const std::size_t length = ::strnlen(name, LUMA_AUDIO_NAME_MOST + 1u);
    if (length == 0u || length > LUMA_AUDIO_NAME_MOST) {
        return argument(mod, where, "name");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    LumaAudioPlay play{sizeof(LumaAudioPlay), 1.0f, 1.0f, LUMA_AUDIO_BUS_SOUND, -1, 0u};
    if (how != nullptr) {
        if (how->size < sizeof(LumaAudioPlay)) {
            return struct_size(mod, where, "LumaAudioPlay", how->size, sizeof(LumaAudioPlay));
        }
        play = *how;
        if (!finite_in(play.volume, 0.0f, LUMA_AUDIO_VOLUME_MOST)) {
            return argument(mod, where, "how.volume");
        }
        if (!finite_in(play.pitch, kPitchLeast, kPitchMost)) {
            return argument(mod, where, "how.pitch");
        }
        if (play.bus > LUMA_AUDIO_BUS_MUSIC && play.bus != LUMA_AUDIO_BUS_SOUND) {
            return argument(mod, where, "how.bus");
        }
        if (play.loop < -1 || play.loop > 1) {
            return argument(mod, where, "how.loop");
        }
    }
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const assets::AssetStore* store = g_store.load(std::memory_order_acquire);
    try {
        std::string path;
        if (!store_path(kernel->mod_id(mod), std::string_view(name, length), path)) {
            return argument(mod, where, "name");
        }
        const std::optional<assets::Entry> found = store != nullptr ? store->find(path) : std::nullopt;
        if (!found) {
            return LUMA_E_NOT_FOUND;
        }
        Sound sound;
        const std::uint8_t* bytes = nullptr;
        if (!found->folder && found->length >= kNsfxHeader) {
            bytes = store->map(found->position, static_cast<std::size_t>(found->length));
            if (bytes == nullptr) {
                return LUMA_E_IO;
            }
        }
        if (bytes == nullptr || !read_sound(bytes, found->length, sound)) {
            report(mod, where, kRowNotASound, name);  // Audio.NotASound
            return LUMA_E_FORMAT;
        }
        auto item = std::make_unique<Item>();
        item->node.event.mod = mod;
        item->node.event.deliver = deliver;
        item->node.event.drop = drop;
        item->node.item = item.get();
        item->owner = state;
        item->fn = fn;
        item->user = user;
        item->name.assign(name, length);
        item->samples = sound.samples;
        item->channels = sound.channels;
        item->frames = sound.frames;
        item->bus = play.bus == LUMA_AUDIO_BUS_SOUND ? sound.bus : play.bus;
        item->sound_volume = sound.volume;
        item->volume = sound.volume * play.volume;
        item->pitch_pick = pick(sound.pitch_low, sound.pitch_high);
        item->pitch = std::clamp(item->pitch_pick * play.pitch, kPitchLeast, kPitchMost);
        item->priority = sound.priority;
        item->order = ++g_order;
        if (play.loop == 1) {
            item->looping = sound.loop_start != kNoLoop || sound.frames > 0u;
            item->loop_start = sound.loop_start != kNoLoop ? sound.loop_start : 0u;
            item->loop_end = sound.loop_start != kNoLoop ? sound.loop_end : sound.frames;
        } else if (play.loop == -1 && sound.loop_start != kNoLoop) {
            item->looping = true;
            item->loop_start = sound.loop_start;
            item->loop_end = sound.loop_end;
        }
        const std::lock_guard<std::mutex> lock(g_lock);
        // The 33rd: the mod's voice of the lowest priority ends (the new one's included; the oldest of equals).
        Item* lowest = nullptr;
        std::uint32_t playing = 0;
        for (Item* other : g_items) {
            if (other->owner == state && !other->stream && !other->closed && !other->ended) {
                playing++;
                if (lowest == nullptr || other->priority < lowest->priority ||
                    (other->priority == lowest->priority && other->order < lowest->order)) {
                    lowest = other;
                }
            }
        }
        if (playing >= LUMA_AUDIO_VOICES_MOST) {
            if (lowest == nullptr || item->priority < lowest->priority) {
                report(mod, where, kRowDropped, name);  // Audio.Dropped: the new one
                return LUMA_E_LIMIT;
            }
            report(mod, where, kRowDropped, lowest->name.c_str());  // Audio.Dropped
            lowest->ended = true;
            lowest->ended_status = LUMA_E_LIMIT;
            lowest->fading = true;
            lowest->fade_ms = kRampMs;
            if (lowest->fn != nullptr) {
                post_locked(lowest);
            } else {
                lowest->closed = true;
            }
        }
        g_items.reserve(g_items.size() + 1u);
        item->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
        *out = item->handle;
        g_items.push_back(item.release());
        reap_locked();
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

std::int32_t audio_voice_set(const LumaMod* mod, LumaHandle voice, float volume, float pitch) noexcept {
    static constexpr const char* where = "audio.voice_set";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (!finite_in(volume, 0.0f, LUMA_AUDIO_VOLUME_MOST)) {
        return argument(mod, where, "volume");
    }
    if (!finite_in(pitch, kPitchLeast, kPitchMost)) {
        return argument(mod, where, "pitch");
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    Item* item = find_locked(state, voice, false);
    if (item == nullptr) {
        return bad_handle(mod, where);
    }
    // play's volume and pitch replaced: the sound's own volume stays, and this play's pick in its pitch range.
    item->volume = volume * item->sound_volume;
    item->pitch = std::clamp(item->pitch_pick * pitch, kPitchLeast, kPitchMost);
    return LUMA_OK;
}

std::int32_t audio_voice_stop(const LumaMod* mod, LumaHandle voice, std::uint32_t fade_ms) noexcept {
    static constexpr const char* where = "audio.voice_stop";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (fade_ms > LUMA_AUDIO_FADE_MOST_MS) {
        return argument(mod, where, "fade_ms");
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    Item* item = find_locked(state, voice, false);
    if (item == nullptr) {
        return bad_handle(mod, where);
    }
    item->closed = true;  // no event from now on; the fade goes on in the mix
    item->fading = true;
    item->fade_ms = fade_ms == 0u ? kRampMs : fade_ms;
    reap_locked();
    return LUMA_OK;
}

std::int32_t audio_bus_volume(const LumaMod* mod, std::uint32_t bus, float volume) noexcept {
    static constexpr const char* where = "audio.bus_volume";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (bus > LUMA_AUDIO_BUS_MUSIC) {
        return argument(mod, where, "bus");
    }
    if (!finite_in(volume, 0.0f, 1.0f)) {
        return argument(mod, where, "volume");
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    state->bus[bus] = volume;
    return LUMA_OK;
}

std::int32_t audio_stream_open(const LumaMod* mod, const LumaAudioStreamDesc* desc, LumaEventFn fn, void* user,
                               LumaHandle* out) noexcept {
    static constexpr const char* where = "audio.stream_open";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (desc == nullptr) {
        return argument(mod, where, "desc");
    }
    if (desc->size < sizeof(LumaAudioStreamDesc)) {
        return struct_size(mod, where, "LumaAudioStreamDesc", desc->size, sizeof(LumaAudioStreamDesc));
    }
    if (desc->rate < LUMA_AUDIO_RATE_LEAST || desc->rate > LUMA_AUDIO_RATE_MOST) {
        return argument(mod, where, "desc.rate");
    }
    if (desc->channels != 1u && desc->channels != 2u) {
        return argument(mod, where, "desc.channels");
    }
    if (desc->bus > LUMA_AUDIO_BUS_MUSIC) {
        return argument(mod, where, "desc.bus");
    }
    if (!finite_in(desc->volume, 0.0f, LUMA_AUDIO_VOLUME_MOST)) {
        return argument(mod, where, "desc.volume");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    try {
        {
            const std::lock_guard<std::mutex> lock(g_lock);
            std::uint32_t open = 0;
            for (const Item* other : g_items) {
                if (other->owner == state && other->stream && !other->closed) {
                    open++;
                }
            }
            if (open >= LUMA_AUDIO_STREAMS_MOST) {
                report(mod, where, 104u, "4");  // Service.Limit
                return LUMA_E_LIMIT;
            }
        }
        auto item = std::make_unique<Item>();
        item->node.event.mod = mod;
        item->node.event.deliver = deliver;
        item->node.event.drop = drop;
        item->node.item = item.get();
        item->owner = state;
        item->fn = fn;
        item->user = user;
        item->stream = true;
        item->bus = desc->bus;
        item->volume = desc->volume;
        item->rate = desc->rate;
        item->channels = desc->channels;
        item->capacity = (static_cast<std::size_t>(desc->rate) * LUMA_AUDIO_RING_MS + 999u) / 1000u;
        item->need_below = static_cast<std::size_t>(desc->rate) * LUMA_AUDIO_NEED_MS / 1000u;
        item->ring.assign(item->capacity * desc->channels, 0.0f);  // made here: the mix never allocates
        const std::lock_guard<std::mutex> lock(g_lock);
        g_items.reserve(g_items.size() + 1u);
        item->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
        *out = item->handle;
        Item* const made = item.release();
        g_items.push_back(made);
        if (fn != nullptr) {
            made->need = true;  // empty: NEED at once
            post_locked(made);
        }
        reap_locked();
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

std::int64_t audio_stream_push(const LumaMod* mod, LumaHandle stream, const float* samples,
                               std::uint64_t frames) noexcept {
    static constexpr const char* where = "audio.stream_push";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (samples == nullptr && frames != 0u) {
        return argument(mod, where, "samples");
    }
    std::uint64_t not_numbers = 0;
    std::uint64_t taken = 0;
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        Item* item = find_locked(state, stream, true);
        if (item == nullptr) {
            return bad_handle(mod, where);
        }
        const std::size_t channels = item->channels;
        taken = std::min<std::uint64_t>(frames, item->capacity - item->count);
        std::size_t at = (item->read + item->count) % item->capacity;
        for (std::uint64_t f = 0; f < taken; ++f) {
            float* to = item->ring.data() + at * channels;
            for (std::size_t c = 0; c < channels; ++c) {
                const float value = samples[f * channels + c];
                if (std::isfinite(value)) {
                    to[c] = value;
                } else {
                    to[c] = 0.0f;
                    not_numbers++;
                }
            }
            at = at + 1u == item->capacity ? 0u : at + 1u;
        }
        item->count += static_cast<std::size_t>(taken);
        item->need_armed = true;
    }
    if (not_numbers != 0u) {
        const std::string count = std::to_string(not_numbers);
        report(mod, where, kRowNotANumber, count.c_str());  // Audio.NotANumber
    }
    return static_cast<std::int64_t>(taken);
}

std::int32_t audio_stream_close(const LumaMod* mod, LumaHandle stream, std::uint32_t drain) noexcept {
    static constexpr const char* where = "audio.stream_close";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (drain > 1u) {
        return argument(mod, where, "drain");
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    Item* item = find_locked(state, stream, true);
    if (item == nullptr) {
        return bad_handle(mod, where);
    }
    item->closed = true;  // no event from now on
    item->need = false;
    if (drain == 1u) {
        item->draining = true;
        if (item->count == 0u) {
            item->silent = true;
        }
    } else {
        item->fading = true;
        item->fade_ms = kRampMs;
    }
    reap_locked();
    return LUMA_OK;
}

// ---- The provider --------------------------------------------------------------------------------------------------

void mod_begin(void*, const LumaMod* mod) {
    if (state_of(mod) != nullptr) {
        return;
    }
    try {
        auto state = std::make_unique<ModState>();
        state->mod = mod;
        g_mods.push_back(std::move(state));
    } catch (...) {
        // Memory: every call of this mod answers LUMA_E_CANCELLED (no state).
    }
}

// After the mod's stop: every voice and stream of it is silent the same audio frame; nothing is posted or called for
// it from here on (a node already queued is dropped by the kernel).
void mod_gone(void*, const LumaMod* mod) {
    ModState* state = state_of(mod);
    if (state == nullptr) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    for (Item* item : g_items) {
        if (item->owner == state) {
            item->owner = nullptr;
            item->silent = true;
            item->closed = true;
        }
    }
    reap_locked();
}

void mod_end(void*, const LumaMod* mod) {
    {
        const std::lock_guard<std::mutex> lock(g_lock);
        reap_locked();
    }
    for (auto it = g_mods.begin(); it != g_mods.end(); ++it) {
        if ((*it)->mod == mod) {
            g_mods.erase(it);
            return;
        }
    }
}

void registered(void*, const LumaServiceKernel* kernel) {
    g_kernel.store(kernel, std::memory_order_release);
}

const LumaAudioService kAudio = {
    {sizeof(LumaAudioService), LUMA_AUDIO_MAJOR, LUMA_AUDIO_MINOR},
    audio_play,
    audio_voice_set,
    audio_voice_stop,
    audio_bus_volume,
    audio_stream_open,
    audio_stream_push,
    audio_stream_close,
};

const LumaServiceRow kRows[] = {
    {kRowNotASound, "Audio.NotASound", {"where", "name", nullptr, nullptr}},
    {kRowDropped, "Audio.Dropped", {"where", "name", nullptr, nullptr}},
    {kRowNotANumber, "Audio.NotANumber", {"where", "count", nullptr, nullptr}},
};
const LumaServiceProvider kAudioProvider = {
    sizeof(LumaServiceProvider), LUMA_AUDIO_NAME, &kAudio.base, nullptr, kRows, sizeof(kRows) / sizeof(kRows[0]),
    nullptr, mod_begin, mod_gone, mod_end, registered,
};
const LumaServiceProvider* const kProviders[] = {&kAudioProvider};

}  // namespace

void set_store(const galaxy::assets::AssetStore* store) noexcept {
    g_store.store(store, std::memory_order_release);
}

std::span<const LumaServiceProvider* const> providers(void (*)(const char*)) noexcept {
    return kProviders;
}

bool mix(float* stereo, std::size_t frames, std::uint32_t rate, const float player[4]) noexcept {
    if (g_live.load(std::memory_order_acquire) == 0u || stereo == nullptr || frames == 0u || rate == 0u) {
        return false;
    }
    float heard[4];
    for (std::size_t b = 0; b < 4u; ++b) {
        heard[b] = finite_in(player[b], 0.0f, 4.0f) ? player[b] : (player[b] > 4.0f ? 4.0f : 0.0f);
    }
    bool any = false;
    const std::lock_guard<std::mutex> lock(g_lock);
    for (Item* item : g_items) {
        if (item->owner == nullptr || item->silent) {
            continue;
        }
        const float target = item->volume * item->owner->bus[item->bus] * heard[item->bus];
        any |= item->stream ? mix_stream(item, stereo, frames, rate, target)
                            : mix_voice(item, stereo, frames, rate, target);
    }
    return any;
}

}  // namespace galaxy::luma_audio
