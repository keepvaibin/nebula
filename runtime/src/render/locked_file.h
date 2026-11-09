// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// A vendor library held against writes and deletes from before its signature is checked until it is loaded, so the file
// that was checked is the file that loads (LoadLibrary takes a path, not a handle). Readers, including the signature check
// and the loader, are not affected.

#include "galaxy/windows_api.h"

namespace galaxy::render {

class LockedFile {
public:
    explicit LockedFile(const wchar_t* path) noexcept
        : handle_(CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {}
    ~LockedFile() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            CloseHandle(handle_);
        }
    }
    LockedFile(const LockedFile&) = delete;
    LockedFile& operator=(const LockedFile&) = delete;

    [[nodiscard]] bool held() const noexcept { return handle_ != INVALID_HANDLE_VALUE; }

private:
    HANDLE handle_;
};

}  // namespace galaxy::render
