// Where the game's store is: a game.pak named directly, or the folder holding it.
#include "galaxy/game_paths.h"

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace galaxy::host {

namespace {

// Windows compares file names without case; elsewhere the name must match exactly.
bool is_game_pak(const std::filesystem::path& name) {
#ifdef _WIN32
    constexpr std::wstring_view kName = L"game.pak";
    const std::wstring& text = name.native();
    if (text.size() != kName.size()) return false;
    for (std::size_t i = 0; i < kName.size(); ++i) {
        const wchar_t c = text[i];
        if ((c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c) != kName[i]) return false;
    }
    return true;
#else
    return name == "game.pak";
#endif
}

}  // namespace

GamePaths locate_game(const std::filesystem::path& input) {
    if (std::filesystem::is_regular_file(input)) {
        if (is_game_pak(input.filename())) {
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
        "game input does not contain DATA/game.pak or game.pak; run Nebula Setup first");
}
}  // namespace galaxy::host
