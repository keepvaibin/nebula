// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/live_controls_engine.h"
#include "galaxy/input/mod_actions.h"
#include "galaxy/luma_input_service.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error The shared live controls engine must not include Windows.h
#endif

namespace galaxy::host::controls {
namespace { constexpr std::uint64_t kMousePointerLossHoldMs = 500u; }
bool pad_has_input(const galaxy::input::PadSample& sample) {
    if (!sample.connected) {
        return false;
    }
    if (sample.buttons != 0u) {
        return true;
    }
    for (std::size_t i = 0; i < 4u; ++i) {
        if (std::fabs(sample.axes[i]) > 0.001f) {
            return true;
        }
    }
    return sample.axes[4] > 30.0f / 255.0f || sample.axes[5] > 30.0f / 255.0f;
}

LiveControlsResult LiveControlsEngine::sample(const LiveControlsFrame& frame, LiveControlsHost& host) {
    LiveControlsResult result;
    auto& snapshot = result.input;
    const auto& config = frame.config;
    const auto& keyboard_table = config.table(galaxy::input::ControlScheme::Keyboard);
    const auto& keyboard_levels = frame.keyboard_levels;
    const auto& script = frame.script;
    const auto live_pads = frame.pads;
    const bool use_pads = frame.use_pads && (frame.window_focused || frame.allow_background_device_input);
    const bool use_keyboard = frame.use_keyboard;
    // The first consuming read after suspension includes motion accumulated while input was inactive.
    const bool discard_accumulated_motion = use_pads && s_device_input_suspended;
    s_device_input_suspended = !use_pads;
    const bool window_focused = frame.window_focused, allow_absolute_pointer = frame.allow_absolute_pointer;
    const bool drop_injected = frame.drop_injected;
    const auto current_frame = frame.game_frame;
    const auto play_context = frame.context;
    const auto tilt_section = frame.tilt_section;
    const float trigger_threshold = static_cast<float>(kXInputTriggerThreshold) / 255.0f;
    if (s_pad_history.capacity() < live_pads.size()) s_pad_history.reserve(live_pads.size());
    for (auto& history : s_pad_history) history.seen = false;
    // Each scheme's bindings turn its physical inputs into game targets. Pad
    // and keyboard pointer axes stay apart: they are different pointer
    // sources (velocity from the stick, keys at the keyboard speed).
    galaxy::input::LogicalInput logic{};
    galaxy::input::LogicalInput pad_logic_all{};
    bool motion_shake_remote = false;
    bool motion_shake_nunchuk = false;
    float gyro_x = 0.0f;
    float gyro_y = 0.0f;
    float pad_pointer_speed = 0.0f;
    bool pad_gyro_pointing = false;
    // The touch pointer's travel this sample (pointer units, 2 = the picture's width and height; y down).
    float touch_x = 0.0f;
    float touch_y = 0.0f;
    bool pad_touch_pointing = false;
    // Rest-hide tuning of the pad that points (gyro first, then the stick).
    float pointer_hide_seconds = 0.0f;
    float pointer_hide_radius = galaxy::input::kDefaultPointerHideRadius;
    // The controller in use: the last one with any input (buttons, sticks, triggers, a gyro turn, a
    // shake). A key or mouse button bound to a game input hands control to the keyboard. In the tilt
    // sections (tilt.h) its tilt steers when it has motion sensors (Pro Controller 1/2); otherwise the
    // stick steers (Xbox, keyboard). Vibration goes to it.
    const auto pad_key = [](const LivePad& pad) {
        return (static_cast<std::uint64_t>(pad.sample.scheme) << 32u) | pad.device_slot;
    };
    galaxy::input::tilt::Vec3 tilt_up_value{0.0f, 1.0f, 0.0f};
    const galaxy::input::tilt::Vec3* tilt_up = nullptr;
    if (use_pads) {
        const LivePad* driving = nullptr;
        const LivePad* first_connected = nullptr;
        for (const LivePad& pad : live_pads) {
            if (!pad.sample.connected) continue;
            if (first_connected == nullptr) first_connected = &pad;
            const bool motion_moved = pad.shake_remote || pad.shake_nunchuk ||
                std::fabs(pad.gyro_x) + std::fabs(pad.gyro_y) > 0.003f ||
                std::fabs(pad.touch_dx) + std::fabs(pad.touch_dy) > 0.0005f;
            const bool moved = pad_has_input(pad.sample) || (!discard_accumulated_motion && motion_moved);
            if (moved) s_driving_pad = pad_key(pad);
            if (pad_key(pad) == s_driving_pad) driving = &pad;
        }
        if (driving == nullptr && s_driving_pad != kDrivingKeyboard) {
            driving = first_connected;
            s_driving_pad = driving != nullptr ? pad_key(*driving) : kDrivingNone;
        }
        if (driving != nullptr && driving->gravity_valid) {
            tilt_up_value = driving->gravity;
            tilt_up = &tilt_up_value;
        }
        for (std::size_t index = 0; index < live_pads.size(); ++index) {
            const LivePad& pad = live_pads[index];
            if (!pad.sample.connected) continue;
            const galaxy::input::DeviceKey device{pad.sample.scheme, pad.device_slot};
            auto previous = std::find_if(s_pad_history.begin(), s_pad_history.end(),
                [device](const PadHistory& history) { return history.device == device; });
            if (previous == s_pad_history.end()) {
                s_pad_history.push_back({device});
                previous = s_pad_history.end() - 1;
            }
            previous->seen = true;
            const galaxy::input::SchemeTuning& tuning = config.tune(pad.sample.scheme);
            const bool gyro_pointing = tuning.gyro_pointer && pad.imu_live && !pad.gyro_missing;
            const bool touch_pointing = tuning.touch_pointer && pad.touch_live;
            galaxy::input::LogicalInput pad_logic = galaxy::input::evaluate_pad_bindings(
                config.table(pad.sample.scheme), pad.sample, trigger_threshold,
                previous->pressed, gyro_pointing || touch_pointing);
            previous->pressed = pad_logic.pressed;
            if (tuning.invert_pointer_y) pad_logic.pointer_y = -pad_logic.pointer_y;
            if (std::fabs(pad_logic.pointer_x) + std::fabs(pad_logic.pointer_y) >
                std::fabs(pad_logic_all.pointer_x) + std::fabs(pad_logic_all.pointer_y)) {
                pad_pointer_speed = tuning.pointer_speed;
            }
            if (!pad_gyro_pointing) {
                pointer_hide_seconds = tuning.pointer_hide_seconds;
                pointer_hide_radius = tuning.pointer_hide_radius;
            }
            pad_logic_all = galaxy::input::merge_logical_input(pad_logic_all, pad_logic);
            motion_shake_remote = motion_shake_remote || (!discard_accumulated_motion && pad.shake_remote);
            motion_shake_nunchuk = motion_shake_nunchuk || (!discard_accumulated_motion && pad.shake_nunchuk);
            if (gyro_pointing) {
                pad_gyro_pointing = true;
                pointer_hide_seconds = tuning.pointer_hide_seconds;
                pointer_hide_radius = tuning.pointer_hide_radius;
                if (!discard_accumulated_motion) {
                    gyro_x += pad.gyro_x;
                    gyro_y += tuning.invert_pointer_y ? -pad.gyro_y : pad.gyro_y;
                }
            }
            if (touch_pointing) {
                pad_touch_pointing = true;
                if (!discard_accumulated_motion) {
                    touch_x += pad.touch_dx * 2.0f * tuning.touch_sensitivity;
                    touch_y += (tuning.invert_pointer_y ? -pad.touch_dy : pad.touch_dy) * 2.0f * tuning.touch_sensitivity;
                }
                if (!pad_gyro_pointing) {
                    pointer_hide_seconds = tuning.pointer_hide_seconds;
                    pointer_hide_radius = tuning.pointer_hide_radius;
                }
            }
        }
    }
    std::erase_if(s_pad_history, [](const PadHistory& history) { return !history.seen; });
    galaxy::input::LogicalInput keyboard_logic{};
    if (use_keyboard) {
        keyboard_logic = galaxy::input::evaluate_keyboard_bindings(keyboard_table, keyboard_levels);
        if (config.tune(galaxy::input::ControlScheme::Keyboard).invert_pointer_y) {
            keyboard_logic.pointer_y = -keyboard_logic.pointer_y;
        }
        if (keyboard_logic.pressed != 0u || keyboard_logic.stick_x != 0.0f || keyboard_logic.stick_y != 0.0f) {
            s_driving_pad = kDrivingKeyboard;
        }
    }
    result.rumble.focused = window_focused;
    if (use_pads && s_driving_pad != kDrivingNone && s_driving_pad != kDrivingKeyboard) {
        const auto scheme = static_cast<galaxy::input::ControlScheme>((s_driving_pad >> 32u) & 0xFFu);
        const auto slot = static_cast<std::uint32_t>(s_driving_pad & 0xFFFFFFFFu);
        result.rumble.device = galaxy::input::DeviceKey{scheme, slot};
        if (static_cast<std::size_t>(scheme) < galaxy::input::kControlSchemeCount) {
            const auto& tuning = config.tune(scheme);
            result.rumble.strength = tuning.vibration ? tuning.rumble : std::uint8_t{0};
        }
    }
    logic = galaxy::input::merge_logical_input(galaxy::input::merge_logical_input(pad_logic_all, keyboard_logic),
                                               frame.extra);
    const float pad_pointer_x = pad_logic_all.pointer_x;
    const float pad_pointer_y = pad_logic_all.pointer_y;
    const float keyboard_pointer_x = keyboard_logic.pointer_x;
    const float keyboard_pointer_y = keyboard_logic.pointer_y;
    const auto held = [&logic](galaxy::input::GameInput target) {
        return logic.held(target);
    };
    using galaxy::input::GameInput;
    snapshot.buttons.left = held(GameInput::DpadLeft) || script.buttons.left;
    snapshot.buttons.right = held(GameInput::DpadRight) || script.buttons.right;
    snapshot.buttons.down = held(GameInput::DpadDown) || script.buttons.down;
    snapshot.buttons.up = held(GameInput::DpadUp) || script.buttons.up;
    snapshot.buttons.plus = held(GameInput::Plus) || script.buttons.plus;
    snapshot.buttons.minus = held(GameInput::Minus) || script.buttons.minus;
    snapshot.buttons.one = held(GameInput::One) || script.buttons.one;
    snapshot.buttons.two = held(GameInput::Two) || script.buttons.two;
    snapshot.buttons.home = held(GameInput::Home) || script.buttons.home;
    // B on the controllers: jump while Mario is under control, back in menus.
    const galaxy::input::JumpBackLatch::Output jump_back =
        s_jump_back.update(held(GameInput::JumpBack), play_context);
    snapshot.buttons.a = held(GameInput::A) || jump_back.a || script.buttons.a;
    snapshot.buttons.b = held(GameInput::B) || jump_back.b || script.buttons.b;

    float stick_x = logic.stick_x;
    float stick_y = logic.stick_y;
    if (script.has_stick) {
        stick_x = script.stick_x;
        stick_y = script.stick_y;
    }
    const galaxy::input::StickValue nunchuk_stick = galaxy::input::nunchuk_stick(stick_x, stick_y);
    snapshot.stick_x = nunchuk_stick.x;
    snapshot.stick_y = nunchuk_stick.y;
    snapshot.buttons.c = held(GameInput::C) || script.buttons.c;
    snapshot.buttons.z = held(GameInput::Z) || script.buttons.z;

    const std::uint64_t live_mouse_script_grace_ms =
        frame.script_pointer_grace_ms;
    const std::uint64_t now = host.now_ms();

    bool pointer_active = false;
    bool live_mouse_recent = false;
    // The mouse this sample: where it points, whether that is on the picture
    // and whether it moved (a new physical sample inside the client).
    bool mouse_visible = false;
    bool mouse_moved = false;
    bool mouse_button_held = false;
    const bool drop_injected_mouse = drop_injected && host.injected(false);
    bool host_pointer_polled = false;
    if (allow_absolute_pointer) {
        const auto pointer = host.pointer(
            use_keyboard && !(drop_injected && host.injected(true)), use_keyboard);
        if (pointer.polled) {
            const auto& host_pointer = pointer;
            const auto& button_sample = pointer;
            const auto& projected_host_pointer = pointer.projected;
            host_pointer_polled = true;
            const auto pointer_availability = select_host_pointer_ir_availability(
                pointer.window_focused, pointer.absolute_valid, pointer.inside_client);
            if (use_keyboard &&
                (host_pointer.left_button || host_pointer.right_button)) {
                if (pointer.deliver_buttons) {
                    const std::uint32_t mouse_targets = galaxy::input::mouse_bound_targets(
                        keyboard_table,
                        static_cast<std::uint8_t>((host_pointer.left_button ? 1u : 0u) |
                                                  (host_pointer.right_button ? 2u : 0u)));
                    const auto mouse = [mouse_targets](GameInput target) {
                        return (mouse_targets & galaxy::input::game_input_bit(target)) != 0u;
                    };
                    snapshot.buttons.a = snapshot.buttons.a || mouse(GameInput::A);
                    snapshot.buttons.b = snapshot.buttons.b || mouse(GameInput::B);
                    snapshot.buttons.one = snapshot.buttons.one || mouse(GameInput::One);
                    snapshot.buttons.two = snapshot.buttons.two || mouse(GameInput::Two);
                    snapshot.buttons.plus = snapshot.buttons.plus || mouse(GameInput::Plus);
                    snapshot.buttons.minus = snapshot.buttons.minus || mouse(GameInput::Minus);
                    snapshot.buttons.home = snapshot.buttons.home || mouse(GameInput::Home);
                    snapshot.buttons.up = snapshot.buttons.up || mouse(GameInput::DpadUp);
                    snapshot.buttons.down = snapshot.buttons.down || mouse(GameInput::DpadDown);
                    snapshot.buttons.left = snapshot.buttons.left || mouse(GameInput::DpadLeft);
                    snapshot.buttons.right = snapshot.buttons.right || mouse(GameInput::DpadRight);
                    snapshot.buttons.c = snapshot.buttons.c || mouse(GameInput::C);
                    snapshot.buttons.z = snapshot.buttons.z || mouse(GameInput::Z);
                    logic.pressed |= mouse_targets;
                }
            }
            const bool host_pointer_inside_client =
                pointer_availability.acquired_inside_available &&
                projected_host_pointer.visible;
            const bool host_pointer_absolute_sequence_changed =
                host_pointer.absolute_sequence != 0u &&
                host_pointer.absolute_sequence !=
                    s_last_host_pointer_absolute_sequence;
            const bool host_pointer_transition_sequence_changed =
                button_sample.has_transition_coordinates &&
                button_sample.transition_sequence != 0u &&
                button_sample.transition_sequence !=
                    s_last_host_pointer_transition_sequence;
            const bool host_pointer_has_fresh_coordinates =
                host_pointer.raw_motion || (button_sample.has_transition_coordinates
                ? host_pointer_transition_sequence_changed
                : host_pointer_absolute_sequence_changed);
            // Fresh physical movement inside the client takes precedence over a non-forced script, even when
            // the game window is unfocused. Button ownership stays guarded separately by window focus.
            mouse_button_held = host_pointer.window_focused &&
                                (host_pointer.left_button || host_pointer.right_button);
            if ((host_pointer_inside_client &&
                 host_pointer_has_fresh_coordinates) ||
                mouse_button_held) {
                s_last_live_mouse_tick = now;
            }
            if (button_sample.has_transition_coordinates &&
                button_sample.transition_sequence != 0u) {
                s_last_host_pointer_transition_sequence =
                    button_sample.transition_sequence;
            } else if (host_pointer.absolute_sequence != 0u) {
                s_last_host_pointer_absolute_sequence =
                    host_pointer.absolute_sequence;
            }
            live_mouse_recent =
                s_last_live_mouse_tick != 0 && now >= s_last_live_mouse_tick &&
                now - s_last_live_mouse_tick <= live_mouse_script_grace_ms;
            if (host_pointer_inside_client) {
                s_mouse_x = projected_host_pointer.x;
                s_mouse_y = projected_host_pointer.y;
                // Only a new inside sample, or a queued button edge with its own coordinates, renews the hold
                // through invalid samples; repeated polls of the same coordinate do not.
                if (host_pointer_has_fresh_coordinates) {
                    s_last_mouse_inside_tick = now;
                    s_mouse_pointer_seen_inside = true;
                    mouse_moved = !drop_injected_mouse;
                }
                mouse_visible = true;
            } else if (pointer_availability.invalid_sample_hold_allowed &&
                       s_mouse_pointer_seen_inside &&
                       now >= s_last_mouse_inside_tick &&
                       now - s_last_mouse_inside_tick <=
                           kMousePointerLossHoldMs) {
                // Remote Desktop can fail absolute cursor sampling for a moment while the pointer is still over the
                // client: an invalid sample is uncertainty, not a lost sensor bar.
                mouse_visible = true;
            }
        }
    }
    if (allow_absolute_pointer && !mouse_visible && !host_pointer_polled) {
        const auto projected_cursor = host.cursor();
        if (projected_cursor.visible) {
            mouse_moved = projected_cursor.x != s_mouse_x || projected_cursor.y != s_mouse_y;
            s_mouse_x = projected_cursor.x;
            s_mouse_y = projected_cursor.y;
            mouse_visible = true;
        }
    }

    // One pointer: the source that moved last owns it (pointer_control.h).
    const std::uint64_t now_us = host.now_us();
    const float dt = s_pointer_clock_us == 0u || now_us <= s_pointer_clock_us
        ? 0.0f
        : std::min(static_cast<float>(now_us - s_pointer_clock_us) * 1e-6f, 0.05f);
    s_pointer_clock_us = now_us;
    const galaxy::input::StickVector stick_velocity = galaxy::input::stick_pointer_velocity(
        {pad_pointer_x, pad_pointer_y}, pad_pointer_speed);
    const float keyboard_speed =
        config.tune(galaxy::input::ControlScheme::Keyboard).pointer_speed;
    // The gyro counts as moving above about 5 degrees per second, so hand
    // tremor and rest noise never take the pointer from the mouse or stick.
    constexpr float kGyroActiveUnitsPerSecond = 0.25f;
    // From the mouse (or the finger) the gyro takes the pointer only with a deliberate turn, about 20 degrees a
    // second: a controller held while the mouse aims never pulls the pointer away.
    constexpr float kGyroTakesMouseUnitsPerSecond = 1.0f;
    const float gyro_threshold = s_pointer_owner.owner == galaxy::input::PointerSource::Mouse
        ? kGyroTakesMouseUnitsPerSecond
        : kGyroActiveUnitsPerSecond;
    // A mod's choice of what aims the pointer (input 1.2 pointer_source): only that source moves it; the gyro turns
    // by the mod's sensitivity. What could aim it now goes to the service for its next answer.
    const galaxy::luma_input::PointerChoice mod_choice = galaxy::luma_input::pointer_choice();
    const auto chosen = static_cast<galaxy::input::PointerSource>(mod_choice.source);
    {
        const auto bit = [](galaxy::input::PointerSource source) { return 1u << static_cast<unsigned>(source); };
        const bool any_pad = use_pads && std::any_of(live_pads.begin(), live_pads.end(),
                                                     [](const LivePad& pad) { return pad.sample.connected; });
        galaxy::luma_input::set_pointer_sources((allow_absolute_pointer && use_keyboard ? bit(galaxy::input::PointerSource::Mouse) : 0u) |
                                                (pad_gyro_pointing ? bit(galaxy::input::PointerSource::Gyro) : 0u) |
                                                (any_pad ? bit(galaxy::input::PointerSource::Stick) : 0u));
    }
    if (chosen == galaxy::input::PointerSource::Gyro) {
        gyro_x *= mod_choice.gyro_scale;
        gyro_y *= mod_choice.gyro_scale;
    }
    const float gyro_motion = std::sqrt(gyro_x * gyro_x + gyro_y * gyro_y);
    galaxy::input::PointerActivity activity{};
    activity.mouse = mouse_moved;
    activity.keys = std::fabs(keyboard_pointer_x) > 0.001f || std::fabs(keyboard_pointer_y) > 0.001f;
    activity.stick = stick_velocity.x != 0.0f || stick_velocity.y != 0.0f;
    activity.gyro = pad_gyro_pointing && dt > 0.0f && gyro_motion > gyro_threshold * dt;
    activity.touch = pad_touch_pointing && std::fabs(touch_x) + std::fabs(touch_y) > 0.0f;
    if (chosen != galaxy::input::PointerSource::None) {
        activity = {};
        s_pointer_owner.owner = chosen;
        s_pointer_owner.last_active_us = now_us;
    }
    // Recenter (pointer_control.h RecenterHold): the press gives the pointer to the recentering device
    // at the picture centre. While the button is held, the pointer stays there and that device keeps it.
    const bool recenter_held = held(GameInput::PointerRecenter);
    const galaxy::input::RecenterHold::Output recenter = s_recenter.update(recenter_held);
    if (recenter.pressed) {
        const bool pad_recenter = pad_logic_all.held(GameInput::PointerRecenter);
        s_pointer_owner.owner = pad_recenter
            ? (pad_gyro_pointing    ? galaxy::input::PointerSource::Gyro
               : pad_touch_pointing ? galaxy::input::PointerSource::Touch
                                    : galaxy::input::PointerSource::Stick)
            : galaxy::input::PointerSource::Keys;
    }
    if (recenter.pinned) {
        s_pointer_owner.last_active_us = now_us;
    }
    const galaxy::input::PointerSource owner = recenter.pinned
        ? s_pointer_owner.owner
        : s_pointer_owner.update(activity, now_us);
    switch (owner) {
    case galaxy::input::PointerSource::Mouse:
        if (mouse_visible) {
            s_pointer_x = s_mouse_x;
            s_pointer_y = s_mouse_y;
            pointer_active = true;
        }
        break;
    case galaxy::input::PointerSource::Keys:
        s_pointer_x = std::clamp(s_pointer_x + keyboard_pointer_x * keyboard_speed * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y - keyboard_pointer_y * keyboard_speed * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        pointer_active = true;
        break;
    case galaxy::input::PointerSource::Stick:
        s_pointer_x = std::clamp(s_pointer_x + stick_velocity.x * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y - stick_velocity.y * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        pointer_active = true;
        break;
    case galaxy::input::PointerSource::Gyro:
        s_pointer_x = std::clamp(s_pointer_x + gyro_x,
                                 -galaxy::input::kGyroPointerLimit, galaxy::input::kGyroPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y - gyro_y,
                                 -galaxy::input::kGyroPointerLimit, galaxy::input::kGyroPointerLimit);
        // Pointed past the picture the game hides the pointer, as with a
        // Wii Remote that loses the sensor bar.
        pointer_active = std::fabs(s_pointer_x) <= galaxy::input::kPointerVisibleLimit &&
                         std::fabs(s_pointer_y) <= galaxy::input::kPointerVisibleLimit;
        break;
    case galaxy::input::PointerSource::Touch:
        // The finger moves it as a touchpad moves a cursor; it stays on the picture.
        s_pointer_x = std::clamp(s_pointer_x + touch_x, -galaxy::input::kStickPointerLimit,
                                 galaxy::input::kStickPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y + touch_y, -galaxy::input::kStickPointerLimit,
                                 galaxy::input::kStickPointerLimit);
        pointer_active = true;
        break;
    case galaxy::input::PointerSource::None:
        break;
    }
    if (recenter.pinned) {
        // Gyro and stick motion while the button is held is discarded.
        s_pointer_x = 0.0f;
        s_pointer_y = 0.0f;
        pointer_active = true;
    }
    if (!use_pads && (owner == galaxy::input::PointerSource::Stick ||
                     owner == galaxy::input::PointerSource::Gyro ||
                     owner == galaxy::input::PointerSource::Touch)) pointer_active = false;
    // A connected controller keeps the pointer on screen until the mouse
    // owns it.
    if (!pointer_active && use_pads && owner != galaxy::input::PointerSource::Mouse &&
        owner != galaxy::input::PointerSource::Gyro) {
        pointer_active = true;
    }
    // Still-pointer hiding (pointer_control.h PointerRestHide): a hidden gyro
    // pointer comes back only with recenter; a stick pointer also with a push
    // or a move out of the radius.
    const bool controller_owned = owner == galaxy::input::PointerSource::Stick ||
                                  owner == galaxy::input::PointerSource::Gyro ||
                                  owner == galaxy::input::PointerSource::Touch ||
                                  (owner == galaxy::input::PointerSource::None && use_pads);
    const bool rest_hidden = s_rest_hide.update(
        s_pointer_x, s_pointer_y, now_us, controller_owned,
        recenter.pinned || recenter.released ||
            (owner == galaxy::input::PointerSource::Stick && activity.stick) ||
            (owner == galaxy::input::PointerSource::Touch && activity.touch),
        owner == galaxy::input::PointerSource::Gyro,
        pointer_hide_seconds, pointer_hide_radius);
    if (rest_hidden) {
        pointer_active = false;
    }
    // In menus a key press acts on the pointer's position, so a resting mouse pointer stays.
    if (s_mouse_rest_hide.update(owner == galaxy::input::PointerSource::Mouse && pointer_active && window_focused &&
                                     play_context == galaxy::input::PlayContext::Play,
                                 mouse_moved || mouse_button_held, now_us)) {
        pointer_active = false;
    }
    if (script.has_pointer &&
        (frame.script_pointer_force || !live_mouse_recent ||
         !pointer_active)) {
        s_pointer_x = script.pointer_x;
        s_pointer_y = script.pointer_y;
        pointer_active = true;
    }
    galaxy::input::set_pointer(snapshot, s_pointer_x, s_pointer_y, pointer_active);
    result.pointer_owner = owner;

    if (tilt_up == nullptr) {
        tilt_up = frame.device_up;
    }
    apply_tilt_accel(snapshot, s_tilt, tilt_section, tilt_up, stick_x, stick_y, recenter.pressed);

    const bool device_shake = !discard_accumulated_motion && frame.device_shake;
    const bool shake_pressed =
        script.shake || held(GameInput::Shake) || motion_shake_remote || device_shake;
    if (shake_pressed && !s_previous_shake) {
        std::cerr << "[input-shake] frame=" << current_frame << " source="
                  << (motion_shake_remote || device_shake ? "motion" : held(GameInput::Shake) ? "binding" : "script")
                  << " tilt-section=" << galaxy::input::tilt::section_name(tilt_section) << '\n';
    }
    s_previous_shake = shake_pressed;
    snapshot.shake = shake_pressed;
    snapshot.nunchuk_shake = held(GameInput::NunchukShake) || motion_shake_nunchuk;

    // The mods' actions (services-input): their own bindings on the keyboard and the pads, else the game control each
    // follows as the game gets it this sample (the player's bindings and the extensions' buttons). Nothing with none.
    if (galaxy::input::mod_actions::any()) {
        std::uint32_t game_held = logic.pressed;
        const auto also = [&game_held](bool on, GameInput target) {
            if (on) game_held |= galaxy::input::game_input_bit(target);
        };
        also(snapshot.buttons.a, GameInput::A);
        also(snapshot.buttons.b, GameInput::B);
        also(snapshot.buttons.one, GameInput::One);
        also(snapshot.buttons.two, GameInput::Two);
        also(snapshot.buttons.plus, GameInput::Plus);
        also(snapshot.buttons.minus, GameInput::Minus);
        also(snapshot.buttons.home, GameInput::Home);
        also(snapshot.buttons.up, GameInput::DpadUp);
        also(snapshot.buttons.down, GameInput::DpadDown);
        also(snapshot.buttons.left, GameInput::DpadLeft);
        also(snapshot.buttons.right, GameInput::DpadRight);
        also(snapshot.buttons.c, GameInput::C);
        also(snapshot.buttons.z, GameInput::Z);
        also(shake_pressed, GameInput::Shake);
        also(snapshot.nunchuk_shake, GameInput::NunchukShake);
        std::array<galaxy::input::mod_actions::PadLevels, 8> pad_levels{};
        std::size_t pad_count = 0;
        if (use_pads) {
            for (const LivePad& pad : live_pads) {
                if (!pad.sample.connected || pad_count == pad_levels.size()) continue;
                pad_levels[pad_count++] = {pad.sample.scheme, pad.sample};
            }
        }
        galaxy::input::mod_actions::sample(frame.host_menu || galaxy::luma_input::text_open(), use_keyboard, keyboard_levels,
                                           std::span<const galaxy::input::mod_actions::PadLevels>(pad_levels.data(), pad_count),
                                           game_held);
    }
    return result;
}
}  // namespace galaxy::host::controls
