#include "galaxy/asset_store.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace galaxy::assets {

namespace {

constexpr std::size_t kHeader = 64;

std::uint64_t word(const std::uint8_t* at) {
    std::uint64_t value;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

bool is_archive_name(std::string_view part) {
    if (part.size() <= 4u) return false;
    std::string ending(part.substr(part.size() - 4u));
    for (char& c : ending) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ending == ".arc" || ending == ".szs";
}

bool same_letters(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

}  // namespace

std::string store_path(std::string_view path) {
    const std::size_t root = path.find_first_not_of('/');
    std::string out(path.substr(0, root == std::string_view::npos ? path.size() : root));
    if (root == std::string_view::npos) return out;
    std::vector<std::string_view> parts;
    for (std::string_view rest = path.substr(root);;) {
        const std::size_t slash = rest.find('/');
        parts.push_back(rest.substr(0, slash));
        if (slash == std::string_view::npos) break;
        rest.remove_prefix(slash + 1u);
    }
    const bool stage_folder = parts.size() > 2u && same_letters(parts[0], "StageData") && !is_archive_name(parts[1]) &&
                              is_archive_name(parts[2]);
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i == 1u && stage_folder) continue;
        if (i != 0u) out.push_back('/');
        const std::string_view part = parts[i];
        out.append(is_archive_name(part) ? part.substr(0, part.size() - 4u) : part);
    }
    return out;
}

AssetStore::~AssetStore() {
    close();
}

namespace {
// A language folder's name: a release ("Us", "Eu", "Jp", "Kr", "Cn", "Tw") then a capitalised word ("UsEnglish").
bool is_language_folder(std::string_view name) {
    static constexpr std::string_view kRegions[] = {"Us", "Eu", "Jp", "Kr", "Cn", "Tw"};
    for (const std::string_view region : kRegions) {
        const std::string_view rest = name.substr(std::min(name.size(), region.size()));
        if (name.starts_with(region) && !rest.empty() && std::isupper(static_cast<unsigned char>(rest[0])) != 0 &&
            std::all_of(rest.begin(), rest.end(), [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; })) {
            return true;
        }
    }
    return false;
}
}  // namespace

bool AssetStore::attach_overlay(const std::filesystem::path& path) {
    // Over the last one attached: the newest wins.
    if (overlay_ != nullptr) return overlay_->attach_overlay(path);
    auto overlay = std::make_unique<AssetStore>();
    if (!overlay->open(path)) return false;
    overlay_ = std::move(overlay);
    language_fallback_.clear();
    for (const std::string& folder : language_folders()) {
        if (!find_own(folder)) continue;
        if (language_fallback_.empty()) language_fallback_ = folder;
        if (folder.ends_with("English")) {
            language_fallback_ = folder;
            break;
        }
    }
    return true;
}

void AssetStore::close() {
    overlay_.reset();
#if defined(_WIN32)
    if (view_ != nullptr) {
        UnmapViewOfFile(view_);
    }
    if (mapping_ != nullptr) {
        CloseHandle(mapping_);
        mapping_ = nullptr;
    }
    if (file_ != nullptr) {
        CloseHandle(file_);
        file_ = nullptr;
    }
#else
    if (view_ != nullptr) {
        munmap(const_cast<std::uint8_t*>(view_), static_cast<std::size_t>(size_));
    }
    if (file_ >= 0) {
        ::close(file_);
        file_ = -1;
    }
#endif
    index_.clear();
    image_ = nullptr;
    view_ = nullptr;
    size_ = 0;
    nodes_ = nullptr;
    hashes_ = nullptr;
    names_ = nullptr;
    count_ = 0;
    game_id_.clear();
    revision_ = 0;
}

bool AssetStore::parse(const std::uint8_t* header, const std::uint8_t* index, std::uint64_t index_at, std::uint64_t size) {
    std::uint32_t version, count;
    std::memcpy(&version, header + 8, 4);
    std::memcpy(&count, header + 12, 4);
    const std::uint64_t nodes = word(header + 16), hashes = word(header + 24), names = word(header + 32),
                        names_size = word(header + 40);
    if (version != kStoreVersion || count == 0u || nodes < index_at || nodes > size || (size - nodes) / sizeof(Node) < count ||
        hashes < index_at || hashes > size || (size - hashes) / sizeof(Hash) < count || names < index_at || names > size ||
        size - names < names_size || names_size == 0u || index[names - index_at + names_size - 1u] != 0u) {
        return false;
    }
    const auto* const node_table = reinterpret_cast<const Node*>(index + (nodes - index_at));
    const auto* const hash_table = reinterpret_cast<const Hash*>(index + (hashes - index_at));
    // Every index the table holds is inside it, so a damaged file is refused here and nothing below checks again (read checks each
    // read against the store's size).
    for (std::uint32_t i = 0u; i < count; ++i) {
        const Node& node = node_table[i];
        const bool folder = (node.flags & 1u) != 0u;
        if (node.name >= names_size || node.parent >= count || hash_table[i].node >= count ||
            (folder && (node.first > count || node.children > count - node.first))) {
            return false;
        }
    }
    nodes_ = node_table;
    hashes_ = hash_table;
    names_ = reinterpret_cast<const char*>(index + (names - index_at));
    count_ = count;
    game_id_.assign(reinterpret_cast<const char*>(header + 48), strnlen(reinterpret_cast<const char*>(header + 48), 6));
    revision_ = header[55];
    return true;
}

