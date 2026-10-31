// The native game's Windows host services (native_host.cpp): the game's audio output, its files and the Mii
// database; the controls are galaxy/input/host_input.h.
#pragma once

#include "galaxy/host_env.h"
#include "galaxy/input/host_input.h"
#include "galaxy/controller_roster.h"
#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/host_pointer_events.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/wiimote_speaker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace galaxy::host {

struct GamePaths {
    std::filesystem::path content_root;
};

GamePaths locate_game(const std::filesystem::path& input);


}  // namespace galaxy::host
