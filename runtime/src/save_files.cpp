#include "galaxy/save_files.h"

#include "galaxy/atomic_file.h"

#include <fstream>
#include <string>
#include <system_error>

namespace galaxy::saves {

bool is_plain_name(std::string_view name) noexcept {
    return !name.empty() && name != "." && name != ".." && name.find_first_of("/\\:") == std::string_view::npos;
}

std::string_view mod_file_name(std::string_view request_name) noexcept {
    if (request_name.size() <= kModFolder.size() + 1u || !request_name.starts_with(kModFolder) ||
        request_name[kModFolder.size()] != '/') {
        return {};
    }
    const std::string_view name = request_name.substr(kModFolder.size() + 1u);
    return is_plain_name(name) ? name : std::string_view{};
}

std::string_view replaced_file_name(std::string_view request_name) noexcept {
    const std::size_t head = kModFolder.size() + 1u + kReplacedFolder.size() + 1u;
    if (request_name.size() <= head || !request_name.starts_with(kModFolder) || request_name[kModFolder.size()] != '/' ||
        request_name.substr(kModFolder.size() + 1u, kReplacedFolder.size()) != kReplacedFolder ||
        request_name[head - 1u] != '/') {
        return {};
    }
    const std::string_view name = request_name.substr(head);
    return is_plain_name(name) ? name : std::string_view{};
}

SaveFolders save_folders(std::string_view code) {
    std::string hex;
    for (const char c : code) {
        const unsigned byte = static_cast<unsigned char>(c);
        hex.push_back("0123456789abcdef"[byte >> 4u]);
        hex.push_back("0123456789abcdef"[byte & 15u]);
    }
    return {"/" + std::string(code), "/title/00010000/" + hex + "/data"};
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

Result write(const std::filesystem::path& folder, std::string_view name, const void* data, std::uint32_t size,
             Backups backups) noexcept {
    try {
        if (!is_plain_name(name)) {
            return kFailed;
        }
        const std::string_view contents(static_cast<const char*>(data), size);
        const std::filesystem::path path = folder / std::filesystem::path(name);
        // The last two saves stay beside it, for a player whose newest file is damaged.
        std::error_code error;
        if (backups == Backups::kKeep && std::filesystem::is_regular_file(path, error)) {
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