bool AssetStore::load(const std::uint8_t* data, std::size_t size) {
    close();
    if (data == nullptr || size < kHeader || std::memcmp(data, kStoreMagic, sizeof(kStoreMagic)) != 0 ||
        !parse(data, data, 0, size)) {
        close();
        return false;
    }
    image_ = data;
    size_ = size;
    return true;
}

bool AssetStore::open(const std::filesystem::path& path) {
    close();
#if defined(_WIN32)
    file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    LARGE_INTEGER length{};
    if (file_ == INVALID_HANDLE_VALUE || !GetFileSizeEx(file_, &length)) {
        file_ = nullptr;
        return false;
    }
    size_ = static_cast<std::uint64_t>(length.QuadPart);
#else
    file_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    const off_t length = file_ >= 0 ? lseek(file_, 0, SEEK_END) : -1;
    if (length < 0) {
        close();
        return false;
    }
    size_ = static_cast<std::uint64_t>(length);
#endif
    std::uint8_t header[kHeader];
    if (size_ < kHeader || !read(0, sizeof(header), header) || std::memcmp(header, kStoreMagic, sizeof(kStoreMagic)) != 0) {
        close();
        return false;
    }
    // The index is the store's end: nodes, hashes, names.
    const std::uint64_t index_at = word(header + 16);
    if (index_at >= size_ || size_ - index_at > (1ull << 30)) {
        close();
        return false;
    }
    index_.resize(static_cast<std::size_t>(size_ - index_at));
    if (!read(index_at, index_.size(), index_.data()) || !parse(header, index_.data(), index_at, size_)) {
        close();
        return false;
    }
    // Address space only: pages come in as they are used and are the file's, so they cost no commit and the system can drop them.
#if defined(_WIN32)
    mapping_ = CreateFileMappingW(file_, nullptr, PAGE_WRITECOPY, 0, 0, nullptr);
    view_ = mapping_ != nullptr ? static_cast<const std::uint8_t*>(MapViewOfFile(mapping_, FILE_MAP_READ, 0, 0, 0)) : nullptr;
#else
    void* const mapped = mmap(nullptr, static_cast<std::size_t>(size_), PROT_READ, MAP_PRIVATE, file_, 0);
    view_ = mapped != MAP_FAILED ? static_cast<const std::uint8_t*>(mapped) : nullptr;
#endif
    if (view_ == nullptr) {
        close();
        return false;
    }
    return true;
}

const std::uint8_t* AssetStore::map(std::uint64_t position, std::size_t size) const {
    if (position >= kOverlayPosition) {
        return overlay_ != nullptr ? overlay_->map(position - kOverlayPosition, size) : nullptr;
    }
    if (position > size_ || size_ - position < size) {
        return nullptr;
    }
    if (image_ != nullptr) {
        return image_ + position;
    }
    if (view_ == nullptr) {
        return nullptr;
    }
    const std::uint8_t* const data = view_ + position;
#if defined(_WIN32)
    WIN32_MEMORY_RANGE_ENTRY range{const_cast<std::uint8_t*>(data), size};
    PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
#else
    const std::uintptr_t page = reinterpret_cast<std::uintptr_t>(data) & ~std::uintptr_t{4095};
    madvise(reinterpret_cast<void*>(page), size + (reinterpret_cast<std::uintptr_t>(data) - page), MADV_WILLNEED);
#endif
    return data;
}

// A view starts where the system's granularity allows, at most 64 KB before the file; the bytes before it are the store's.
// Where a view of the store can start: 64 KB on Windows, a page elsewhere (where mmap returns a page, whatever the file
// offset). map_private and unmap_private round by the same value, as unmap_private has only the address to go on.
static std::uint64_t view_granularity() noexcept {
#if defined(_WIN32)
    return 0x10000u;
#else
    static const auto page = static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
    return page;
#endif
}

