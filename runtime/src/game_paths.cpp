// Where the game's store is: a game.pak named directly, or the folder holding it.
#include "galaxy/game_paths.h"

#include <filesystem>
#include <stdexcept>

namespace galaxy::host {

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
