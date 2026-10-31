// The native game's Windows host services: keyboard, mouse and controllers as Wii
// Remote input reports (with the scripted and replayed test input), vibration and the virtual Wii devices.
#include "galaxy/native_host.h"
#include "galaxy/host_env.h"

#include "galaxy/platform_time.h"
#include "galaxy/controller_hid.h"
#include "galaxy/wiimote_hid.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/pointer_control.h"
#include "galaxy/rumble.h"
#include "galaxy/tilt.h"
#include "galaxy/diagnostic_stick_lease.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/renderer_api.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/scope_exit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "galaxy/windows_power.h"
#include <Xinput.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace galaxy::host {
namespace {

using Microsoft::WRL::ComPtr;



}  // namespace


GamePaths locate_game(const std::filesystem::path& input) {
    if (std::filesystem::is_regular_file(input)) {
        if (input.filename() == L"game.pak") {
            return {input.parent_path()};
        }
        throw std::runtime_error(
            "game input file must be the installer-generated game.pak");
    }
    const std::array<std::filesystem::path, 2> roots{input / L"DATA", input};
    for (const auto& root : roots) {
        if (std::filesystem::is_regular_file(root / L"game.pak")) {
            return {root};
        }
    }
    throw std::runtime_error(
        "game input does not contain DATA/game.pak or game.pak; run build-pak first");
}
}  // namespace galaxy::host
