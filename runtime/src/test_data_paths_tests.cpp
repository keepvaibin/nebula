// The guards that keep a run off the player's data: the host's hidden start (hidden_start.h, every build) and the test
// extension's work-folder policy (tools/test_driver/extension/data_guard.h).
#include "data_guard.h"
#include "galaxy/hidden_start.h"

#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}

std::map<std::wstring, std::wstring> g_environment;

std::wstring environment(const wchar_t* name) {
    const auto it = g_environment.find(name);
    return it == g_environment.end() ? std::wstring() : it->second;
}

struct Args {
    std::vector<std::wstring> text;
    std::vector<const wchar_t*> pointers;
    explicit Args(std::vector<std::wstring> given) : text(std::move(given)) {
        for (const std::wstring& one : text) pointers.push_back(one.c_str());
    }
    [[nodiscard]] int count() const { return static_cast<int>(pointers.size()); }
    [[nodiscard]] const wchar_t* const* data() const { return pointers.data(); }
};
}  // namespace

int main() {
    namespace fs = std::filesystem;
    using galaxy::host::hidden_start_error;
    using galaxy::host::path_is_within;
    try {
        const fs::path root = L"D:/NebulaWork/port/x/runs/path-policy";
        check(path_is_within(root / "saves", root), "child path");
        check(!path_is_within(root / "../outside", root), "parent escape");
        check(!path_is_within(root.string() + "-other", root), "component boundary");
        check(!path_is_within("relative", root), "relative path");
        check(path_is_within(L"d:/nebulawork/port/x/runs/path-policy/settings", root), "Windows case");

        // The host: a hidden start names its own settings and crash folders, none in the player's data folder.
        const fs::path player = galaxy::host::player_data_folder();
        check(!player.empty() && player.filename() == L"Nebula", "the player's data folder");
        const Args private_run({L"game", L"--saves", (root / "saves").wstring(), L"--settings", (root / "settings").wstring(),
                                L"--crash-dumps", (root / "crashes").wstring(), L"--data", (root / "data").wstring()});
        check(hidden_start_error(true, private_run.count(), private_run.data()) == nullptr, "a private hidden start");
        check(hidden_start_error(false, 1, private_run.data()) == nullptr, "a player's start needs no folders");
        check(hidden_start_error(true, 5, private_run.data()) != nullptr, "a hidden start without --crash-dumps");
        for (const wchar_t* name : {L"--saves", L"--data", L"--settings", L"--crash-dumps"}) {
            const Args into_player({L"game", L"--settings", (root / "settings").wstring(), L"--crash-dumps",
                                    (root / "crashes").wstring(), name, (player / "saves").wstring()});
            check(hidden_start_error(true, into_player.count(), into_player.data()) != nullptr,
                  "a folder in the player's data refused");
        }
        const Args holding_player({L"game", L"--settings", (root / "settings").wstring(), L"--crash-dumps",
                                   player.parent_path().wstring()});
        check(hidden_start_error(true, holding_player.count(), holding_player.data()) != nullptr,
              "a folder holding the player's data refused");
        const Args relative({L"game", L"--settings", L"settings", L"--crash-dumps", (root / "crashes").wstring()});
        check(hidden_start_error(true, relative.count(), relative.data()) != nullptr, "a relative folder refused");

        // The test extension: everything inside NEBULA_WORK.
        const std::wstring work = root.wstring();
        g_environment = {{L"NEBULA_WORK", work}};
        const wchar_t* names[] = {L"NEBULA_SETTINGS", L"NEBULA_APPDATA", L"NEBULA_SAVES", L"LOCALAPPDATA"};
        for (const auto* name : names) g_environment[name] = (root / name).wstring();
        check(test_guard::error(private_run.count(), private_run.data(), environment, player.wstring()) == nullptr,
              "private run accepted");
        for (const auto* name : names) {
            const std::wstring kept = g_environment[name];
            g_environment.erase(name);
            check(test_guard::error(private_run.count(), private_run.data(), environment, player.wstring()) != nullptr,
                  "missing path rejected");
            g_environment[name] = kept;
        }
        check(test_guard::error(5, private_run.data(), environment, player.wstring()) != nullptr,
              "a run without --crash-dumps rejected");
        for (const auto* name : {L"GALAXY_AUDIO_WAV_DUMP", L"GALAXY_PRESENT_SNAPSHOT_DIR"}) {
            g_environment[name] = (root / "out" / name).wstring();
            check(test_guard::error(private_run.count(), private_run.data(), environment, player.wstring()) == nullptr,
                  "private output path accepted");
            g_environment[name] = (root / "../outside").wstring();
            check(test_guard::error(private_run.count(), private_run.data(), environment, player.wstring()) != nullptr,
                  "output path outside rejected");
            g_environment.erase(name);
        }
        for (std::size_t value : {2u, 4u, 6u, 8u}) {
            std::vector<std::wstring> moved = private_run.text;
            moved[value] = (root / "../outside").wstring();
            const Args outside(moved);
            check(test_guard::error(outside.count(), outside.data(), environment, player.wstring()) != nullptr,
                  "a folder argument outside rejected");
        }
        g_environment[L"NEBULA_WORK"] = player.wstring();
        check(test_guard::error(private_run.count(), private_run.data(), environment, player.wstring()) != nullptr,
              "a work folder in the player's data rejected");
        g_environment.erase(L"NEBULA_WORK");
        check(test_guard::error(private_run.count(), private_run.data(), environment, player.wstring()) != nullptr,
              "no work folder rejected");
        std::cout << "test data paths: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
