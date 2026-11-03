#include "galaxy/save_files.h"

#include "galaxy/atomic_file.h"

#include <fstream>
#include <string>
#include <system_error>

namespace galaxy::saves {

namespace {

bool has_version(std::string_view save, std::string_view word) noexcept {
    return save.size() >= 8 && save.substr(4, 4) == word;
}

// False when an old save that had to be kept could not be.
bool keep_old_save(const std::filesystem::path& path, std::string_view name, std::string_view contents) {
    constexpr std::string_view kNative("\x03\0\0\0", 4), kOld("\0\0\0\x02", 4);
    if (name != "GameData.bin" || !has_version(contents, kNative)) {
        return true;
    }
    std::filesystem::path kept = path;
    kept += ".v2";
    std::error_code error;
    if (std::filesystem::exists(kept, error) || !std::filesystem::is_regular_file(path, error)) {
        return true;
    }
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        return false;
    }
    std::string old(static_cast<std::size_t>(size), '\0');
    std::ifstream in(path, std::ios::binary);
    in.read(old.data(), static_cast<std::streamsize>(size));
    if (!in) {
        return false;
    }
    return !has_version(old, kOld) || io::write_file_atomically(kept, old);
}

}  // namespace

bool is_plain_name(std::string_view name) noexcept {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string_view::npos;
}

Result read(const std::filesystem::path& folder, std::string_view name, void* buffer, std::uint32_t capacity,
            std::uint32_t* length) noexcept {
    try {
        if (!is_plain_name(name)) {
            return kFailed;
        }
        const std::filesystem::path path = folder / std::filesystem::path(name);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            return kNoFile;
        }
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error || size > UINT32_MAX) {
            return kFailed;
        }
        if (length != nullptr) {
            *length = static_cast<std::uint32_t>(size);
        }
        if (size > capacity) {
            return kTooLarge;
        }
        std::ifstream in(path, std::ios::binary);
        in.read(static_cast<char*>(buffer), static_cast<std::streamsize>(size));
        return in && static_cast<std::uintmax_t>(in.gcount()) == size ? kDone : kFailed;
    } catch (...) {
        return kFailed;
    }
}

Result write(const std::filesystem::path& folder, std::string_view name, const void* data, std::uint32_t size) noexcept {
    try {
        if (!is_plain_name(name)) {
            return kFailed;
        }
        const std::string_view contents(static_cast<const char*>(data), size);
        const std::filesystem::path path = folder / std::filesystem::path(name);
        if (!keep_old_save(path, name, contents)) {
            return kFailed;
        }
        // The last two saves stay beside it, for a player whose newest file is damaged.
        std::error_code error;
        if (std::filesystem::is_regular_file(path, error)) {
            std::filesystem::path first = path;
            first += ".1";
            std::filesystem::path second = path;
            second += ".2";
            if (std::filesystem::is_regular_file(first, error)) {
                std::filesystem::rename(first, second, error);
            }
            std::filesystem::copy_file(path, first, std::filesystem::copy_options::overwrite_existing, error);
        }
        return io::write_file_atomically(path, contents) ? kDone : kFailed;
    } catch (...) {
        return kFailed;
    }
}

Result remove(const std::filesystem::path& folder, std::string_view name) noexcept {
    try {
        if (!is_plain_name(name)) {
            return kFailed;
        }
        const std::filesystem::path path = folder / std::filesystem::path(name);
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error)) {
            return kNoFile;
        }
        return std::filesystem::remove(path, error) && !error ? kDone : kFailed;
    } catch (...) {
        return kFailed;
    }
}

}  // namespace galaxy::saves