std::uint8_t* AssetStore::map_private(std::uint64_t position, std::size_t size) const {
    if (position >= kOverlayPosition) {
        return overlay_ != nullptr ? overlay_->map_private(position - kOverlayPosition, size) : nullptr;
    }
    const std::uint64_t granularity = view_granularity();
    if (view_ == nullptr || position > size_ || size_ - position < size) {
        return nullptr;
    }
    const std::uint64_t start = position & ~(granularity - 1u);
    const auto length = static_cast<std::size_t>(position + size - start);
#if defined(_WIN32)
    void* const base = MapViewOfFile(mapping_, FILE_MAP_COPY, static_cast<DWORD>(start >> 32u), static_cast<DWORD>(start), length);
    if (base == nullptr) {
        return nullptr;
    }
    WIN32_MEMORY_RANGE_ENTRY range{base, length};
    PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
#else
    void* const base = mmap(nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, file_, static_cast<off_t>(start));
    if (base == MAP_FAILED) {
        return nullptr;
    }
    madvise(base, length, MADV_WILLNEED);
#endif
    return static_cast<std::uint8_t*>(base) + (position - start);
}

void AssetStore::unmap_private(void* data, std::size_t size) const {
    const auto granularity = static_cast<std::uintptr_t>(view_granularity());
    const auto at = reinterpret_cast<std::uintptr_t>(data);
    const std::uintptr_t base = at & ~(granularity - 1u);
#if defined(_WIN32)
    (void)size;
    UnmapViewOfFile(reinterpret_cast<void*>(base));
#else
    munmap(reinterpret_cast<void*>(base), size + (at - base));
#endif
}

bool AssetStore::read(std::uint64_t position, std::size_t size, void* dst) const {
    if (position >= kOverlayPosition) {
        return overlay_ != nullptr && overlay_->read(position - kOverlayPosition, size, dst);
    }
    if (position > size_ || size_ - position < size) {
        return false;
    }
    if (image_ != nullptr) {
        std::memcpy(dst, image_ + position, size);
        return true;
    }
#if defined(_WIN32)
    // One event a thread, so reads of different threads run at once. The readers are a few long-lived threads, so
    // the events are not closed.
    thread_local const HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    auto* out = static_cast<std::uint8_t*>(dst);
    while (size != 0) {
        OVERLAPPED overlapped{};
        overlapped.Offset = static_cast<DWORD>(position);
        overlapped.OffsetHigh = static_cast<DWORD>(position >> 32);
        overlapped.hEvent = done;
        const DWORD wanted = static_cast<DWORD>(std::min<std::size_t>(size, 1u << 30));
        DWORD got = 0;
        if (!ReadFile(file_, out, wanted, nullptr, &overlapped) && GetLastError() != ERROR_IO_PENDING) {
            return false;
        }
        if (!GetOverlappedResult(file_, &overlapped, &got, TRUE) || got == 0) {
            return false;
        }
        out += got;
        position += got;
        size -= got;
    }
#else
    auto* out = static_cast<std::uint8_t*>(dst);
    while (size != 0) {
        const ssize_t got = pread(file_, out, size, static_cast<off_t>(position));
        if (got <= 0) {
            return false;
        }
        out += got;
        position += static_cast<std::uint64_t>(got);
        size -= static_cast<std::size_t>(got);
    }
#endif
    return true;
}

std::optional<Entry> AssetStore::entry(std::int32_t index) const {
    if (overlay_ != nullptr && index >= 0 && static_cast<std::uint32_t>(index) >= count_) {
        std::optional<Entry> found = overlay_->entry(index - static_cast<std::int32_t>(count_));
        if (found) {
            found->index += static_cast<std::int32_t>(count_);
            found->position += kOverlayPosition;
            found->from_overlay = true;
        }
        return found;
    }
    if (index < 0 || static_cast<std::uint32_t>(index) >= count_) {
        return std::nullopt;
    }
    const Node& node = nodes_[index];
    Entry out;
    out.index = index;
    out.folder = (node.flags & 1u) != 0u;
    out.archive = (node.flags & 2u) != 0u;
    out.kind = static_cast<std::uint8_t>(node.flags >> 8);
    out.name = names_ + node.name;
    out.length = node.size;
    out.position = node.position;
    out.children = out.folder ? node.children : 0u;
    return out;
}

std::optional<Entry> AssetStore::child(std::int32_t folder, std::uint32_t index) const {
    if (overlay_ != nullptr && folder >= 0 && static_cast<std::uint32_t>(folder) >= count_) {
        std::optional<Entry> found = overlay_->child(folder - static_cast<std::int32_t>(count_), index);
        if (found) {
            found->index += static_cast<std::int32_t>(count_);
            found->position += kOverlayPosition;
            found->from_overlay = true;
        }
        return found;
    }
    if (folder < 0 || static_cast<std::uint32_t>(folder) >= count_ || (nodes_[folder].flags & 1u) == 0u ||
        index >= nodes_[folder].children) {
        return std::nullopt;
    }
    return entry(static_cast<std::int32_t>(nodes_[folder].first + index));
}

