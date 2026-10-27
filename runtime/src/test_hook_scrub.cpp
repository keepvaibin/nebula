// A release runtime removes the test hooks' variables (galaxy/test_hooks.h) from its environment before any of its
// own initializers run (the library initialization segment comes before the program's), so no variable set by hand or
// by another program can script input, teleport, tour the galaxies, fault on purpose or write picture snapshots.
// GALAXY_INPUT_MODE is the launcher's input setting, not a hook.
#include "galaxy/test_hooks.h"

#if !(defined(GALAXY_TEST_HOOKS) && GALAXY_TEST_HOOKS)

#include <windows.h>

#include <cstdlib>
#include <cwchar>
#include <string>
#include <vector>

#pragma warning(disable : 4073)  // initializers in the library initialization area, on purpose
#pragma init_seg(lib)

namespace {

bool test_hook_variable(const std::wstring& name) noexcept {
    static constexpr const wchar_t* kPrefixes[] = {
        L"GALAXY_TEST_", L"GALAXY_INPUT_", L"GALAXY_PRESENT_SNAPSHOT_", L"GALAXY_INTERP_SNAPSHOT_",
        L"GALAXY_GX_CAPTURE_"};
    if (_wcsicmp(name.c_str(), L"GALAXY_INPUT_MODE") == 0) {
        return false;
    }
    for (const wchar_t* prefix : kPrefixes) {
        if (_wcsnicmp(name.c_str(), prefix, std::wcslen(prefix)) == 0) {
            return true;
        }
    }
    return false;
}

struct RemoveTestHookVariables {
    RemoveTestHookVariables() noexcept {
        try {
            std::vector<std::wstring> names;
            if (wchar_t* block = GetEnvironmentStringsW()) {
                for (const wchar_t* entry = block; *entry != L'\0'; entry += std::wcslen(entry) + 1u) {
                    // A leading '=' belongs to a drive's directory entry (=C:=C:\...).
                    const wchar_t* equals = std::wcschr(entry + 1, L'=');
                    if (equals != nullptr) {
                        std::wstring name(entry, equals);
                        if (test_hook_variable(name)) {
                            names.push_back(std::move(name));
                        }
                    }
                }
                FreeEnvironmentStringsW(block);
            }
            for (const std::wstring& name : names) {
                // An empty value removes the variable from the C runtime's copies and the process environment.
                _wputenv_s(name.c_str(), L"");
                SetEnvironmentVariableW(name.c_str(), nullptr);
            }
        } catch (...) {
        }
    }
};

const RemoveTestHookVariables s_remove_test_hook_variables;

}  // namespace

#endif
