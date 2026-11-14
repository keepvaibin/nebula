// SPDX-License-Identifier: GPL-3.0-only
// LumaCore's audio service (docs/modding/services/audio.md). Layer 1: a mod's cooked sound effects (sound.md 4, the
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
#include "galaxy/mod_log.h"
#include "galaxy/mods.h"

#include <luma_audio.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
// Content.MissingSound (references 6): a name no store file has, answered with the placeholder.
constexpr std::uint32_t kMissingSoundCode = 624u;
// Nebula's placeholder sound (references 4, nebula:placeholder/sound): a 120 ms soft two-note chime the importer
// cooks into every store from data/placeholder/sound.wav; the start check (3.13) turns the mods off when a store lacks
// it, so a mod's sound can never be silently missing.
constexpr const char* kPlaceholderSound = "NebulaData/PlaceholderSound.nsfx";

struct Item;

struct ModState {
    const LumaMod* mod = nullptr;
    float bus[4] = {1.0f, 1.0f, 1.0f, 1.0f};  // g_lock: the mod's own share of each bus (bus_volume)
    Item* placer = nullptr;                    // g_lock: its placer, made when it first places a sound
};

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
    bool music = false;   // a voice that is the game's music (music): one at a time, not among the 32
    bool tap = false;     // a tap (tap_open): not mixed, fed
    bool placer = false;  // the mod's placer (one a mod, never mixed): its node comes back each frame while needed
    bool posted = false;  // the node is queued with the kernel
    bool closed = false;  // the handle is closed (stop, close, the last event given): no event from now on
    bool silent = false;  // the mix is done with it: freed once neither queued nor being delivered
    std::uint32_t bus = 0;
    float volume = 1.0f;  // the mod's: the sound's times play's (or voice_set's), or the stream's
    // The audio thread's: the gain heard now, ramped to its target over ramp_ms (linear: a step set when the target
    // changes).
    float gain = 0.0f;
    bool gain_started = false;
    std::uint32_t ramp_ms = kRampMs;
    float ramp_target = -1.0f;
    float ramp_step = 0.0f;
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
    // A voice heard from a place (play_at, voice_place): the gains of left and right, aimed on the game thread
    // (pan_target, g_lock) and reached by the mix over one block (pan, the audio thread's).
    bool placed = false;
    std::uint32_t follow = LUMA_AUDIO_AT_POINT;
    float offset[3] = {0.0f, 0.0f, 0.0f};
    float near_units = 300.0f;
    float far_units = 3000.0f;
    float pan_target[2] = {1.0f, 1.0f};
    float pan[2] = {1.0f, 1.0f};
    std::uint8_t priority = 128;
    std::uint64_t order = 0;     // play's count: the oldest of equal priority drops first
    bool ended = false;          // ENDED to give
    std::int32_t ended_status = LUMA_OK;
    // A replacement of a game sound that the game renews each frame (sound.md 6): what it is renewed by (the place
    // pointer the game gives, 0 for none) and the replace frame it was last renewed at.
    bool level = false;
    std::uintptr_t level_key = 0;
    std::uint64_t renewed = 0;
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
    // A tap's: the block the mix fills, and up to kTapBlocks finished ones waiting for the game thread.
    std::uint32_t source = 0;
    std::vector<float> filling;  // LUMA_AUDIO_TAP_FRAMES × 2
    std::size_t filled = 0;      // frames
    std::vector<float> blocks;   // kTapBlocks × LUMA_AUDIO_TAP_FRAMES × 2
    std::size_t ready_first = 0;
    std::size_t ready_count = 0;
    bool lost = false;           // a block found no room
    std::uint32_t tap_rate = 0;
    std::vector<float> handing;  // the game thread's copy given in SAMPLES
};

constexpr std::size_t kTapBlocks = 8u;  // 170 ms at 48 kHz: a frame of the game's late is not a loss

std::mutex g_lock;
std::vector<Item*> g_items;                     // g_lock
std::vector<std::unique_ptr<ModState>> g_mods;  // game thread
const Item* g_delivering = nullptr;             // g_lock: the item whose deliver runs
std::atomic<std::size_t> g_live{0};             // g_items' size: the mix's test with no lock
std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
std::atomic<const assets::AssetStore*> g_store{nullptr};
std::atomic<LumaHandle> g_next_handle{1};
std::atomic<std::size_t> g_taps{0};             // taps of the output: the frame's test with no lock
// The game's own music: its share (1 heard, 0 replaced), ramped by the mix to g_music_target over g_music_ms; read by
// the audio frame for the music volume it gives the game's mixer.
std::atomic<float> g_music_share{1.0f};
std::atomic<float> g_music_target{1.0f};
std::atomic<std::uint32_t> g_music_ms{kRampMs};
float g_music_step = 0.0f;  // the audio thread's
float g_music_step_target = -1.0f;
std::uint64_t g_order = 0;                // game thread
std::unordered_set<std::string> g_placeholders;  // game thread: each mod and name a placeholder fact was said for

// The game module's half (LumaGameSound.cpp), found by set_game before the first mod; null in a host without it.
struct Game {
    std::int32_t (*play)(const void*, const char*, std::uint32_t, const float*, std::uint64_t*) = nullptr;
    std::int32_t (*place)(const void*, std::uint64_t, std::uint32_t, const float*) = nullptr;
    std::int32_t (*stop)(const void*, std::uint64_t, std::uint32_t) = nullptr;
    void (*gone)(const void*) = nullptr;
    std::uint32_t (*follow)() = nullptr;
    std::uint32_t (*listener)(float*) = nullptr;
};
Game g_game;  // game thread
constexpr std::uint32_t kNoPlace = 0xFFFFFFFFu;  // LumaGameSound.cpp's follow for a sound heard everywhere
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
Item* find_locked(const ModState* state, LumaHandle handle, bool stream, bool tap = false) noexcept {
    for (Item* item : g_items) {
        if (item->owner == state && item->handle == handle && item->stream == stream && item->tap == tap &&
            !item->music && !item->placer && !item->closed) {
            return item;
        }
    }
    return nullptr;
}

