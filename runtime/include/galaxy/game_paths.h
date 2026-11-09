// Where the game's store is. (Its includes are what the host's files take from here.)
#pragma once

#include "galaxy/input/host_input.h"
#include "galaxy/controller_roster.h"
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
