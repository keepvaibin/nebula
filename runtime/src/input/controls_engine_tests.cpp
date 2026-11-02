// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/input/controls_engine.h"
#include "galaxy/input_bindings.h"

#if defined(_WINDOWS_) || defined(_INC_WINDOWS)
#error Controls engine tests must not include Windows.h
#endif

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace galaxy::host::controls;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
constexpr auto header =
    "meta\tkey_order\tLButton,RButton,Space,Enter,Shift,LShift,RShift,Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,Q,R,S,W,Z,Ctrl,LCtrl,RCtrl\n"
    "elapsed_ms\tgame_focused\tmouse_client_x\tmouse_client_y\tclient_w\tclient_h\tkeys_hold_hex\txinput_connected\txinput_buttons_hex\tleft_trigger\tright_trigger\tthumb_lx\tthumb_ly\tthumb_rx\tthumb_ry\n";
void replay_parser() {
    const auto file = std::filesystem::temp_directory_path() / "controls_engine_replay.tsv";
    const auto write = [&](const std::string& content) {
        std::ofstream out(file, std::ios::binary); out << content;
        if (!out) throw std::runtime_error("write replay fixture");
    };
    write(std::string("\xef\xbb\xbf") + header +
          "0\t1\t-1\t2\t1280\t720\t0x4\t1\t0x1000\t0\t255\t-32768\t32767\t0\t0\n");
    const auto log = load_native_input_replay_log(file.string());
    check(log.samples.size() == 1 && log.samples[0].thumb_lx == -32768 && log.samples[0].right_trigger == 255,
          "replay BOM and signed/unsigned limits");
    const auto rejected = [&](const std::string& content) {
        write(content);
        try { (void)load_native_input_replay_log(file.string()); }
        catch (const std::runtime_error&) { return true; }
        return false;
    };
    check(rejected(std::string(header) + "0\t1\t0\t0\t1\t1\t0\t1\t65536\t0\t0\t0\t0\t0\t0\n"), "oversized buttons rejected");
    check(rejected(std::string(header) + "0\t1\t0\t0\t1\t1\t0\t1\t0\t256\t0\t0\t0\t0\t0\n"), "oversized trigger rejected");
    check(rejected(std::string(header) + "0\t2\t0\t0\t1\t1\t0\t1\t0\t0\t0\t0\t0\t0\t0\n"), "nonboolean focus rejected");
    check(rejected(std::string(header) + "0\t1\t0\t0\t1\t1\t0\t1\t0\t0\t0\t32768\t0\t0\t0\n"), "oversized signed axis rejected");
    check(rejected(std::string(header) + "0\t1\t0\t0\t1\t1\t0\t1\t0\t0\t0\t0\n"), "short row rejected");
    check(rejected(std::string(header)), "empty replay rejected");
    std::filesystem::remove(file);
}
void projected_replay() {
    using namespace galaxy::input;
    NativeInputReplaySample replay;
    replay.game_focused = true;
    replay.mouse_client_x = 10; replay.mouse_client_y = 20;
    replay.keys_hold = std::uint64_t{1} << 2;
    NativeMousePointerProjection projection;
    projection.visible = true; projection.x = 0.25f; projection.y = -0.4f;
    int identity = 0;
    auto input = build_replayed_input(10, replay, {}, galaxy::RuntimeInputMode::KeyboardMouse,
                                     tilt::Section::None, projection, &identity);
    check(input.buttons.a && input.pointer_on && input.pointer_x == 0.25f && input.pointer_y == -0.4f,
          "typed projected mouse and replayed keyboard reach game state");
    replay.game_focused = false;
    projection.visible = false;
    input = build_replayed_input(11, replay, {}, galaxy::RuntimeInputMode::KeyboardMouse,
                                 tilt::Section::None, projection, &identity);
    check(!input.buttons.a && !input.pointer_on, "focus and invalid projection release keyboard/mouse");
    replay.xinput_connected = true;
    replay.xinput_buttons = static_cast<std::uint16_t>(pad::bit(pad::kSouthBit));
    replay.thumb_lx = -32768;
    input = build_replayed_input(12, replay, {}, galaxy::RuntimeInputMode::Controller,
                                 tilt::Section::None, projection, &identity);
    check(input.buttons.a && input.stick_x == -1.0f, "portable pad masks preserve replayed controller");
    NativeInputScriptOverlay script;
    script.buttons.z = true; script.has_stick = true; script.stick_x = 0.5f; script.shake = true;
    input = build_replayed_input(13, replay, script, galaxy::RuntimeInputMode::Controller,
                                 tilt::Section::None, projection, &identity);
    check(input.buttons.z && input.shake && input.stick_x == nunchuk_stick(0.5f, 0).x, "script overlay retains precedence");
}
}
int main() {
    try {
        replay_parser(); projected_replay();
        std::cout << "controls engine parser/projection/mapping passed\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
