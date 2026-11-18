#include "galaxy/save_files.h"

#include "galaxy/atomic_file.h"

#include <atomic>
#include <cstring>
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

namespace {

std::uint32_t little_u32(const unsigned char* bytes) {
    return std::uint32_t{bytes[0]} | std::uint32_t{bytes[1]} << 8 | std::uint32_t{bytes[2]} << 16 | std::uint32_t{bytes[3]} << 24;
}

// A file that begins with the same tag as the new contents and a higher format number (the word after the tag) was
// written by a newer build: copied to <name>.v<format> before the write, once, so the two backups never rotate it out.
void keep_newer(const std::filesystem::path& path, std::string_view contents) noexcept {
    try {
        if (contents.size() < 8) {
            return;
        }
        unsigned char head[8] = {};
        std::ifstream in(path, std::ios::binary);
        in.read(reinterpret_cast<char*>(head), sizeof(head));
        if (!in || std::memcmp(head, contents.data(), 4) != 0) {
            return;
        }
        const std::uint32_t had = little_u32(head + 4);
        if (had <= little_u32(reinterpret_cast<const unsigned char*>(contents.data()) + 4)) {
            return;
        }
        in.close();
        std::filesystem::path kept = path;
        kept += ".v" + std::to_string(had);
        std::error_code error;
        if (!std::filesystem::exists(kept, error)) {
            std::filesystem::copy_file(path, kept, error);
        }
    } catch (...) {
    }
}

}  // namespace

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
            keep_newer(path, contents);
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

namespace {

std::int32_t load_status(const std::int32_t* status) noexcept {
#if defined(__cpp_lib_atomic_ref)
    // atomic_ref of a const type is C++26 (P3323), which Apple's libc++ does not build; the load writes nothing.
    return std::atomic_ref<std::int32_t>(*const_cast< std::int32_t* >(status)).load(std::memory_order_acquire);
#else  // libc++ without atomic_ref (Android NDK r28): the compiler's builtin, the same acquire load.
    return __atomic_load_n(status, __ATOMIC_ACQUIRE);
#endif
}

}  // namespace

void StatusBoard::store(std::int32_t* status, std::int32_t value) noexcept {
#if defined(__cpp_lib_atomic_ref)
    std::atomic_ref<std::int32_t>(*status).store(value, std::memory_order_release);
#else
    __atomic_store_n(status, value, __ATOMIC_RELEASE);
#endif
    // Taken after the store: a waiter that saw the old value under the mutex is asleep before this notify.
    { const std::lock_guard<std::mutex> lock(mutex_); }
    stored_.notify_all();
}

bool StatusBoard::wait(const std::int32_t* status, std::int32_t pending) noexcept {
    if (load_status(status) != pending) {
        return false;
    }
    std::unique_lock<std::mutex> lock(mutex_);
    stored_.wait(lock, [status, pending] { return load_status(status) != pending; });
    return true;
}

}  // namespace galaxy::saves
