// SPDX-License-Identifier: GPL-3.0-only
// The Linux install step's parts the tests reach (update_install_linux.cpp, update_install_linux_tests.cpp).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace galaxy::update::detail {

// An install install.sh made: <root>/versions/<version>/nebula beside <root>/current.json and <root>/nebula-launch.
struct ScriptInstall {
    std::filesystem::path root;
    bool portable = false;  // <root>/portable.txt
};

// The install `program` runs from, when the update may install into it: the folder is writable, and a user install is the
// one install.sh updates (${XDG_DATA_HOME:-$HOME/.local/share}/Nebula); nothing otherwise.
[[nodiscard]] std::optional<ScriptInstall> script_install_of(const std::filesystem::path& program, const char* xdg_data_home,
                                                             const char* home);

// The verified package (the release's tar.gz) unpacked in `work` and its install.sh run with --yes (and --portable for a
// portable folder): the new version beside the running one, named in current.json. `error` says why it did not.
bool install_with_script(const std::filesystem::path& package, const ScriptInstall& install,
                         const std::filesystem::path& work, std::string& error);

// The program current.json names when it is not `program` (an update installed during this run), or "".
[[nodiscard]] std::filesystem::path next_program_of(const std::filesystem::path& program);

// LD_LIBRARY_PATH with each entry naming `from` changed to `to` (nebula-launch puts the version's folder first).
[[nodiscard]] std::string library_path_for(std::string_view library_path, const std::filesystem::path& from,
                                           const std::filesystem::path& to);

}  // namespace galaxy::update::detail