// With g_lock: an item ends its sound (a voice's natural end, or a fade's): the mix is done with it; a voice with a
// function and no stop gets its ENDED.
void finish_locked(Item* item) noexcept {
    item->silent = true;
    if (!item->stream && !item->music && !item->tap && item->ended && !item->closed) {
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
        item->gain = item->music ? 0.0f : target;  // music fades in over its ramp
        item->gain_started = true;
    }
    if (item->fading && item->fade_total == 0u) {
        const std::uint64_t total = static_cast<std::uint64_t>(rate) * item->fade_ms / 1000u;
        item->fade_total = static_cast<std::uint32_t>(std::max<std::uint64_t>(total, 1u));
        item->fade_left = item->fade_total;
    }
    if (target != item->ramp_target) {
        const std::uint64_t samples = static_cast<std::uint64_t>(rate) * item->ramp_ms / 1000u;
        const float ramp = static_cast<float>(std::max<std::uint64_t>(samples, 1u));
        item->ramp_target = target;
        item->ramp_step = std::fabs(target - item->gain) / ramp;
    }
    return {item->ramp_step, target};
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
    // The place's gains, from the last block's to the game thread's latest over this block.
    const float pan_l = item->pan[0];
    const float pan_r = item->pan[1];
    const float step_l = (item->pan_target[0] - pan_l) / static_cast<float>(frames);
    const float step_r = (item->pan_target[1] - pan_r) / static_cast<float>(frames);
    item->pan[0] = item->pan_target[0];
    item->pan[1] = item->pan_target[1];
    const bool placed = item->placed;
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
        float left = l0 + (l1 - l0) * t;
        float right_sample = r0 + (r1 - r0) * t;
        if (placed) {  // a placed sound is one point: its channels together, then aimed
            left = (left + right_sample) * 0.5f;
            right_sample = left;
        }
        const float moved = static_cast<float>(i + 1u);
        stereo[i * 2u] += left * gain * (pan_l + step_l * moved);
        stereo[i * 2u + 1u] += right_sample * gain * (pan_r + step_r * moved);
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

// With g_lock: `frames` stereo frames into a tap's block; each full block waits for the game thread (posted), or is
// lost when kTapBlocks wait already (the next SAMPLES says so).
void feed_tap(Item* item, const float* stereo, std::size_t frames, std::uint32_t rate) noexcept {
    item->tap_rate = rate;
    std::size_t at = 0;
    while (at < frames) {
        const std::size_t take = std::min(frames - at, LUMA_AUDIO_TAP_FRAMES - item->filled);
        std::copy(stereo + at * 2u, stereo + (at + take) * 2u, item->filling.begin() + item->filled * 2u);
        item->filled += take;
        at += take;
        if (item->filled == LUMA_AUDIO_TAP_FRAMES) {
            item->filled = 0;
            if (item->ready_count == kTapBlocks) {
                item->lost = true;
                continue;
            }
            const std::size_t slot = (item->ready_first + item->ready_count) % kTapBlocks;
            std::copy(item->filling.begin(), item->filling.end(),
                      item->blocks.begin() + static_cast<std::ptrdiff_t>(slot * LUMA_AUDIO_TAP_FRAMES * 2u));
            item->ready_count++;
            post_locked(item);
        }
    }
}

// The game's music share one frame on: toward g_music_target over g_music_ms (audio thread).
void move_music_share(std::size_t frames, std::uint32_t rate) noexcept {
    const float target = g_music_target.load(std::memory_order_relaxed);
    float share = g_music_share.load(std::memory_order_relaxed);
    if (share == target) {
        return;
    }
    if (target != g_music_step_target) {
        const std::uint64_t samples =
            static_cast<std::uint64_t>(rate) * g_music_ms.load(std::memory_order_relaxed) / 1000u;
        g_music_step = std::fabs(target - share) / static_cast<float>(std::max<std::uint64_t>(samples, 1u));
        g_music_step_target = target;
    }
    const float move = g_music_step * static_cast<float>(frames);
    share = share < target ? std::min(share + move, target) : std::max(share - move, target);
    g_music_share.store(share, std::memory_order_relaxed);
}

// ---- Deliveries (game thread) --------------------------------------------------------------------------------------

void place_frame(Item* placer) noexcept;

void deliver(LumaEventNode* node) {
    Item* const item = reinterpret_cast<Node*>(node)->item;
    if (item->placer) {  // set at its making, never changed
        place_frame(item);
        return;
    }
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
        if (item->tap) {
            g_delivering = item;
            fn = item->fn;
            user = item->user;
        } else if (!item->stream && item->ended) {
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
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    if (item->tap) {
        // Every block waiting, each in its own SAMPLES, copied out under the lock (the mix goes on filling).
        for (;;) {
            {
                const std::lock_guard<std::mutex> lock(g_lock);
                if (item->closed || item->owner == nullptr || item->ready_count == 0u) {
                    break;
                }
                const float* from = item->blocks.data() + item->ready_first * LUMA_AUDIO_TAP_FRAMES * 2u;
                std::copy(from, from + LUMA_AUDIO_TAP_FRAMES * 2u, item->handing.begin());
                item->ready_first = (item->ready_first + 1u) % kTapBlocks;
                item->ready_count--;
                event = LumaEvent{};
                event.size = sizeof(event);
                event.kind = LUMA_AUDIO_EVENT_SAMPLES;
                event.status = std::exchange(item->lost, false) ? LUMA_E_BUSY : LUMA_OK;
                event.handle = item->handle;
                event.data = item->handing.data();
                event.data_size = LUMA_AUDIO_TAP_FRAMES * 2u * sizeof(float);
                event.value = LUMA_AUDIO_TAP_FRAMES;
                event.flags = item->tap_rate;
            }
            if (kernel == nullptr || kernel->call(node->mod, LUMA_AUDIO_NAME, fn, user, &event) != LUMA_OK) {
                break;
            }
        }
    } else if (kernel != nullptr && fn != nullptr) {
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
    float near_units = 300.0f;  // sound.md 3's distance; a file of before it (0, 0) hears the default
    float far_units = 3000.0f;
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
    const float near_units = field<float>(bytes, 40u);
    const float far_units = field<float>(bytes, 44u);
    if (near_units != 0.0f || far_units != 0.0f) {
        if (!finite_in(near_units, 0.0f, 1.0e6f) || !finite_in(far_units, 0.0f, 1.0e6f) || near_units >= far_units) {
            return false;
        }
        sound.near_units = near_units;
        sound.far_units = far_units;
    }
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

// A sound name the store has no file for: the placeholder's file (nebula:placeholder/sound, references 4 and 5.1),
// said in the mod log as Content.MissingSound (one CONTENT Error a name, counted on every call) and, the first time
// the mod names it, as the harness's placeholder fact. None when the store lacks the placeholder too: a store older
// than it, which the start check (3.13) does not let mods run on.
std::optional<assets::Entry> placeholder_sound(const assets::AssetStore* store, const char* id, std::string_view name) {
    std::optional<assets::Entry> found = store != nullptr ? store->find(kPlaceholderSound) : std::nullopt;
    if (!found) {
        return std::nullopt;
    }
    const std::string mod_id = id != nullptr ? id : "";
    const std::string given(name);
    std::uint32_t hash = 2166136261u;  // FNV-1a: one entry a name
    for (const char c : given) {
        hash = (hash ^ static_cast<unsigned char>(c)) * 16777619u;
    }
    modlog::add_row(4u /* LUMA_MODLOG_CONTENT */, 0u /* LUMA_MODLOG_ERROR */, mod_id, kMissingSoundCode, "",
                    "Content.MissingSound", {{"found", given}}, hash);
    if (modlog::facts_wanted() && g_placeholders.insert(mod_id + '\x1f' + given).second) {
        std::string json = "{\"mod\": ";
        modlog::append_json_text(json, mod_id);
        json += ", \"kind\": \"sound\", \"found\": ";
        modlog::append_json_text(json, given);
        json += ", \"file\": \"\", \"line\": 0}";
        modlog::fact("placeholder", json);
    }
    return found;
}

[[nodiscard]] float pick(float low, float high) noexcept {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 7;
    g_random ^= g_random << 17;
    const float unit = static_cast<float>(g_random >> 40) / static_cast<float>(1ull << 24);
    return low + (high - low) * unit;
}

// ---- Places (game thread) -----------------------------------------------------------------------------------------

std::int32_t check_place(const LumaMod* mod, const char* where, const LumaAudioPlace* at) noexcept {
    if (at->size < sizeof(LumaAudioPlace)) {
        return struct_size(mod, where, "LumaAudioPlace", at->size, sizeof(LumaAudioPlace));
    }
    if (at->follow > LUMA_AUDIO_AT_CAMERA) {
        return argument(mod, where, "at.follow");
    }
    for (const float value : at->pos) {
        if (!finite_in(value, -LUMA_AUDIO_PLACE_MOST, LUMA_AUDIO_PLACE_MOST)) {
            return argument(mod, where, "at.pos");
        }
    }
    return LUMA_OK;
}

// Where the camera and the player are now (LumaGameSound.cpp luma_game_listener).
struct Listener {
    bool camera = false;
    bool player = false;
    float v[12] = {};  // camera place, front, up; the player's place
};

Listener listen() noexcept {
    Listener heard;
    if (g_game.listener != nullptr) {
        const std::uint32_t bits = g_game.listener(heard.v);
        heard.camera = (bits & 1u) != 0u;
        heard.player = (bits & 2u) != 0u;
    }
    return heard;
}

// With g_lock: a placed voice's gains from where it is and where the camera hears from. A place not there now (no
// camera; no player for one that follows it) keeps the gains last aimed.
void aim_locked(Item* item, const Listener& heard) noexcept {
    if (!item->placed) {
        item->pan_target[0] = 1.0f;
        item->pan_target[1] = 1.0f;
        return;
    }
    if (!heard.camera || (item->follow == LUMA_AUDIO_AT_PLAYER && !heard.player)) {
        return;
    }
    const float* camera = heard.v;
    const float* front = heard.v + 3;
    const float* up = heard.v + 6;
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const float* from = item->follow == LUMA_AUDIO_AT_PLAYER ? heard.v + 9
                        : item->follow == LUMA_AUDIO_AT_CAMERA ? camera : zero;
    float rel[3];
    for (int i = 0; i < 3; ++i) {
        rel[i] = from[i] + item->offset[i] - camera[i];
    }
    const float distance = std::sqrt(rel[0] * rel[0] + rel[1] * rel[1] + rel[2] * rel[2]);
    float gain = 1.0f;
    if (distance >= item->far_units) {
        gain = 0.0f;
    } else if (distance > item->near_units) {
        gain = (item->far_units - distance) / (item->far_units - item->near_units);
    }
    // The camera's right: front × up (the game's view: x right, y up, the front along -z).
    const float right[3] = {front[1] * up[2] - front[2] * up[1], front[2] * up[0] - front[0] * up[2],
                            front[0] * up[1] - front[1] * up[0]};
    const float right_length = std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);
    float side = 0.0f;
    if (distance > 1.0e-3f && right_length > 1.0e-6f) {
        side = (rel[0] * right[0] + rel[1] * right[1] + rel[2] * right[2]) / (distance * right_length);
        side = std::clamp(side, -1.0f, 1.0f);
    }
    // Constant power, ahead as loud as a sound with no place.
    constexpr float kQuarterPi = 0.78539816f;
    constexpr float kRootTwo = 1.41421356f;
    const float angle = (side + 1.0f) * kQuarterPi;
    item->pan_target[0] = std::cos(angle) * kRootTwo * gain;
    item->pan_target[1] = std::sin(angle) * kRootTwo * gain;
}

// With g_lock: the mod's placer made (once) and queued for the next frame's start. May throw (memory).
void need_placer_locked(ModState* state, const LumaMod* mod) {
    if (state->placer == nullptr) {
        auto placer = std::make_unique<Item>();
        placer->node.event.mod = mod;
        placer->node.event.deliver = deliver;
        placer->node.event.drop = drop;
        placer->node.item = placer.get();
        placer->owner = state;
        placer->placer = true;
        placer->silent = true;  // never mixed
        g_items.reserve(g_items.size() + 1u);
        state->placer = placer.get();
        g_items.push_back(placer.release());
        g_live.store(g_items.size(), std::memory_order_release);
    }
    post_locked(state->placer);
}

// The placer's frame (game thread, the frame's start): the game's following sounds moved, the mod's placed voices
// aimed, and the node queued again while either is left.
void place_frame(Item* placer) noexcept {
    const std::uint32_t following = g_game.follow != nullptr ? g_game.follow() : 0u;
    const Listener heard = listen();
    const std::lock_guard<std::mutex> lock(g_lock);
    placer->posted = false;
    if (placer->owner == nullptr || placer->closed) {
        reap_locked();
        return;
    }
    bool again = following != 0u;
    for (Item* item : g_items) {
        if (item->owner == placer->owner && item->placed && !item->silent && !item->ended) {
            aim_locked(item, heard);
            again = true;
        }
    }
    if (again) {
        post_locked(placer);
    }
    reap_locked();
}

// ---- The calls, continued ------------------------------------------------------------------------------------------

std::int32_t play_sound(const LumaMod* mod, const char* where, const char* name, const LumaAudioPlay* how,
                        const LumaAudioPlace* at, LumaEventFn fn, void* user, LumaHandle* out) noexcept {
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
    if (at != nullptr) {
        if (const std::int32_t status = check_place(mod, where, at); status != LUMA_OK) {
            return status;
        }
    }
    const Listener heard = at != nullptr ? listen() : Listener{};
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const assets::AssetStore* store = g_store.load(std::memory_order_acquire);
    try {
        std::string path;
        if (!store_path(kernel->mod_id(mod), std::string_view(name, length), path)) {
            return argument(mod, where, "name");
        }
        // A name no file has: the placeholder chime plays in its place, with the call's place, bus and settings.
        std::optional<assets::Entry> found = store != nullptr ? store->find(path) : std::nullopt;
        if (!found) {
            found = placeholder_sound(store, kernel->mod_id(mod), std::string_view(name, length));
        }
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
        item->near_units = sound.near_units;
        item->far_units = sound.far_units;
        if (at != nullptr) {
            item->placed = true;
            item->follow = at->follow;
            std::copy(at->pos, at->pos + 3, item->offset);
        }
        const std::lock_guard<std::mutex> lock(g_lock);
        if (at != nullptr) {
            aim_locked(item.get(), heard);
            item->pan[0] = item->pan_target[0];  // heard from its place from its first sample
            item->pan[1] = item->pan_target[1];
            need_placer_locked(state, mod);
        }
        // The 33rd: the mod's voice of the lowest priority ends (the new one's included; the oldest of equals).
        Item* lowest = nullptr;
        std::uint32_t playing = 0;
        for (Item* other : g_items) {
            if (other->owner == state && !other->stream && !other->music && !other->tap && !other->placer &&
                !other->closed && !other->ended) {
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

std::int32_t audio_play(const LumaMod* mod, const char* name, const LumaAudioPlay* how, LumaEventFn fn, void* user,
                        LumaHandle* out) noexcept {
    return play_sound(mod, "audio.play", name, how, nullptr, fn, user, out);
}

std::int32_t audio_play_at(const LumaMod* mod, const char* name, const LumaAudioPlay* how, const LumaAudioPlace* at,
                           LumaEventFn fn, void* user, LumaHandle* out) noexcept {
    return play_sound(mod, "audio.play_at", name, how, at, fn, user, out);
}

std::int32_t audio_voice_place(const LumaMod* mod, LumaHandle voice, const LumaAudioPlace* at) noexcept {
    static constexpr const char* where = "audio.voice_place";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (at != nullptr) {
        if (const std::int32_t status = check_place(mod, where, at); status != LUMA_OK) {
            return status;
        }
    }
    const Listener heard = at != nullptr ? listen() : Listener{};
    const std::lock_guard<std::mutex> lock(g_lock);
    Item* item = find_locked(state, voice, false);
    if (item == nullptr) {
        return bad_handle(mod, where);
    }
    item->placed = at != nullptr;
    if (at != nullptr) {
        item->follow = at->follow;
        std::copy(at->pos, at->pos + 3, item->offset);
    }
    aim_locked(item, heard);
    if (at != nullptr) {
        try {
            need_placer_locked(state, mod);
        } catch (...) {
            return LUMA_E_MEMORY;
        }
    }
    return LUMA_OK;
}

// The game's own sounds (LumaGameSound.cpp). Their handles are the game module's ids, never a voice's.
std::int32_t audio_game_play(const LumaMod* mod, const char* name, const LumaAudioPlace* at, LumaHandle* out) noexcept {
    static constexpr const char* where = "audio.game_play";
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
    if (at != nullptr) {
        if (const std::int32_t status = check_place(mod, where, at); status != LUMA_OK) {
            return status;
        }
    }
    if (g_game.play == nullptr) {
        return LUMA_E_ABSENT;
    }
    std::uint64_t id = 0;
    const std::int32_t status =
        g_game.play(mod, name, at != nullptr ? at->follow : kNoPlace, at != nullptr ? at->pos : nullptr, &id);
    if (status == LUMA_E_LIMIT) {
        report(mod, where, 104u, "16");  // Service.Limit
        return status;
    }
    if (status == LUMA_E_ARGUMENT) {
        return argument(mod, where, "at");
    }
    if (status != LUMA_OK) {
        return status;  // NOT_FOUND, BUSY: answers
    }
    *out = id;
    if (at != nullptr && at->follow != LUMA_AUDIO_AT_POINT) {
        const std::lock_guard<std::mutex> lock(g_lock);
        try {
            need_placer_locked(state, mod);
        } catch (...) {
            // It plays where it started; it follows from the next place or play that finds memory.
        }
    }
    return LUMA_OK;
}

std::int32_t audio_game_place(const LumaMod* mod, LumaHandle sound, const LumaAudioPlace* at) noexcept {
    static constexpr const char* where = "audio.game_place";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (at == nullptr) {
        return argument(mod, where, "at");
    }
    if (const std::int32_t status = check_place(mod, where, at); status != LUMA_OK) {
        return status;
    }
    const std::int32_t status = g_game.place != nullptr ? g_game.place(mod, sound, at->follow, at->pos) : LUMA_E_HANDLE;
    if (status == LUMA_E_HANDLE) {
        return bad_handle(mod, where);
    }
    if (status == LUMA_E_ARGUMENT) {
        return argument(mod, where, "sound");  // started with no place
    }
    if (status == LUMA_OK && at->follow != LUMA_AUDIO_AT_POINT) {
        const std::lock_guard<std::mutex> lock(g_lock);
        try {
            need_placer_locked(state, mod);
        } catch (...) {
        }
    }
    return status;
}

std::int32_t audio_game_stop(const LumaMod* mod, LumaHandle sound, std::uint32_t fade_ms) noexcept {
    static constexpr const char* where = "audio.game_stop";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (fade_ms > LUMA_AUDIO_FADE_MOST_MS) {
        return argument(mod, where, "fade_ms");
    }
    const std::int32_t status = g_game.stop != nullptr ? g_game.stop(mod, sound, fade_ms) : LUMA_E_HANDLE;
    if (status == LUMA_E_HANDLE) {
        return bad_handle(mod, where);
    }
    return status;
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

std::int32_t audio_stream_channels(const LumaMod* mod, LumaHandle stream, std::uint32_t* out) noexcept {
    static constexpr const char* where = "audio.stream_channels";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    const Item* item = find_locked(state, stream, true);
    if (item == nullptr) {
        return bad_handle(mod, where);
    }
    *out = static_cast<std::uint32_t>(item->channels);
    return LUMA_OK;
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

// Game thread, with g_lock: the music playing (any mod's) fades out over fade_ms and ends.
void end_music_locked(std::uint32_t fade_ms) noexcept {
    for (Item* item : g_items) {
        if (item->music && !item->closed) {
            item->closed = true;
            if (!item->fading) {
                item->fading = true;
                item->fade_ms = fade_ms;
                item->fade_total = 0;
            }
        }
    }
}

std::int32_t audio_music(const LumaMod* mod, const char* name, std::uint32_t fade_ms) noexcept {
    static constexpr const char* where = "audio.music";
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
    if (fade_ms > LUMA_AUDIO_FADE_MOST_MS) {
        return argument(mod, where, "fade_ms");
    }
    const std::uint32_t fade = fade_ms == 0u ? kRampMs : fade_ms;
    const LumaServiceKernel* kernel = g_kernel.load(std::memory_order_acquire);
    const assets::AssetStore* store = g_store.load(std::memory_order_acquire);
    try {
        std::string path;
        if (!store_path(kernel->mod_id(mod), std::string_view(name, length), path)) {
            return argument(mod, where, "name");
        }
        // A name no file has: the placeholder chime plays as the music, once (looped, it would ring on).
        std::optional<assets::Entry> found = store != nullptr ? store->find(path) : std::nullopt;
        const bool placeholder = !found;
        if (!found) {
            found = placeholder_sound(store, kernel->mod_id(mod), std::string_view(name, length));
        }
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
        if (bytes == nullptr || !read_sound(bytes, found->length, sound) || sound.frames == 0u) {
            report(mod, where, kRowNotASound, name);  // Audio.NotASound
            return LUMA_E_FORMAT;
        }
        auto item = std::make_unique<Item>();
        item->node.event.mod = mod;
        item->node.event.deliver = deliver;
        item->node.event.drop = drop;
        item->node.item = item.get();
        item->owner = state;
        item->music = true;
        item->name.assign(name, length);
        item->samples = sound.samples;
        item->channels = sound.channels;
        item->frames = sound.frames;
        item->bus = LUMA_AUDIO_BUS_MUSIC;
        item->sound_volume = sound.volume;
        item->volume = sound.volume;
        item->pitch_pick = 1.0f;
        item->pitch = 1.0f;
        item->ramp_ms = fade;
        item->looping = !placeholder;
        item->loop_start = sound.loop_start != kNoLoop ? sound.loop_start : 0u;
        item->loop_end = sound.loop_start != kNoLoop ? sound.loop_end : sound.frames;
        const std::lock_guard<std::mutex> lock(g_lock);
        end_music_locked(fade);
        g_items.reserve(g_items.size() + 1u);
        item->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
        g_items.push_back(item.release());
        g_music_ms.store(fade, std::memory_order_relaxed);
        g_music_target.store(0.0f, std::memory_order_relaxed);
        reap_locked();
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

std::int32_t audio_music_restore(const LumaMod* mod, std::uint32_t fade_ms) noexcept {
    static constexpr const char* where = "audio.music_restore";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (fade_ms > LUMA_AUDIO_FADE_MOST_MS) {
        return argument(mod, where, "fade_ms");
    }
    const std::uint32_t fade = fade_ms == 0u ? kRampMs : fade_ms;
    const std::lock_guard<std::mutex> lock(g_lock);
    bool mine = false;
    for (const Item* item : g_items) {
        mine |= item->music && !item->closed && item->owner == state;
    }
    if (mine) {
        end_music_locked(fade);
        g_music_ms.store(fade, std::memory_order_relaxed);
        g_music_target.store(1.0f, std::memory_order_relaxed);
    }
    reap_locked();
    return LUMA_OK;
}

std::int32_t audio_tap_open(const LumaMod* mod, std::uint32_t source, LumaEventFn fn, void* user,
                            LumaHandle* out) noexcept {
    static constexpr const char* where = "audio.tap_open";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    if (source > LUMA_AUDIO_TAP_MODS) {
        return argument(mod, where, "source");
    }
    if (fn == nullptr) {
        return argument(mod, where, "fn");
    }
    if (out == nullptr) {
        return argument(mod, where, "out");
    }
    try {
        {
            const std::lock_guard<std::mutex> lock(g_lock);
            std::uint32_t open = 0;
            for (const Item* other : g_items) {
                open += other->owner == state && other->tap && !other->closed ? 1u : 0u;
            }
            if (open >= LUMA_AUDIO_TAPS_MOST) {
                report(mod, where, 104u, "2");  // Service.Limit
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
        item->tap = true;
        item->silent = true;  // never mixed
        item->source = source;
        item->filling.assign(LUMA_AUDIO_TAP_FRAMES * 2u, 0.0f);  // made here: the mix never allocates
        item->blocks.assign(kTapBlocks * LUMA_AUDIO_TAP_FRAMES * 2u, 0.0f);
        item->handing.assign(LUMA_AUDIO_TAP_FRAMES * 2u, 0.0f);
        const std::lock_guard<std::mutex> lock(g_lock);
        g_items.reserve(g_items.size() + 1u);
        item->handle = g_next_handle.fetch_add(1, std::memory_order_relaxed);
        *out = item->handle;
        if (source == LUMA_AUDIO_TAP_OUTPUT) {
            g_taps.fetch_add(1, std::memory_order_relaxed);
        }
        g_items.push_back(item.release());
        reap_locked();
    } catch (...) {
        return LUMA_E_MEMORY;
    }
    return LUMA_OK;
}

std::int32_t audio_tap_close(const LumaMod* mod, LumaHandle tap) noexcept {
    static constexpr const char* where = "audio.tap_close";
    ModState* state = nullptr;
    if (const std::int32_t status = begin(mod, where, &state); status != LUMA_OK) {
        return status;
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    Item* item = find_locked(state, tap, false, true);
    if (item == nullptr) {
        return bad_handle(mod, where);
    }
    item->closed = true;
    if (item->source == LUMA_AUDIO_TAP_OUTPUT) {
        g_taps.fetch_sub(1, std::memory_order_relaxed);
    }
    reap_locked();
    return LUMA_OK;
}

// ---- Game sounds a mod's sound stands in for (sound.md 6; game thread) ---------------------------------------------

// Made once by set_game from the session's mods: each game sound's name to the cooked sound that replaces it, the mod
// highest in the list winning (as for files). Read only after that.
struct Replacement {
    Sound sound;
    std::string mod;
    unsigned order = 0;
};
std::unordered_map<std::string, Replacement> g_replacements;
// The owner of every replacement voice: no mod, each bus's share 1. Its voices are closed from the start (no events)
// and end with their sound, a fade (a level sound not renewed, the 33rd), or never before the game stops.
ModState g_replacing;
std::uint64_t g_replace_frame = 0;

// The cooked sounds under a mod's folder of the store (Luma/<id>/...), each with a replace name, into the index.
void index_replacements(const assets::AssetStore& store, std::int32_t folder, std::uint32_t children,
                        const mods::ModInfo& mod, unsigned depth) {
    if (depth > 32u) {
        return;
    }
    for (std::uint32_t i = 0; i < children; ++i) {
        const std::optional<assets::Entry> entry = store.child(folder, i);
        if (!entry) {
            continue;
        }
        if (entry->folder) {
            index_replacements(store, entry->index, entry->children, mod, depth + 1u);
            continue;
        }
        const std::string_view name = entry->name != nullptr ? std::string_view(entry->name) : std::string_view();
        if (name.size() <= 5u || name.substr(name.size() - 5u) != ".nsfx" || entry->length < kNsfxHeader) {
            continue;
        }
        const std::uint8_t* bytes = store.map(entry->position, static_cast<std::size_t>(entry->length));
        Sound sound;
        if (bytes == nullptr || !read_sound(bytes, entry->length, sound)) {
            continue;
        }
        const std::uint32_t at = field<std::uint32_t>(bytes, 60u);
        if (at < kNsfxHeader || at >= entry->length) {
            continue;  // 0: replaces nothing
        }
        const std::size_t most = static_cast<std::size_t>(entry->length - at);
        const char* text = reinterpret_cast<const char*>(bytes + at);
        const std::size_t length = ::strnlen(text, most);
        if (length == 0u || length == most || length > LUMA_AUDIO_NAME_MOST) {
            continue;  // not 0-ended inside the file
        }
        const std::string replaced(text, length);
        const auto known = g_replacements.find(replaced);
        if (known == g_replacements.end() || mod.order < known->second.order) {
            g_replacements[replaced] = Replacement{sound, mod.id, mod.order};
        }
    }
}

void make_replacements() {
    const assets::AssetStore* store = g_store.load(std::memory_order_acquire);
    if (store == nullptr) {
        return;
    }
    for (const mods::ModInfo& mod : mods::session_mods().started_with) {
        if (!mod.package || !mod.enabled) {
            continue;
        }
        const std::optional<assets::Entry> folder = store->find("Luma/" + mod.id);
        if (folder && folder->folder) {
            index_replacements(*store, folder->index, folder->children, mod, 0u);
        }
    }
}

// The game's start of a sound (LumaGameSound::replaced): 1 when the replacing sound plays instead. A level sound
// renewed while its voice plays only moves it.
std::int32_t replace_start(const char* name, const float* pos, std::int32_t level) noexcept {
    if (name == nullptr) {
        return 0;
    }
    try {
        const auto found = g_replacements.find(std::string(name));
        if (found == g_replacements.end()) {
            return 0;
        }
        const Sound& sound = found->second.sound;
        bool placed = pos != nullptr;
        for (int i = 0; placed && i < 3; ++i) {
            placed = finite_in(pos[i], -LUMA_AUDIO_PLACE_MOST, LUMA_AUDIO_PLACE_MOST);
        }
        const Listener heard = placed ? listen() : Listener{};
        const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(pos);
        std::unique_lock<std::mutex> lock(g_lock);
        if (level != 0) {
            for (Item* item : g_items) {
                if (item->owner == &g_replacing && item->level && item->level_key == key && !item->ended &&
                    !item->silent && item->name == found->first) {
                    item->renewed = g_replace_frame;
                    if (placed) {
                        std::copy(pos, pos + 3, item->offset);
                        aim_locked(item, heard);
                    }
                    return 1;
                }
            }
        }
        auto item = std::make_unique<Item>();
        item->owner = &g_replacing;
        item->closed = true;  // no mod holds it: no handle, no event
        item->name = found->first;
        item->samples = sound.samples;
        item->channels = sound.channels;
        item->frames = sound.frames;
        item->bus = sound.bus;
        item->sound_volume = sound.volume;
        item->volume = sound.volume;
        item->pitch_pick = pick(sound.pitch_low, sound.pitch_high);
        item->pitch = item->pitch_pick;
        item->priority = sound.priority;
        item->order = ++g_order;
        // A level sound keeps the sound's loop while renewed. A sound the game starts once plays once: the game ends
        // its own looping ones through the handle, which a replacement does not have.
        if (level != 0 && sound.loop_start != kNoLoop) {
            item->looping = true;
            item->loop_start = sound.loop_start;
            item->loop_end = sound.loop_end;
        }
        item->level = level != 0;
        item->level_key = key;
        item->renewed = g_replace_frame;
        item->near_units = sound.near_units;
        item->far_units = sound.far_units;
        if (placed) {
            item->placed = true;
            item->follow = LUMA_AUDIO_AT_POINT;
            std::copy(pos, pos + 3, item->offset);
            aim_locked(item.get(), heard);
            item->pan[0] = item->pan_target[0];
            item->pan[1] = item->pan_target[1];
        }
        // The 33rd replacement playing: the lowest priority ends (the new one's included; the oldest of equals).
        Item* lowest = nullptr;
        std::uint32_t playing = 0;
        for (Item* other : g_items) {
            if (other->owner == &g_replacing && !other->ended && !other->silent) {
                playing++;
                if (lowest == nullptr || other->priority < lowest->priority ||
                    (other->priority == lowest->priority && other->order < lowest->order)) {
                    lowest = other;
                }
            }
        }
        if (playing >= LUMA_AUDIO_VOICES_MOST) {
            if (lowest == nullptr || item->priority < lowest->priority) {
                return 1;  // the game's stays silent too: the mod's sound stands for it, dropped as the game drops
            }
            lowest->ended = true;
            lowest->fading = true;
            lowest->fade_ms = kRampMs;
        }
        g_items.reserve(g_items.size() + 1u);
        g_items.push_back(item.release());
        g_live.store(g_items.size(), std::memory_order_release);
        reap_locked();
        lock.unlock();
        // In a harness run: which game sound a mod's stood in for (nothing otherwise).
        std::string json = "{\"name\": ";
        modlog::append_json_text(json, found->first);
        json += ", \"mod\": ";
        modlog::append_json_text(json, found->second.mod);
        json += level != 0 ? ", \"level\": 1" : ", \"level\": 0";
        json += placed ? ", \"placed\": 1}" : ", \"placed\": 0}";
        modlog::fact("audio.replaced", json);
        return 1;
    } catch (...) {
        return 0;  // memory: the game's own sound plays
    }
}

// Once a frame (LumaGameSound::frame): placed replacements aimed from where the camera is now; a level one the game
// did not renew last frame fades out.
void replace_frame() noexcept {
    ++g_replace_frame;
    const Listener heard = listen();
    const std::lock_guard<std::mutex> lock(g_lock);
    for (Item* item : g_items) {
        if (item->owner != &g_replacing || item->silent) {
            continue;
        }
        if (item->level && !item->ended && g_replace_frame > item->renewed + 1u) {
            item->ended = true;
            item->fading = true;
            item->fade_ms = kRampMs;
        }
        if (item->placed) {
            aim_locked(item, heard);
        }
    }
    reap_locked();
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
    if (g_game.gone != nullptr) {
        g_game.gone(mod);  // its game sounds stop the same frame
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    state->placer = nullptr;
    for (Item* item : g_items) {
        if (item->owner == state) {
            if (item->tap && !item->closed && item->source == LUMA_AUDIO_TAP_OUTPUT) {
                g_taps.fetch_sub(1, std::memory_order_relaxed);
            }
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
    audio_music,
    audio_music_restore,
    audio_tap_open,
    audio_tap_close,
    audio_play_at,
    audio_voice_place,
    audio_game_play,
    audio_game_place,
    audio_game_stop,
    audio_stream_channels,
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

void set_game(void* (*find_symbol)(const char*)) noexcept {
    if (find_symbol == nullptr) {
        return;
    }
    g_game.play = reinterpret_cast<decltype(g_game.play)>(find_symbol("luma_game_sound_play"));
    g_game.place = reinterpret_cast<decltype(g_game.place)>(find_symbol("luma_game_sound_place"));
    g_game.stop = reinterpret_cast<decltype(g_game.stop)>(find_symbol("luma_game_sound_stop"));
    g_game.gone = reinterpret_cast<decltype(g_game.gone)>(find_symbol("luma_game_sound_gone"));
    g_game.follow = reinterpret_cast<decltype(g_game.follow)>(find_symbol("luma_game_sound_follow"));
    g_game.listener = reinterpret_cast<decltype(g_game.listener)>(find_symbol("luma_game_listener"));
    if (g_game.play == nullptr || g_game.place == nullptr || g_game.stop == nullptr || g_game.gone == nullptr ||
        g_game.follow == nullptr || g_game.listener == nullptr) {
        g_game = Game{};  // all or none: game_play answers LUMA_E_ABSENT, places keep the gains of no place
    }
}

// The game sounds mods replace (sound.md 6), whether any mod has code or not: the index of the session's cooked sounds
// with a replace name, and the module's replacer given this service's two calls when there is one (else it stays null
// and a sound's start costs the game one test). Game thread, once, after the mods' store is attached.
void set_replacements(void* (*find_symbol)(const char*)) noexcept {
    if (find_symbol == nullptr) {
        return;
    }
    using Replacer = void (*)(std::int32_t (*)(const char*, const float*, std::int32_t), void (*)());
    const auto replacer = reinterpret_cast<Replacer>(find_symbol("luma_game_sound_replacer"));
    if (replacer == nullptr) {
        return;
    }
    try {
        g_replacements.clear();
        make_replacements();
    } catch (...) {
        g_replacements.clear();  // memory: the game's own sounds
    }
    replacer(g_replacements.empty() ? nullptr : replace_start, g_replacements.empty() ? nullptr : replace_frame);
    if (!g_replacements.empty()) {
        try {
            modlog::fact("audio.replacing", "{\"count\": " + std::to_string(g_replacements.size()) + "}");
        } catch (...) {
        }
    }
}

std::span<const LumaServiceProvider* const> providers(void (*)(const char*)) noexcept {
    return kProviders;
}

bool mix(float* stereo, std::size_t frames, std::uint32_t rate, const float player[4]) noexcept {
    if (stereo == nullptr || frames == 0u || rate == 0u) {
        return false;
    }
    move_music_share(frames, rate);
    if (g_live.load(std::memory_order_acquire) == 0u) {
        if (g_music_target.load(std::memory_order_relaxed) == 0.0f) {  // no music left: the game's own back
            g_music_ms.store(1000u, std::memory_order_relaxed);
            g_music_target.store(1.0f, std::memory_order_relaxed);
        }
        return false;
    }
    float heard[4];
    for (std::size_t b = 0; b < 4u; ++b) {
        heard[b] = finite_in(player[b], 0.0f, 4.0f) ? player[b] : (player[b] > 4.0f ? 4.0f : 0.0f);
    }
    bool any = false;
    bool music = false;
    const std::lock_guard<std::mutex> lock(g_lock);
    for (Item* item : g_items) {
        if (item->owner == nullptr || item->silent) {
            continue;
        }
        const float target = item->volume * item->owner->bus[item->bus] * heard[item->bus];
        any |= item->stream ? mix_stream(item, stereo, frames, rate, target)
                            : mix_voice(item, stereo, frames, rate, target);
        music |= item->music && !item->closed;
    }
    // A music whose mod is gone or that ended: the game's own back (its restore set the target already).
    if (!music && g_music_target.load(std::memory_order_relaxed) == 0.0f) {
        g_music_ms.store(1000u, std::memory_order_relaxed);
        g_music_target.store(1.0f, std::memory_order_relaxed);
    }
    for (Item* item : g_items) {
        if (item->tap && item->source == LUMA_AUDIO_TAP_MODS && item->owner != nullptr && !item->closed) {
            feed_tap(item, stereo, frames, rate);
        }
    }
    return any;
}

void tap_output(const float* stereo, std::size_t frames, std::uint32_t rate) noexcept {
    if (g_taps.load(std::memory_order_relaxed) == 0u || stereo == nullptr || frames == 0u) {
        return;
    }
    const std::lock_guard<std::mutex> lock(g_lock);
    for (Item* item : g_items) {
        if (item->tap && item->source == LUMA_AUDIO_TAP_OUTPUT && item->owner != nullptr && !item->closed) {
            feed_tap(item, stereo, frames, rate);
        }
    }
}

float game_music_share() noexcept {
    return g_music_share.load(std::memory_order_relaxed);
}

}  // namespace galaxy::luma_audio
