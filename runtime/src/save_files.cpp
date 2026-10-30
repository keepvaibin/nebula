#include "galaxy/save_files.h"

#include "galaxy/atomic_file.h"

#include <fstream>
#include <system_error>

namespace galaxy::saves {

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
        return io::write_text_file_atomically(folder / std::filesystem::path(name), contents) ? kDone : kFailed;
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
