// Player 1's input as the host gives it to the game (native_game_input) from a harness plan's steps through the test
// extension (buttons, stick, shake, pointer), and tilt (the Star Ball's upright hold loses the pointer). Hidden window: no live keyboard or
// mouse.

#include "galaxy/launch_args.h"
#include "galaxy/host_extension.h"
#include "galaxy/input/controls_engine.h"
#include "galaxy/input/host_input.h"
#include "galaxy/native_input.h"

#include <cmath>
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

galaxy::input::PlayerInput input_at(std::uint64_t frame, std::uint8_t tilt = 0) {
    return galaxy::host::native_game_input(frame, -1, tilt);
}

// The plan's steps, on the test clock (a bare plan: from the first frame the extension is given). The pointer is in
// 0..1 of the screen, the host's -1..1: (0.37, 0.65) is (-0.26, 0.3).
constexpr const char* kPlan = "harness 1\nbare 1\n"
                              "step 100 hold A+B+C+PLUS 10\nstep 200 shake 5\nstep 300 stick 0.5 -1 10\n"
                              "step 400 pointer 0.37 0.65 300\nstep 500 stick 1 0 20\nstep 1000 end\n";

// The extension reads its plan once, when attached; the steps run as the host gives it frames.
bool attach_plan() {
    static const NebulaHostForExtension host{NEBULA_HOST_EXTENSION_VERSION, sizeof(NebulaHostForExtension)};
    const std::filesystem::path file = NEBULA_TEST_EXTENSION_FILE;
    const std::filesystem::path work = NEBULA_TEST_WORK;
    std::string error;
    // Without a work folder the extension declines (its data guard); without a plan too; with this test's folders and
    // plan, it attaches.
    _putenv_s("NEBULA_WORK", "");
    expect(!galaxy::extensions::attach({&file, 1u}, host, error) && error.find("declined") != std::string::npos,
           "the extension declines a run with no work folder");
    _putenv_s("NEBULA_WORK", work.string().c_str());
    for (const char* name : {"NEBULA_SETTINGS", "NEBULA_APPDATA", "NEBULA_SAVES", "LOCALAPPDATA"}) {
        _putenv_s(name, (work / name).string().c_str());
    }
    _putenv_s("LUMA_HARNESS_PLAN", "");
    error.clear();
    expect(!galaxy::extensions::attach({&file, 1u}, host, error) && error.find("declined") != std::string::npos,
           "the extension declines a run with no harness plan");
    const std::filesystem::path out = work / "player-input";
    std::filesystem::create_directories(out);
    {
        std::ofstream plan(out / "plan.txt", std::ios::binary | std::ios::trunc);
        plan << "out " << out.generic_string() << "\n" << kPlan;
    }
    _putenv_s("LUMA_HARNESS_PLAN", (out / "plan.txt").string().c_str());
    error.clear();
    if (!galaxy::extensions::attach({&file, 1u}, host, error)) {
        std::cerr << "FAILED: the test extension: " << error << '\n';
        return false;
    }
    return true;
}

// The frames up to `frame` given to the extensions, as the host gives each game frame; then that frame's input.
galaxy::input::PlayerInput step_to(std::uint64_t frame, std::uint8_t tilt = 0) {
    static std::uint64_t given = 0u;
    for (; given <= frame; ++given) {
        galaxy::extensions::frame(NebulaExtensionFrame{given});
    }
    return input_at(frame, tilt);
}

void planned_input() {
    galaxy::input::PlayerInput in = step_to(50);
    expect(galaxy::input::button_bits(in.buttons) == 0u && !in.shake && in.stick_x == 0.0f && !in.pointer_on,
           "nothing before the scripts' windows");
    in = step_to(105);
    expect(in.buttons.a && in.buttons.b && in.buttons.c && in.buttons.plus && !in.buttons.z,
           "the autopress window holds A, B, C and +");
    expect(galaxy::input::button_bits(in.buttons) ==
               (galaxy::input::kButtonA | galaxy::input::kButtonB | galaxy::input::kButtonC | galaxy::input::kButtonPlus),
           "as the game's bits");
    expect(step_to(202).shake && !step_to(206).shake, "SHAKE shakes for its window");
    in = step_to(305);
    const galaxy::input::StickValue stick = galaxy::input::nunchuk_stick(0.5f, -1.0f);
    expect(in.stick_x == stick.x && in.stick_y == stick.y && in.stick_y < 0.0f, "the stick script through the response");
    expect(step_to(320).stick_x == 0.0f, "the stick lets go after its window");

    in = step_to(400);
    expect(in.pointer_on && near(in.pointer_x, -0.26f) && near(in.pointer_y, 0.3f) && in.roll_x == 1.0f,
           "the scripted pointer, level, from its frame");
    expect(near(in.distance, galaxy::input::kVirtualPointerDistance), "at the virtual remote's distance");
    expect(in.gravity == galaxy::input::Gravity{0.0f, 0.0f, 1.0f}, "lying still outside tilt sections");
    // The Star Ball: the remote held upright, no pointer; the stick leans it.
    in = step_to(505, 1);
    expect(!in.pointer_on, "the Star Ball's upright hold has no pointer");
    expect(in.gravity[1] < -0.8f && in.gravity[0] > 0.2f, "upright, leaned right by the stick");
    in = step_to(600, 0);
    expect(in.pointer_on && in.gravity == galaxy::input::Gravity{0.0f, 0.0f, 1.0f}, "back flat after the section");
}

}  // namespace

int main() {
    galaxy::g_launch_args.started_hidden = true;
    try {
        if (attach_plan()) {
            planned_input();
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
