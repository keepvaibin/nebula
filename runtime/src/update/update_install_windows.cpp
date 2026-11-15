// SPDX-License-Identifier: GPL-3.0-only
// The update on Windows: the game checks (update.cpp over update_transport_winhttp.cpp) and offers the release on the
// home screen; the install is Nebula Setup's. Choosing Update starts the install folder's Nebula-Setup.exe with
// `--update --yes`: it downloads the release's Setup, checks its signature against the release key and runs it, which
// installs the new version beside the running one (the game has left by then). A game that is not an installed copy
// (a build tree) offers the release without Update.
#include "update_internal.h"

#include "galaxy/platform_paths.h"

#include "galaxy/windows_api.h"

#include <string>

namespace galaxy::update {

namespace {

// The install folder of this game, `<root>\versions\<version>\Nebula.exe`, when it has its Setup and current.json.
std::filesystem::path install_root() {
    const std::filesystem::path program = galaxy::program_directory();
    if (program.empty() || !program.has_parent_path()) return {};
    const std::filesystem::path versions = program.parent_path();
    if (_wcsicmp(versions.filename().c_str(), L"versions") != 0) return {};
    const std::filesystem::path root = versions.parent_path();
    std::error_code error;
    if (!std::filesystem::is_regular_file(root / L"Nebula-Setup.exe", error) || !std::filesystem::is_regular_file(root / L"current.json", error)) {
        return {};
    }
    return root;
}

}  // namespace

bool start_setup_update() {
    const std::filesystem::path root = install_root();
    if (root.empty()) return false;
    const std::wstring setup = (root / L"Nebula-Setup.exe").wstring();
    std::wstring command = L"\"" + setup + L"\" --update --yes";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(setup.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, root.c_str(), &startup, &process)) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

namespace detail {

std::string platform_asset(bool beta) { return beta ? "Nebula-Beta-Setup.exe" : "Nebula-Setup.exe"; }

bool platform_can_install() noexcept {
    try {
        return !install_root().empty();
    } catch (...) {
        return false;
    }
}

bool platform_install(const std::filesystem::path&, std::string& error) {
    error = "Nebula Setup installs updates on Windows";
    return false;
}

}  // namespace detail

}  // namespace galaxy::update
