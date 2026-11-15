// SPDX-License-Identifier: GPL-3.0-only
// A hidden start (LaunchArgs::started_hidden: a tool's run, never a player's) keeps off the player's data.
#pragma once

#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>

// A literal in the system's own argument and path text: UTF-16 on Windows, bytes elsewhere.
#if defined(_WIN32)
#define NEBULA_ARG_TEXT(text) L##text
#else
#define NEBULA_ARG_TEXT(text) text
#endif

namespace galaxy::host {

// One character of the process's arguments and environment as the system gives them (wchar_t on Windows, char
// elsewhere), and their text.
using ArgChar = std::filesystem::path::value_type;
using ArgText = std::basic_string<ArgChar>;

// Whether argument `a` is `b`.
[[nodiscard]] inline bool same_arg(const ArgChar* a, const ArgChar* b) {
    return std::basic_string_view<ArgChar>(a) == std::basic_string_view<ArgChar>(b);
}

// Whether `path` is `root` or inside it, both absolute, after resolving `..` and links (Windows: any letter case).
[[nodiscard]] inline bool path_is_within(const std::filesystem::path& path, const std::filesystem::path& root) {
    if (!path.is_absolute() || !root.is_absolute()) return false;
    std::error_code error;
    const auto canonical_root = std::filesystem::weakly_canonical(root, error);
    if (error) return false;
    const auto canonical_path = std::filesystem::weakly_canonical(path, error);
    if (error) return false;
    auto p = canonical_path.begin();
    for (const auto& component : canonical_root) {
        if (p == canonical_path.end()) return false;
#if defined(_WIN32)
        auto a = component.native(), b = p->native();
        for (auto& ch : a) ch = static_cast<wchar_t>(std::towlower(ch));
        for (auto& ch : b) ch = static_cast<wchar_t>(std::towlower(ch));
        if (a != b) return false;
#else
        if (component != *p) return false;
#endif
        ++p;
    }
    return true;
}

// The player's data folder as the system names it for this account, whatever the environment says (Windows: the
// shell's Local AppData\Nebula; Linux: <the account's home>/.local/share/Nebula; macOS: <the account's
// home>/Library/Application Support/Nebula, the home read from the account database, not HOME); empty when the system
// gives none.
[[nodiscard]] std::filesystem::path player_data_folder();

// Empty when the start may go on. A hidden start names its own --settings and --crash-dumps folders, and no folder it
// names (--saves, --data, --settings, --crash-dumps) is inside the player's data folder or holds it. Called before
// startup writes anything; `hidden` is LaunchArgs::started_hidden.
[[nodiscard]] const char* hidden_start_error(bool hidden, int argc, const ArgChar* const* argv);

}  // namespace galaxy::host
