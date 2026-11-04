// SPDX-License-Identifier: GPL-3.0-only
// xess_library.h.
#include "galaxy/render/xess_library.h"

#include "galaxy/platform_paths.h"
#include "galaxy/run_options.h"

#include <SoftPub.h>
#include <WinTrust.h>

#include <cstdlib>
#include <iostream>
#include <mutex>

#pragma comment(lib, "wintrust.lib")

namespace galaxy::render::xess_library {

namespace {

// An Authenticode signature chaining to a root Windows trusts.
bool signature_trusted(const wchar_t* path) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path;
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    (void)WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return result == ERROR_SUCCESS;
}

}  // namespace

std::wstring directory() {
    if (const wchar_t* const test = ::galaxy::run_options().xess_dir_w; test != nullptr && test[0] != L'\0') {
        return test;
    }
    const std::filesystem::path data = galaxy::nebula_data_directory();
    return data.empty() ? std::wstring{} : (data / L"vendor" / L"xess-3.0.2").wstring();
}

HMODULE module() noexcept {
    static std::once_flag once;
    static HMODULE library = nullptr;
    std::call_once(once, [] {
        const std::wstring folder = directory();
        if (folder.empty()) {
            return;
        }
        const std::wstring path = folder + L"\\libxess.dll";
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            return;
        }
        if (!signature_trusted(path.c_str())) {
            std::cerr << "[xess] libxess.dll is not signed: not loaded\n";
            return;
        }
        library = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (library == nullptr) {
            std::cerr << "[xess] libxess.dll did not load\n";
        }
    });
    return library;
}

}  // namespace galaxy::render::xess_library
