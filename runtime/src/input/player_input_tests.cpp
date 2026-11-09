// Player 1's input as the host gives it to the game (native_game_input), per source: the test scripts (buttons,
// stick, shake, pointer), a replayed keyboard and a replayed XInput controller, and tilt (the Star Ball's upright hold
// loses the pointer). Hidden window: no live keyboard or mouse.

#include "galaxy/runtime_settings.h"
#include "galaxy/launch_args.h"
#include "galaxy/test_temp.h"
#include "galaxy/input/controls_engine.h"
#include "galaxy/input/host_input.h"
#include "galaxy/native_input.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

bool g_ok = true;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        g_ok = false;
    }
}

bool near(float a, float b, float tolerance = 1e-5f) {
    return std::fabs(a - b) <= tolerance;
}

void set_env(const char* name, const char* value) {
    _putenv_s(name, value != nullptr ? value : "");
}

galaxy::input::PlayerInput input_at(std::uint64_t frame, std::uint8_t tilt = 0) {
    return galaxy::host::native_game_input(frame, -1, tilt);
}

void scripted_input() {
    set_env("GALAXY_INPUT_AUTOPRESS_SCRIPT", "100:10:A+B+C+PLUS;200:5:SHAKE");
    set_env("GALAXY_INPUT_STICK_SCRIPT", "300:10:0.5:-1");
    galaxy::input::PlayerInput in = input_at(50);
    expect(galaxy::input::button_bits(in.buttons) == 0u && !in.shake && in.stick_x == 0.0f && !in.pointer_on,
           "nothing before the scripts' windows");
    in = input_at(105);
    expect(in.buttons.a && in.buttons.b && in.buttons.c && in.buttons.plus && !in.buttons.z,
           "the autopress window holds A, B, C and +");
    expect(galaxy::input::button_bits(in.buttons) ==
               (galaxy::input::kButtonA | galaxy::input::kButtonB | galaxy::input::kButtonC | galaxy::input::kButtonPlus),
           "as the game's bits");
    expect(input_at(202).shake && !input_at(206).shake, "SHAKE shakes for its window");
    in = input_at(305);
    const galaxy::input::StickValue stick = galaxy::input::nunchuk_stick(0.5f, -1.0f);
    expect(in.stick_x == stick.x && in.stick_y == stick.y && in.stick_y < 0.0f, "the stick script through the response");
    expect(input_at(320).stick_x == 0.0f, "the stick lets go after its window");
    set_env("GALAXY_INPUT_AUTOPRESS_SCRIPT", nullptr);
    set_env("GALAXY_INPUT_STICK_SCRIPT", nullptr);

    // The stop clock before the first stop holds the scripts; the first stop lets them act.
    set_env("GALAXY_INPUT_AUTOPRESS_SCRIPT", "0:10:A");
    galaxy::host::controls::g_native_input_script_held.store(true);
    expect(!input_at(5).buttons.a, "a held script presses nothing");
    galaxy::host::controls::g_native_input_script_held.store(false);
    expect(input_at(5).buttons.a, "a released script presses again");
    set_env("GALAXY_INPUT_AUTOPRESS_SCRIPT", nullptr);

    set_env("GALAXY_INPUT_POINTER_X", "-0.26");
    set_env("GALAXY_INPUT_POINTER_Y", "0.3");
    in = input_at(400);
    expect(in.pointer_on && in.pointer_x == -0.26f && in.pointer_y == 0.3f && in.roll_x == 1.0f,
           "the scripted pointer, level");
    expect(near(in.distance, galaxy::input::kVirtualPointerDistance), "at the virtual remote's distance");
    expect(in.gravity == galaxy::input::Gravity{0.0f, 0.0f, 1.0f}, "lying still outside tilt sections");
    // The Star Ball: the remote held upright, no pointer; the stick leans it.
    set_env("GALAXY_INPUT_STICK_SCRIPT", "500:20:1:0");
    in = input_at(505, 1);
    expect(!in.pointer_on, "the Star Ball's upright hold has no pointer");
    expect(in.gravity[1] < -0.8f && in.gravity[0] > 0.2f, "upright, leaned right by the stick");
    in = input_at(600, 0);
    expect(in.pointer_on && in.gravity == galaxy::input::Gravity{0.0f, 0.0f, 1.0f}, "back flat after the section");
    set_env("GALAXY_INPUT_STICK_SCRIPT", nullptr);
    set_env("GALAXY_INPUT_POINTER_X", nullptr);
    set_env("GALAXY_INPUT_POINTER_Y", nullptr);
}

// A replay log (key_order as the recorder writes it): `rows` tab-separated after the header.
std::filesystem::path write_replay(const char* name, const char* rows) {
    const std::filesystem::path path = galaxy::test_temp_path(name);
    std::ofstream out(path, std::ios::binary);
    out << "meta\tkey_order\tLButton,RButton,Space,Enter,Shift,LShift,RShift,Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,"
           "Q,R,S,W,Z,Ctrl,LCtrl,RCtrl\n";
    out << "elapsed_ms\tgame_focused\tmouse_client_x\tmouse_client_y\tclient_w\tclient_h\tkeys_hold_hex\t"
           "xinput_connected\txinput_buttons_hex\tleft_trigger\tright_trigger\tthumb_lx\tthumb_ly\tthumb_rx\tthumb_ry\n";
    out << rows;
    return path;
}

void replayed_input() {
    // 0 s: Space (A), D (stick right), C, E (shake); 2 s: left Shift (B), Z; 4 s: a controller's A and full left
    // stick.
    const std::filesystem::path log = write_replay(
        "nebula_player_input_tests.tsv",
        "0\t1\t-1\t-1\t1280\t720\t0x38004\t0\t0x0\t0\t0\t0\t0\t0\t0\n"
        "2000\t1\t-1\t-1\t1280\t720\t0x4000020\t0\t0x0\t0\t0\t0\t0\t0\t0\n"
        "4000\t1\t-1\t-1\t1280\t720\t0x0\t1\t0x1000\t0\t0\t-32768\t0\t0\t0\n");
    set_env("GALAXY_INPUT_REPLAY_LOG", log.string().c_str());
    galaxy::RuntimeSettings settings = galaxy::get_runtime_settings();
    settings.input_mode = galaxy::RuntimeInputMode::Auto;
    galaxy::set_runtime_settings(settings);
    galaxy::input::PlayerInput in = input_at(30);
    expect(in.buttons.a && in.buttons.c && !in.buttons.b && in.shake, "replayed keys: A, C and shake");
    expect(in.stick_x == 1.0f && in.stick_y == 0.0f, "replayed D: the stick right");
    in = input_at(150);
    expect(in.buttons.b && in.buttons.z && !in.buttons.a && in.stick_x == 0.0f, "replayed Shift and Z: B and Z");
    in = input_at(270);
    expect(in.buttons.a && !in.buttons.b && in.stick_x == -1.0f, "a replayed controller: A and the stick left");
    set_env("GALAXY_INPUT_REPLAY_LOG", nullptr);
    std::error_code ignored;
    std::filesystem::remove(log, ignored);
}

}  // namespace

int main() {
    galaxy::g_launch_args.started_hidden = true;
    try {
        scripted_input();
        replayed_input();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        g_ok = false;
    }
    if (g_ok) std::cout << "player input tests passed\n";
    return g_ok ? 0 : 1;
}
