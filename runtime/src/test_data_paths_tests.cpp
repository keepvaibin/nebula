#include "../../tools/test_driver/switches.h"
#include "galaxy/host_env.h"
#include "galaxy/test_data_paths.h"

#include <Windows.h>
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

        _putenv_s("GALAXY_HIDE_WINDOW", "1");
        _wputenv_s(L"NEBULA_WORK", root.c_str());
        const wchar_t* names[] = {L"NEBULA_SETTINGS", L"NEBULA_APPDATA", L"NEBULA_NAND",
                                  L"LOCALAPPDATA", L"GALAXY_RUNTIME_SETTINGS_PATH",
                                  L"GALAXY_RENDER_CONFIG_PATH", L"GALAXY_CRASH_DUMP_DIR"};
        for (const auto* name : names) _wputenv_s(name, (root / name).c_str());
        char program[] = "path-tests", option[] = "--nand";
        std::string saves = (root / "saves").string();
        char* argv[] = {program, option, saves.data()};
        check(!galaxy::host::test_data_paths_error(3, argv), "private run accepted");
        for (const auto* name : names) {
            _wputenv_s(name, L"");
            check(galaxy::host::test_data_paths_error(3, argv), "missing path rejected");
            _wputenv_s(name, (root / name).c_str());
        }
        saves = (root / "../outside").string(); argv[2] = saves.data();
        check(galaxy::host::test_data_paths_error(3, argv), "save argument escape rejected");
        _wputenv_s(L"NEBULA_WORK", L"");
        _putenv_s("GALAXY_HIDE_WINDOW", "0");
        _putenv_s(nebula_switch::test_path_guard_name(), "1");
        check(galaxy::host::test_data_paths_error(1, argv), "test variable activates guard");
        std::cout << "test data paths: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
