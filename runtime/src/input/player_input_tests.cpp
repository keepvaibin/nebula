// Player 1's input as the host gives it to the game (native_game_input) from the test extension's scripts (buttons,
// stick, shake, pointer), and tilt (the Star Ball's upright hold loses the pointer). Hidden window: no live keyboard or
// mouse.

#include "galaxy/launch_args.h"
#include "galaxy/host_extension.h"
#include "galaxy/input/controls_engine.h"
#include "galaxy/input/host_input.h"
#include "galaxy/native_input.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
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

galaxy::input::PlayerInput input_at(std::uint64_t frame, std::uint8_t tilt = 0) {
    return galaxy::host::native_game_input(frame, -1, tilt);
}

// The extension reads its scripts once, when attached.
bool attach_scripts() {
    _putenv_s("GALAXY_INPUT_AUTOPRESS_SCRIPT", "100:10:A+B+C+PLUS;200:5:SHAKE");
    _putenv_s("GALAXY_INPUT_STICK_SCRIPT", "300:10:0.5:-1;500:20:1:0");
    _putenv_s("GALAXY_INPUT_POINTER2_X", "-0.26");
    _putenv_s("GALAXY_INPUT_POINTER2_Y", "0.3");
    _putenv_s("GALAXY_INPUT_POINTER2_FRAME", "400");
    static const NebulaHostForExtension host{NEBULA_HOST_EXTENSION_VERSION, sizeof(NebulaHostForExtension)};
    const std::filesystem::path file = NEBULA_TEST_EXTENSION_FILE;
    std::string error;
    if (!galaxy::extensions::attach({&file, 1u}, host, error)) {
        std::cerr << "FAILED: the test extension: " << error << '\n';
        return false;
    }
    return true;
}

void scripted_input() {
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

    in = input_at(400);
    expect(in.pointer_on && in.pointer_x == -0.26f && in.pointer_y == 0.3f && in.roll_x == 1.0f,
           "the scripted pointer, level, from its frame");
    expect(near(in.distance, galaxy::input::kVirtualPointerDistance), "at the virtual remote's distance");
    expect(in.gravity == galaxy::input::Gravity{0.0f, 0.0f, 1.0f}, "lying still outside tilt sections");
    // The Star Ball: the remote held upright, no pointer; the stick leans it.
    in = input_at(505, 1);
    expect(!in.pointer_on, "the Star Ball's upright hold has no pointer");
    expect(in.gravity[1] < -0.8f && in.gravity[0] > 0.2f, "upright, leaned right by the stick");
    in = input_at(600, 0);
    expect(in.pointer_on && in.gravity == galaxy::input::Gravity{0.0f, 0.0f, 1.0f}, "back flat after the section");
}

}  // namespace

int main() {
    galaxy::g_launch_args.started_hidden = true;
    try {
        if (attach_scripts()) {
            scripted_input();
        } else {
            g_ok = false;
        }
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        g_ok = false;
    }
    if (g_ok) std::cout << "player input tests passed\n";
    return g_ok ? 0 : 1;
}
