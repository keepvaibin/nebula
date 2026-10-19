#include "galaxy/atomic_file.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace galaxy::io {
namespace {

std::atomic<std::uint64_t> s_temp_sequence{0u};

std::filesystem::path make_sibling_temp_path(
    const std::filesystem::path& destination,
    std::uint64_t sequence) {
    std::wstring name = destination.filename().wstring();
    name += L".tmp.";
    name += std::to_wstring(GetCurrentProcessId());
    name += L".";
    name += std::to_wstring(sequence);
    return destination.parent_path() / name;
}

bool write_all(HANDLE file, std::string_view contents) noexcept {
    std::size_t offset = 0u;
    while (offset < contents.size()) {
        const std::size_t remaining = contents.size() - offset;
        const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
            remaining,
            std::numeric_limits<DWORD>::max()));
        DWORD written = 0u;
        if (WriteFile(
                file,
                contents.data() + offset,
                request,
                &written,
                nullptr) == FALSE ||
            written != request) {
            return false;
        }
        offset += written;
    }
    return true;
}

}  // namespace

bool write_text_file_atomically(
    const std::filesystem::path& destination,
    std::string_view contents) noexcept {
    return detail::write_text_file_atomically_with_collision_hook(
        destination, contents, nullptr, nullptr);
}

bool detail::write_text_file_atomically_with_collision_hook(
    const std::filesystem::path& destination,
    std::string_view contents,
    void (*on_collision)(void*),
    void* user) noexcept {
    std::filesystem::path temporary;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool owns_temporary = false;
    try {
        if (destination.empty() || destination.filename().empty()) {
            return false;
        }

        std::error_code error;
        const std::filesystem::path parent = destination.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, error);
            if (error) {
                return false;
            }
        }

        for (unsigned attempt = 0u; attempt < 64u; ++attempt) {
            const std::uint64_t sequence =
                s_temp_sequence.fetch_add(1u, std::memory_order_relaxed) + 1u;
            temporary = make_sibling_temp_path(destination, sequence);
            file = CreateFileW(
                temporary.c_str(),
                GENERIC_WRITE,
                0u,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_WRITE_THROUGH,
                nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                owns_temporary = true;
                break;
            }
            const DWORD create_error = GetLastError();
            if (create_error != ERROR_FILE_EXISTS &&
                create_error != ERROR_ALREADY_EXISTS) {
                return false;
            }
            if (on_collision != nullptr) {
                on_collision(user);
            }
        }
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }

        bool ready_to_replace = write_all(file, contents);
        if (ready_to_replace && FlushFileBuffers(file) == FALSE) {
            ready_to_replace = false;
        }
        if (CloseHandle(file) == FALSE) {
            ready_to_replace = false;
        }
        file = INVALID_HANDLE_VALUE;

        bool replaced = false;
        if (ready_to_replace) {
            replaced = MoveFileExW(
                           temporary.c_str(),
                           destination.c_str(),
                           MOVEFILE_REPLACE_EXISTING |
                               MOVEFILE_WRITE_THROUGH) != FALSE;
        }
        if (!replaced) {
            (void)DeleteFileW(temporary.c_str());
        }
        return replaced;
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) {
            (void)CloseHandle(file);
        }
        if (owns_temporary) {
            (void)DeleteFileW(temporary.c_str());
        }
        return false;
    }
}

}  // namespace galaxy::io

#else

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <string>

namespace galaxy::io {
namespace {

std::atomic<std::uint64_t> s_temp_sequence{0u};

std::filesystem::path make_sibling_temp_path(
    const std::filesystem::path& destination,
    std::uint64_t sequence) {
    std::string name = destination.filename().string();
    name += ".tmp.";
    name += std::to_string(getpid());
    name += ".";
    name += std::to_string(sequence);
    return destination.parent_path() / name;
}

bool write_all(int file, std::string_view contents) noexcept {
    std::size_t offset = 0u;
    while (offset < contents.size()) {
        const ssize_t written = write(file, contents.data() + offset, contents.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

}  // namespace

bool write_text_file_atomically(
    const std::filesystem::path& destination,
    std::string_view contents) noexcept {
    return detail::write_text_file_atomically_with_collision_hook(
        destination, contents, nullptr, nullptr);
}

bool detail::write_text_file_atomically_with_collision_hook(
    const std::filesystem::path& destination,
    std::string_view contents,
    void (*on_collision)(void*),
    void* user) noexcept {
    std::filesystem::path temporary;
    int file = -1;
    bool owns_temporary = false;
    try {
        if (destination.empty() || destination.filename().empty()) {
            return false;
        }

        std::error_code error;
        const std::filesystem::path parent = destination.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, error);
            if (error) {
                return false;
            }
        }

        for (unsigned attempt = 0u; attempt < 64u; ++attempt) {
            const std::uint64_t sequence =
                s_temp_sequence.fetch_add(1u, std::memory_order_relaxed) + 1u;
            temporary = make_sibling_temp_path(destination, sequence);
            file = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
            if (file >= 0) {
                owns_temporary = true;
                break;
            }
            if (errno != EEXIST) {
                return false;
            }
            if (on_collision != nullptr) {
                on_collision(user);
            }
        }
        if (file < 0) {
            return false;
        }

        bool ready_to_replace = write_all(file, contents) && fsync(file) == 0;
        if (close(file) != 0) {
            ready_to_replace = false;
        }
        file = -1;

        // rename replaces the destination atomically; syncing the folder makes the new name durable.
        const bool replaced = ready_to_replace && rename(temporary.c_str(), destination.c_str()) == 0;
        if (replaced) {
            const int folder = open(parent.empty() ? "." : parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
            if (folder >= 0) {
                (void)fsync(folder);
                (void)close(folder);
            }
        } else {
            (void)unlink(temporary.c_str());
        }
        return replaced;
    } catch (...) {
        if (file >= 0) {
            (void)close(file);
        }
        if (owns_temporary) {
            (void)unlink(temporary.c_str());
        }
        return false;
    }
}

}  // namespace galaxy::io

#endif
