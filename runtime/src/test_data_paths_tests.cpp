#include "../../tools/test_driver/switches.h"
#include "galaxy/host_env.h"
#include "galaxy/launch_args.h"
#include "galaxy/test_data_paths.h"

#include "galaxy/windows_api.h"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value, const char* label) {
    if (!value) throw std::runtime_error(label);
}
}

int main() {
    namespace fs = std::filesystem;
    try {
        const fs::path root = L"D:/NebulaWork/port/x/runs/path-policy";
        check(galaxy::host::path_is_within(root / "saves", root), "child path");
        check(!galaxy::host::path_is_within(root / "../outside", root), "parent escape");
        check(!galaxy::host::path_is_within(root.string() + "-other", root), "component boundary");
        check(!galaxy::host::path_is_within("relative", root), "relative path");
        check(galaxy::host::path_is_within(L"d:/nebulawork/port/x/runs/path-policy/settings", root), "Windows case");

        galaxy::g_launch_args.started_hidden = true;
        _wputenv_s(L"NEBULA_WORK", root.c_str());
        const wchar_t* names[] = {L"NEBULA_SETTINGS", L"NEBULA_APPDATA", L"NEBULA_SAVES", L"LOCALAPPDATA"};
        for (const auto* name : names) _wputenv_s(name, (root / name).c_str());
        char program[] = "path-tests", saves_key[] = "--saves", settings_key[] = "--settings",
             crash_key[] = "--crash-dumps", data_key[] = "--data";
        std::string saves = (root / "saves").string(), settings = (root / "settings").string(),
                    crashes = (root / "crashes").string(), data = (root / "data").string();
        char* argv[] = {program, saves_key, saves.data(), settings_key, settings.data(), crash_key, crashes.data(),
                        data_key, data.data()};
        check(!galaxy::host::test_data_paths_error(9, argv), "private run accepted");
        for (const auto* name : names) {
            _wputenv_s(name, L"");
            check(galaxy::host::test_data_paths_error(9, argv), "missing path rejected");
            _wputenv_s(name, (root / name).c_str());
        }
        check(galaxy::host::test_data_paths_error(5, argv), "a run without --crash-dumps rejected");
        const wchar_t* outputs[] = {L"GALAXY_AUDIO_WAV_DUMP", L"GALAXY_PRESENT_SNAPSHOT_DIR"};
        for (const auto* name : outputs) {
            _wputenv_s(name, (root / "out" / name).c_str());
            check(!galaxy::host::test_data_paths_error(9, argv), "private output path accepted");
            _wputenv_s(name, (root / "../outside").c_str());
            check(galaxy::host::test_data_paths_error(9, argv), "output path outside rejected");
            _wputenv_s(name, L"");
        }
        for (int value : {2, 4, 6, 8}) {
            char* const kept = argv[value];
            std::string outside = (root / "../outside").string();
            argv[value] = outside.data();
            check(galaxy::host::test_data_paths_error(9, argv), "a folder argument outside rejected");
            argv[value] = kept;
        }
        _wputenv_s(L"NEBULA_WORK", L"");
        galaxy::g_launch_args.started_hidden = false;
        _putenv_s(nebula_switch::test_path_guard_name(), "1");
        check(galaxy::host::test_data_paths_error(1, argv), "test variable activates guard");
        std::cout << "test data paths: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
