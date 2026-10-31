// SPDX-License-Identifier: GPL-3.0-only
// The native game's host services on SDL3 platforms (native_host.h, native_audio.h): the game's sound through SDL's
// audio stream, player 1's input from the on-screen controls, the finger on the picture, keys and gamepads (with the
// same bindings and tuning as Windows), vibration, and the device data the game reads.

#include "sdl_host.h"

#include "galaxy/content_viewport.h"
#include "galaxy/input_bindings.h"
#include "galaxy/audio_mixer.h"
#include "galaxy/native_audio.h"
#include "galaxy/native_host.h"
#include "galaxy/native_input.h"
#include "galaxy/platform_time.h"
#include "galaxy/pointer_control.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/tilt.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <vector>

namespace galaxy::host {
namespace {

namespace in = galaxy::input;

// The finger's pointer stays where it was lifted this long, so the cursor does not blink between touches.
constexpr std::uint64_t kTouchPointerHoldMs = 1500;
// A pad or key pointer at rest hides after this long, as the Wii's does when the remote points away.
constexpr std::uint64_t kStickPointerHoldMs = 3000;
// A tap in a menu is A for this many reports (100 a second), as long as a finger's press, once the pointer has rested
// where the finger touched this long: the game takes A on what the pointer is over, which it finds a few frames
// after the pointer arrives, and reads the remote once a frame (a shorter press can fall between two reads).
constexpr std::uint8_t kTapPressReports = 10;
constexpr std::uint64_t kTapHoverMs = 200;
constexpr float kTriggerThreshold = 96.0f / 255.0f;  // the Windows host's XInput trigger threshold

const in::ControlsConfig& live_controls() {
    static in::ControlsConfig config = in::get_controls_config();
    static std::uint64_t generation = in::controls_config_generation();
    if (const std::uint64_t now = in::controls_config_generation(); now != generation) {
        config = in::get_controls_config();
        generation = now;
    }
    return config;
}

// The on-screen controls as the bindings' output.
in::LogicalInput touch_logic(const sdl::InputState& state) {
    in::LogicalInput logic{};
    const auto press = [&logic](in::WiiTarget target) { logic.pressed |= in::wii_target_bit(target); };
    const std::uint32_t b = state.touch_buttons;
    if (b & sdl::kTouchA) press(in::WiiTarget::A);
    if (b & sdl::kTouchB) press(in::WiiTarget::B);
    if (b & sdl::kTouchZ) press(in::WiiTarget::Z);
    if (b & sdl::kTouchC) press(in::WiiTarget::C);
    if (b & sdl::kTouchShake) press(in::WiiTarget::Shake);
    if (b & sdl::kTouchMinus) press(in::WiiTarget::Minus);
    if (b & sdl::kTouchPlus) press(in::WiiTarget::Plus);
    if (b & sdl::kTouchHome) press(in::WiiTarget::Home);
    if (b & sdl::kTouchOne) press(in::WiiTarget::One);
    if (b & sdl::kTouchTwo) press(in::WiiTarget::Two);
    if (b & sdl::kTouchUp) press(in::WiiTarget::DpadUp);
    if (b & sdl::kTouchDown) press(in::WiiTarget::DpadDown);
    if (b & sdl::kTouchLeft) press(in::WiiTarget::DpadLeft);
    if (b & sdl::kTouchRight) press(in::WiiTarget::DpadRight);
    logic.stick_x = state.touch_stick_x;
    logic.stick_y = state.touch_stick_y;
    return logic;
}

// A point in window pixels as the pointer (-1..1 across the picture, y down), through the rectangle the picture was last drawn in.
bool project_window_point(const sdl::InputState& state, float x, float y, float& out_x, float& out_y) {
    const auto content = galaxy::g_presentation_content.read();
    if (!content || content->surface_width != state.window_width || content->surface_height != state.window_height) {
        return false;
    }
    const in::NativeMousePointerProjection projection = in::project_native_mouse_pointer({
        state.window_focused, true, true, static_cast<int>(x), static_cast<int>(y), state.window_width,
        state.window_height, content->viewport, 1.0f, 1.0f});
    if (!projection.visible) {
        return false;
    }
    out_x = projection.x;
    out_y = projection.y;
    return true;
}

std::uint32_t g_rumble_pad = 0;  // the gamepad the player used last (SDL id; 0: none)
float g_rumble_strength = 1.0f;

}  // namespace

in::PlayerInput native_game_input(std::uint64_t, std::int32_t star_pointer_mode, std::uint8_t tilt_section) {
    const in::PlayContext context =
        star_pointer_mode >= 0 && static_cast<std::uint32_t>(star_pointer_mode) < in::kStarPointerModeCount
        ? in::play_context_from_star_pointer_mode(static_cast<std::uint32_t>(star_pointer_mode))
        : in::PlayContext::Play;
    sdl::set_menu_context(context == in::PlayContext::Menu);
    const in::tilt::Section section = tilt_section == 1u ? in::tilt::Section::StarBall
        : tilt_section == 2u ? in::tilt::Section::Ray
        : in::tilt::Section::None;
    const sdl::InputState state = sdl::input_state();
    const in::ControlsConfig& config = live_controls();
    in::PlayerInput snapshot{};

    // Gamepads: the bindings and tuning of each one's scheme, as on Windows.
    static std::array<std::uint32_t, sdl::kMaxPads> s_previous{};
    in::LogicalInput pads{};
    float pad_pointer_speed = 0.0f;
    for (std::size_t i = 0; i < sdl::kMaxPads; ++i) {
        in::PadSample sample = state.pads[i];
        if (!sample.connected) {
            s_previous[i] = 0u;
            continue;
        }
        const in::SchemeTuning& tuning = config.tune(sample.scheme);
        const in::StickVector left = in::apply_radial_deadzone(sample.axes[0], sample.axes[1], tuning.stick_deadzone);
        const in::StickVector right =
            in::apply_radial_deadzone(sample.axes[2], sample.axes[3], tuning.pointer_deadzone);
        sample.axes[0] = left.x;
        sample.axes[1] = left.y;
        sample.axes[2] = right.x;
        sample.axes[3] = right.y;
        in::LogicalInput logic =
            in::evaluate_pad_bindings(config.table(sample.scheme), sample, kTriggerThreshold, s_previous[i], false);
        s_previous[i] = logic.pressed;
        if (tuning.invert_pointer_y) {
            logic.pointer_y = -logic.pointer_y;
        }
        if (logic.pressed != 0u || logic.stick_x != 0.0f || logic.stick_y != 0.0f || logic.pointer_x != 0.0f ||
            logic.pointer_y != 0.0f) {
            g_rumble_pad = state.pad_ids[i];
            g_rumble_strength = tuning.vibration ? static_cast<float>(tuning.rumble) / 255.0f : 0.0f;
            pad_pointer_speed = tuning.pointer_speed;
        }
        pads = in::merge_logical_input(pads, logic);
    }
    in::LogicalInput keys = in::evaluate_keyboard_bindings(config.table(in::ControlScheme::Keyboard), state.keys);
    if (config.tune(in::ControlScheme::Keyboard).invert_pointer_y) {
        keys.pointer_y = -keys.pointer_y;
    }
    const in::LogicalInput logic = in::merge_logical_input(in::merge_logical_input(pads, keys), touch_logic(state));
    const auto held = [&logic](in::WiiTarget target) { return logic.held(target); };

    snapshot.buttons.left = held(in::WiiTarget::DpadLeft);
    snapshot.buttons.right = held(in::WiiTarget::DpadRight);
    snapshot.buttons.down = held(in::WiiTarget::DpadDown);
    snapshot.buttons.up = held(in::WiiTarget::DpadUp);
    snapshot.buttons.plus = held(in::WiiTarget::Plus);
    snapshot.buttons.minus = held(in::WiiTarget::Minus);
    snapshot.buttons.one = held(in::WiiTarget::One);
    snapshot.buttons.two = held(in::WiiTarget::Two);
    snapshot.buttons.home = held(in::WiiTarget::Home);
    // A controller's B: jump while Mario is under control, back in menus.
    static in::JumpBackLatch s_jump_back{};
    const in::JumpBackLatch::Output jump_back = s_jump_back.update(held(in::WiiTarget::JumpBack), context);
    snapshot.buttons.a = held(in::WiiTarget::A) || jump_back.a;
    snapshot.buttons.b = held(in::WiiTarget::B) || jump_back.b;
    const in::StickValue stick = in::nunchuk_stick(logic.stick_x, logic.stick_y);
    snapshot.stick_x = stick.x;
    snapshot.stick_y = stick.y;
    snapshot.buttons.c = held(in::WiiTarget::C);
    snapshot.buttons.z = held(in::WiiTarget::Z);

    // The pointer: the finger (or mouse) on the picture where it is; a stick or keys move it at their speed.
    static float s_pointer_x = 0.0f;
    static float s_pointer_y = 0.0f;
    static std::uint64_t s_pointer_ms = 0;
    static std::uint64_t s_hold_ms = 0;
    static std::uint64_t s_last_ms = galaxy::steady_ms();
    const std::uint64_t now = galaxy::steady_ms();
    const float dt = std::min(0.1f, static_cast<float>(now - s_last_ms) / 1000.0f);
    s_last_ms = now;
    float x = 0.0f;
    float y = 0.0f;
    if (state.finger_ms != 0 && now - state.finger_ms <= kTouchPointerHoldMs &&
        project_window_point(state, state.finger_x, state.finger_y, x, y)) {
        if (state.finger_down || state.finger_ms > s_pointer_ms) {
            s_pointer_x = x;
            s_pointer_y = y;
            s_pointer_ms = state.finger_ms;
            s_hold_ms = kTouchPointerHoldMs;
        }
    }
    const float axis_x = pads.pointer_x != 0.0f || pads.pointer_y != 0.0f ? pads.pointer_x : keys.pointer_x;
    const float axis_y = pads.pointer_x != 0.0f || pads.pointer_y != 0.0f ? pads.pointer_y : keys.pointer_y;
    if (axis_x != 0.0f || axis_y != 0.0f) {
        const float speed = pad_pointer_speed > 0.0f ? pad_pointer_speed : config.tune(in::ControlScheme::Keyboard).pointer_speed;
        s_pointer_x = std::clamp(s_pointer_x + axis_x * speed * dt, -1.0f, 1.0f);
        s_pointer_y = std::clamp(s_pointer_y - axis_y * speed * dt, -1.0f, 1.0f);
        s_pointer_ms = now;
        s_hold_ms = kStickPointerHoldMs;
    }
    // A tap in a menu points there and presses A.
    static std::uint64_t s_taps = state.taps;
    static std::uint8_t s_tap_reports = 0;
    static std::uint64_t s_tap_due_ms = 0;  // 0: no press waiting
    if (state.taps != s_taps) {
        s_taps = state.taps;
        if (context == in::PlayContext::Menu && project_window_point(state, state.tap_x, state.tap_y, x, y)) {
            s_pointer_x = x;
            s_pointer_y = y;
            s_pointer_ms = now;
            s_hold_ms = kTouchPointerHoldMs;
            s_tap_due_ms = std::max(now, state.tap_down_ms + kTapHoverMs);
        }
    }
    if (s_tap_due_ms != 0 && now >= s_tap_due_ms) {
        s_tap_due_ms = 0;
        s_tap_reports = kTapPressReports;
    }
    if (s_tap_reports != 0) {
        snapshot.buttons.a = true;
        --s_tap_reports;
    }
    const bool pointer_active = s_pointer_ms != 0 && now - s_pointer_ms <= s_hold_ms;
    in::set_pointer(snapshot, s_pointer_x, s_pointer_y, pointer_active);

    // Tilt sections steer with the stick.
    static in::tilt::Tilt s_tilt{};
    const in::tilt::Tilt::Output tilt = s_tilt.update(section, nullptr, logic.stick_x, logic.stick_y, false);
    if (tilt.active) {
        snapshot.gravity = tilt.game;
    }
    snapshot.pointer_on = snapshot.pointer_on && in::remote_faces_screen(snapshot.gravity);
    snapshot.shake = held(in::WiiTarget::Shake);
    snapshot.nunchuk_shake = held(in::WiiTarget::NunchukShake);
    return snapshot;
}

void native_game_rumble(bool motor_on, std::int32_t) noexcept {
    if (g_rumble_pad != 0) {
        sdl::rumble(g_rumble_pad, motor_on ? g_rumble_strength : 0.0f);
    }
}

GamePaths locate_game(const std::filesystem::path& input) {
    return {input.parent_path()};
}

// The Windows host's links between the window and the live controls; the SDL host reads its own state.
void install_host_pointer_provider(HostPointerProvider) noexcept {}
void install_host_pointer_transition_provider(HostPointerTransitionProvider) noexcept {}
void install_native_keyboard_capture_provider(NativeKeyboardCaptureProvider) noexcept {}
void install_native_settings_pad_hooks(NativeSettingsVisibleProvider, NativeSettingsKeySink) noexcept {}
void set_env_flag_cache_enabled(bool) noexcept {}

// Sound: the native mixer at the output device's rate, its frames through an SDL audio stream (float samples).
struct NativeGameAudio::State {
    std::unique_ptr<galaxy::audio::AudioMixer> mixer;
    SDL_AudioStream* stream = nullptr;
    std::size_t block_bytes = 0;
    float master = 1.0f;
    bool master_started = false;
    std::vector<float> frames;
};

NativeGameAudio::NativeGameAudio() : state_(std::make_unique<State>()) {}

NativeGameAudio::~NativeGameAudio() {
    if (state_ && state_->stream != nullptr) {
        SDL_DestroyAudioStream(state_->stream);
    }
}

void NativeGameAudio::setup(NebulaAudioVoice* voices, std::uint32_t count, const NebulaAudioEffect* effects,
                            const void* sound_memory, std::uint32_t sound_memory_size) {
    State& s = *state_;
    SDL_AudioSpec device{};
    int device_frames = 0;
    int rate = 48000;
    if (SDL_GetAudioDeviceFormat(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &device, &device_frames) && device.freq >= 8000 &&
        device.freq <= 192000) {
        rate = device.freq;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, rate};
    s.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (s.stream == nullptr) {
        std::fprintf(stderr, "[audio] no output: %s\n", SDL_GetError());
    } else {
        SDL_ResumeAudioStreamDevice(s.stream);
    }
    s.mixer = std::make_unique<galaxy::audio::AudioMixer>(static_cast<std::uint32_t>(rate));
    s.mixer->setup(voices, count, effects, static_cast<const std::byte*>(sound_memory), sound_memory_size);
    const galaxy::RuntimeAudioSettings audio = galaxy::get_runtime_settings().audio;
    s.mixer->set_category_gains({1.0f, audio.music, audio.voice, audio.sfx, audio.ambience, audio.cutscene});
}

void NativeGameAudio::mix(float gain) {
    if (state_->mixer != nullptr) {
        state_->mixer->mix(gain);
    }
}

void NativeGameAudio::frame(const std::int16_t* stream, std::uint32_t stream_frames) {
    State& s = *state_;
    if (s.mixer == nullptr) {
        return;
    }
    const std::span<const float> samples = s.mixer->end_frame(stream, stream_frames);
    const galaxy::RuntimeAudioSettings audio = galaxy::get_runtime_settings().audio;
    s.mixer->set_category_gains({1.0f, audio.music, audio.voice, audio.sfx, audio.ambience, audio.cutscene});
    const float target = audio.master >= 0.0f ? std::min(audio.master, 4.0f) : 0.0f;
    const float start = s.master_started ? s.master : target;
    s.master_started = true;
    s.master = target;
    const std::size_t count = samples.size() / 2u;
    if (s.stream == nullptr || count == 0u) {
        return;
    }
    s.frames.resize(count * 2u);
    for (std::size_t i = 0; i < count; ++i) {
        const float gain = start + (target - start) * static_cast<float>(i + 1u) / static_cast<float>(count);
        s.frames[i * 2u] = std::clamp(samples[i * 2u] * gain, -1.0f, 1.0f);
        s.frames[i * 2u + 1u] = std::clamp(samples[i * 2u + 1u] * gain, -1.0f, 1.0f);
    }
    s.block_bytes = count * 2u * sizeof(float);
    (void)SDL_PutAudioStreamData(s.stream, s.frames.data(), static_cast<int>(s.block_bytes));
}

unsigned NativeGameAudio::queued_blocks() const {
    const State& s = *state_;
    if (s.stream == nullptr || s.block_bytes == 0) {
        return 0u;
    }
    const int queued = SDL_GetAudioStreamQueued(s.stream);
    return queued > 0 ? static_cast<unsigned>(static_cast<std::size_t>(queued) / s.block_bytes) : 0u;
}

// SDL's stream plays silence when it runs dry and starts again with the next data: four frames (70 ms at the game's
// 17.5 ms frames) absorb the device's period and the game's mixing time.
unsigned NativeGameAudio::target_blocks() const {
    return 4u;
}

bool NativeGameAudio::output_available() const {
    return state_->stream != nullptr;
}

void NativeGameAudio::set_paused(bool paused) {
    if (state_->stream != nullptr) {
        (void)(paused ? SDL_PauseAudioStreamDevice(state_->stream) : SDL_ResumeAudioStreamDevice(state_->stream));
    }
}

std::string NativeGameAudio::stats_line() const {
    if (state_->mixer == nullptr) {
        return "not set up";
    }
    const galaxy::audio::AudioMixerStats& s = state_->mixer->stats();
    std::ostringstream line;
    line << "rate=" << state_->mixer->rate() << " frames=" << s.frames << " subframes=" << s.subframes
         << " voice-renders=" << s.voice_renders << " adpcm-blocks=" << s.adpcm_blocks << " pcm=" << s.pcm_renders
         << " oscillators=" << s.oscillator_renders << " iir=" << s.iir_renders << " panner=" << s.panner_renders
         << " effect-subframes=" << s.effect_subframes << " unresolved=" << s.unresolved_reads
         << " mix-ms=" << s.mix_ns / 1000000u;
    return line.str();
}

}  // namespace galaxy::host