std::optional<Entry> AssetStore::added_child(std::string_view path, std::uint32_t index) const {
    if (overlay_ == nullptr || !find_own(path)) {
        return std::nullopt;
    }
    const std::optional<Entry> folder = overlay_->find_own(path);
    if (!folder || !folder->folder) {
        return std::nullopt;
    }
    std::string base(path);
    if (base.empty() || base.back() != '/') base += '/';
    std::uint32_t added = 0u;
    for (std::uint32_t i = 0u; i < folder->children; ++i) {
        std::optional<Entry> found = overlay_->child(folder->index, i);
        if (!found || find_own(base + (found->name != nullptr ? found->name : ""))) continue;
        if (added++ == index) {
            found->index += static_cast<std::int32_t>(count_);
            found->position += kOverlayPosition;
            found->from_overlay = true;
            return found;
        }
    }
    return std::nullopt;
}

std::optional<Entry> AssetStore::find(std::string_view path) const {
    std::optional<Entry> found = find_merged(path);
    if (found || language_fallback_.empty()) {
        return found;
    }
    // A language a mod adds (German on a US disc) without this file: the game's own language has it.
    const std::string_view rest = path.substr(path.find_first_not_of('/') == std::string_view::npos ? path.size() : path.find_first_not_of('/'));
    const std::size_t slash = rest.find('/');
    const std::string_view folder = rest.substr(0, slash == std::string_view::npos ? rest.size() : slash);
    if (slash == std::string_view::npos || !is_language_folder(folder) || find_own(folder) || !overlay_->find(folder)) {
        return std::nullopt;
    }
    return find_merged(language_fallback_ + std::string(rest.substr(slash)));
}

std::optional<Entry> AssetStore::find_merged(std::string_view path) const {
    if (overlay_ != nullptr) {
        // A file or an archive of the mods first. A plain folder of theirs only lists what they change: the game's
        // stands, unless the game has none of that name.
        std::optional<Entry> found = overlay_->find(path);
        if (found) {
            if (!found->folder || found->archive || !find_own(path)) {
                found->index += static_cast<std::int32_t>(count_);
                found->position += kOverlayPosition;
                found->from_overlay = true;
                return found;
            }
        }
    }
    return find_own(path);
}

std::optional<Entry> AssetStore::find_own(std::string_view path) const {
    if (count_ == 0u) {
        return std::nullopt;
    }
    // The path from the root: "." and "" steps dropped, ".." going up.
    std::array<std::string_view, 16> parts;
    std::size_t depth = 0;
    const bool folder_only = path.size() > 1 && path.back() == '/';
    while (!path.empty()) {
        const std::size_t slash = path.find('/');
        const std::string_view part = path.substr(0, slash);
        path = slash == std::string_view::npos ? std::string_view{} : path.substr(slash + 1);
        if (part.empty()) {
            depth = 0;
            continue;
        }
        if (part == ".") {
            continue;
        }
        if (part == "..") {
            depth -= depth != 0 ? 1 : 0;
            continue;
        }
        if (depth == parts.size()) {
            return std::nullopt;
        }
        parts[depth++] = part;
    }
    std::uint64_t hash = 0xCBF29CE484222325ull;
    const auto add = [&hash](char c) {
        const char lower = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        hash = (hash ^ static_cast<std::uint8_t>(lower)) * 0x100000001B3ull;
    };
    for (std::size_t i = 0; i < depth; i++) {
        if (i != 0) {
            add('/');
        }
        for (const char c : parts[i]) {
            add(c);
        }
    }
    const Hash* const end = hashes_ + count_;
    const Hash* const found =
        std::lower_bound(hashes_, end, hash, [](const Hash& item, std::uint64_t value) { return item.hash < value; });
    if (found == end || found->hash != hash) {
        return std::nullopt;
    }
    if (folder_only && (nodes_[found->node].flags & 1u) == 0u) {
        return std::nullopt;
    }
    return entry(static_cast<std::int32_t>(found->node));
}

std::vector<std::string> AssetStore::language_folders() const {
    std::vector<std::string> folders;
    for (std::uint32_t i = 0;; i++) {
        const std::optional<Entry> item = child(0, i);
        if (!item) {
            break;
        }
        if (item->folder && is_language_folder(item->name)) {
            folders.emplace_back(item->name);
        }
    }
    if (overlay_ != nullptr) {
        for (std::string& added : overlay_->language_folders()) {
            if (std::find(folders.begin(), folders.end(), added) == folders.end()) folders.push_back(std::move(added));
        }
    }
    return folders;
}

}  // namespace galaxy::assets
