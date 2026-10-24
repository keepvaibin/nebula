// SPDX-License-Identifier: GPL-3.0-only
// texture_pack.cpp: the texture-pack index, loaders, resident replacements and dump writer (texture_pack.h).

#include "galaxy/gx/texture_pack.h"

#include "galaxy/crash_report.h"

#include "galaxy/gx/rt_materials.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/windows_power.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <system_error>

namespace galaxy::gx {

bool texture_format_palettized(TexFormat format) noexcept {
    return format == TexFormat::C4 || format == TexFormat::C8 || format == TexFormat::C14X2;
}

std::uint64_t texture_pack_tlut_hash(TexFormat format, const std::uint8_t* base_level, std::uint32_t width,
                                     std::uint32_t height, const std::uint8_t* palette) noexcept {
    if (!texture_format_palettized(format) || base_level == nullptr || palette == nullptr) {
        return 0u;
    }
    const std::size_t bytes = texture_guest_byte_size(format, width, height, 1u);
    std::uint32_t lo = 0xFFFFu;
    std::uint32_t hi = 0u;
    if (format == TexFormat::C4) {
        for (std::size_t i = 0; i < bytes; ++i) {
            const std::uint32_t a = base_level[i] >> 4u;
            const std::uint32_t b = base_level[i] & 0xFu;
            lo = std::min({lo, a, b});
            hi = std::max({hi, a, b});
        }
    } else if (format == TexFormat::C8) {
        for (std::size_t i = 0; i < bytes; ++i) {
            lo = std::min<std::uint32_t>(lo, base_level[i]);
            hi = std::max<std::uint32_t>(hi, base_level[i]);
        }
    } else {
        for (std::size_t i = 0; i + 1u < bytes; i += 2u) {
            const std::uint32_t v = ((static_cast<std::uint32_t>(base_level[i]) << 8u) | base_level[i + 1u]) & 0x3FFFu;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
    }
    if (lo > hi) {
        return 0u;
    }
    return texture_pack_hash(palette + lo * 2u, static_cast<std::size_t>(hi - lo + 1u) * 2u);
}

namespace {

bool parse_hex64(std::string_view text, std::uint64_t& out) noexcept {
    if (text.size() != 16u) return false;
    std::uint64_t value = 0;
    for (const char c : text) {
        value <<= 4u;
        if (c >= '0' && c <= '9') value |= static_cast<std::uint64_t>(c - '0');
        else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint64_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint64_t>(c - 'A' + 10);
        else return false;
    }
    out = value;
    return true;
}

bool parse_uint(std::string_view text, std::uint32_t limit, std::uint32_t& out) noexcept {
    if (text.empty() || text.size() > 5u) return false;
    std::uint32_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10u + static_cast<std::uint32_t>(c - '0');
    }
    if (value > limit) return false;
    out = value;
    return true;
}

bool known_format(std::uint32_t format) noexcept {
    return format <= 6u || format == 8u || format == 9u || format == 10u || format == 14u;
}

}  // namespace

std::optional<TexturePackName> parse_texture_pack_name(std::string_view stem) noexcept {
    constexpr std::string_view kPrefix = "tex1_";
    if (stem.size() < kPrefix.size() || stem.substr(0, kPrefix.size()) != kPrefix) return std::nullopt;
    std::string_view rest = stem.substr(kPrefix.size());
    std::array<std::string_view, 8> parts{};
    std::size_t count = 0;
    while (!rest.empty()) {
        if (count == parts.size()) return std::nullopt;
        const std::size_t cut = rest.find('_');
        parts[count++] = rest.substr(0, cut);
        if (cut == std::string_view::npos) break;
        rest = rest.substr(cut + 1u);
    }
    if (count == 0u) return std::nullopt;
    TexturePackName name{};
    std::size_t at = 0;
    {
        const std::string_view size = parts[at++];
        const std::size_t x = size.find('x');
        std::uint32_t w = 0, h = 0;
        if (x == std::string_view::npos || !parse_uint(size.substr(0, x), 1024u, w) ||
            !parse_uint(size.substr(x + 1u), 1024u, h) || w == 0u || h == 0u) {
            return std::nullopt;
        }
        name.width = static_cast<std::uint16_t>(w);
        name.height = static_cast<std::uint16_t>(h);
    }
    if (at < count && parts[at] == "m") {
        name.mipmapped = true;
        ++at;
    }
    if (at >= count || !parse_hex64(parts[at++], name.hash)) return std::nullopt;
    if (at < count && parts[at] == "$") {
        name.any_tlut = true;
        ++at;
    } else if (at < count && parts[at].size() == 16u) {
        if (!parse_hex64(parts[at++], name.tlut)) return std::nullopt;
    }
    std::uint32_t format = 0;
    if (at >= count) return std::nullopt;
    if (const std::string_view token = parts[at++]; !parse_uint(token, 255u, format)) {
        // A letter typed after the format number ("_14v", a slip in some packs): the number before it. The hash still
        // has to match the game's texture.
        std::size_t digits = 0;
        while (digits < token.size() && token[digits] >= '0' && token[digits] <= '9') ++digits;
        bool letters = digits != 0u && digits < token.size();
        for (std::size_t i = digits; letters && i < token.size(); ++i) {
            const char c = token[i];
            letters = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }
        if (!letters || !parse_uint(token.substr(0, digits), 255u, format)) return std::nullopt;
    }
    if (!known_format(format)) return std::nullopt;
    name.format = static_cast<std::uint8_t>(format);
    if (!texture_format_palettized(static_cast<TexFormat>(format)) && (name.any_tlut || name.tlut != 0u)) {
        return std::nullopt;
    }
    if (at < count && parts[at] == "arb") {
        name.arbitrary = true;
        ++at;
    }
    if (at < count) {
        const std::string_view tail = parts[at++];
        std::uint32_t level = 0;
        if (tail.size() > 3u && tail.substr(0, 3u) == "mip" && parse_uint(tail.substr(3u), 15u, level) &&
            level != 0u) {
            name.level = static_cast<std::uint8_t>(level);
        } else {
            return std::nullopt;
        }
    }
    if (at != count) return std::nullopt;
    return name;
}

std::string texture_pack_name(const TexturePackName& name) {
    char text[96]{};
    int length = std::snprintf(text, sizeof(text), "tex1_%ux%u%s_%016llx", name.width, name.height,
                               name.mipmapped ? "_m" : "", static_cast<unsigned long long>(name.hash));
    if (texture_format_palettized(static_cast<TexFormat>(name.format))) {
        length += name.any_tlut ? std::snprintf(text + length, sizeof(text) - static_cast<std::size_t>(length), "_$")
                                : std::snprintf(text + length, sizeof(text) - static_cast<std::size_t>(length),
                                                "_%016llx", static_cast<unsigned long long>(name.tlut));
    }
    length += std::snprintf(text + length, sizeof(text) - static_cast<std::size_t>(length), "_%u", name.format);
    if (name.arbitrary) {
        length += std::snprintf(text + length, sizeof(text) - static_cast<std::size_t>(length), "_arb");
    }
    if (name.level != 0u) {
        std::snprintf(text + length, sizeof(text) - static_cast<std::size_t>(length), "_mip%u", name.level);
    }
    return text;
}

namespace {

std::uint32_t read32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8u |
           static_cast<std::uint32_t>(p[2]) << 16u | static_cast<std::uint32_t>(p[3]) << 24u;
}

constexpr std::uint32_t fourcc(char a, char b, char c, char d) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(a)) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(b)) << 8u) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(c)) << 16u) |
           (static_cast<std::uint32_t>(static_cast<std::uint8_t>(d)) << 24u);
}

// DXGI_FORMAT values of a DX10 header.
DdsFormat dds_dxgi(std::uint32_t dxgi) noexcept {
    switch (dxgi) {
    case 70: case 71: case 72: return DdsFormat::BC1;
    case 73: case 74: case 75: return DdsFormat::BC2;
    case 76: case 77: case 78: return DdsFormat::BC3;
    case 79: case 80: return DdsFormat::BC4;
    case 82: case 83: return DdsFormat::BC5;
    case 97: case 98: case 99: return DdsFormat::BC7;
    case 27: case 28: case 29: return DdsFormat::RGBA8;
    case 87: case 90: case 91: return DdsFormat::BGRA8;
    default: return DdsFormat::Unknown;
    }
}

// Bytes of one 4x4 block, 0 for uncompressed formats (4 bytes a pixel).
std::uint32_t dds_block_bytes(DdsFormat format) noexcept {
    switch (format) {
    case DdsFormat::BC1:
    case DdsFormat::BC4: return 8u;
    case DdsFormat::BC2:
    case DdsFormat::BC3:
    case DdsFormat::BC5:
    case DdsFormat::BC7: return 16u;
    default: return 0u;
    }
}

}  // namespace

bool parse_dds(std::span<const std::uint8_t> file, DdsLayout& out) noexcept {
    // "DDS " magic, DDS_HEADER (124 bytes): size, flags, height, width, pitch, depth, mip count, reserved[11], pixel
    // format (size, flags, fourCC, bit count, R/G/B/A masks), caps[4], reserved.
    if (file.size() < 128u || read32(file.data()) != fourcc('D', 'D', 'S', ' ') || read32(file.data() + 4u) != 124u) {
        return false;
    }
    const std::uint8_t* h = file.data() + 4u;
    const std::uint32_t height = read32(h + 8u);
    const std::uint32_t width = read32(h + 12u);
    const std::uint32_t depth = read32(h + 20u);
    std::uint32_t levels = read32(h + 24u);
    const std::uint8_t* pf = h + 72u;
    const std::uint32_t pf_flags = read32(pf + 4u);
    const std::uint32_t code = read32(pf + 8u);
    const std::uint32_t bits = read32(pf + 12u);
    const std::uint32_t r_mask = read32(pf + 16u);
    const std::uint32_t a_mask = read32(pf + 28u);
    constexpr std::uint32_t kFourCC = 0x4u;
    constexpr std::uint32_t kRgb = 0x40u;
    std::size_t offset = 128u;
    DdsFormat format = DdsFormat::Unknown;
    if ((pf_flags & kFourCC) != 0u) {
        if (code == fourcc('D', 'X', '1', '0')) {
            // DDS_HEADER_DXT10: dxgi format, dimension (3 = 2D), misc, array size, misc2.
            if (file.size() < 148u || read32(file.data() + 132u) != 3u || read32(file.data() + 140u) > 1u) return false;
            format = dds_dxgi(read32(file.data() + 128u));
            offset = 148u;
        } else if (code == fourcc('D', 'X', 'T', '1')) {
            format = DdsFormat::BC1;
        } else if (code == fourcc('D', 'X', 'T', '2') || code == fourcc('D', 'X', 'T', '3')) {
            format = DdsFormat::BC2;
        } else if (code == fourcc('D', 'X', 'T', '4') || code == fourcc('D', 'X', 'T', '5')) {
            format = DdsFormat::BC3;
        } else if (code == fourcc('A', 'T', 'I', '1') || code == fourcc('B', 'C', '4', 'U')) {
            format = DdsFormat::BC4;
        } else if (code == fourcc('A', 'T', 'I', '2') || code == fourcc('B', 'C', '5', 'U')) {
            format = DdsFormat::BC5;
        }
    } else if ((pf_flags & kRgb) != 0u && bits == 32u && a_mask == 0xFF000000u) {
        format = r_mask == 0x00FF0000u ? DdsFormat::BGRA8 : r_mask == 0x000000FFu ? DdsFormat::RGBA8 : DdsFormat::Unknown;
    }
    const std::uint32_t block = dds_block_bytes(format);
    if (format == DdsFormat::Unknown || width == 0u || height == 0u || width > 16384u || height > 16384u || depth > 1u) {
        return false;
    }
    levels = std::clamp<std::uint32_t>(levels, 1u, 16u);
    levels = std::min(levels, static_cast<std::uint32_t>(std::bit_width(std::max(width, height))));
    out = DdsLayout{};
    out.format = format;
    out.width = width;
    out.height = height;
    std::size_t at = offset;
    std::uint32_t kept = 0;
    for (std::uint32_t level = 0; level < levels; ++level) {
        const std::uint32_t lw = std::max(1u, width >> level);
        const std::uint32_t lh = std::max(1u, height >> level);
        const std::uint32_t row_bytes = block != 0u ? ((lw + 3u) / 4u) * block : lw * 4u;
        const std::uint32_t rows = block != 0u ? (lh + 3u) / 4u : lh;
        const std::size_t bytes = static_cast<std::size_t>(row_bytes) * rows;
        if (at + bytes > file.size()) break;
        out.level_offset[level] = at;
        out.row_bytes[level] = row_bytes;
        out.rows[level] = rows;
        at += bytes;
        ++kept;
    }
    out.levels = kept;
    return kept != 0u;
}

namespace {

constexpr std::uint64_t kUploadBytesPerFrame = 48ull << 20;
constexpr std::uint32_t kPreloadOutstanding = 8u;

// A name in lower case if it is ASCII (pack names are), else empty.
std::string ascii_lower(const std::filesystem::path& name) {
    const std::u8string text = name.u8string();
    std::string out;
    for (const char8_t c : text) {
        if (c == 0 || c > 0x7F) return {};
        out.push_back(static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    }
    return out;
}

std::string ascii_name(const std::filesystem::path& name) {
    const std::u8string text = name.u8string();
    std::string out;
    for (const char8_t c : text) {
        if (c == 0 || c > 0x7F) return {};
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string display(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

}  // namespace

std::uint64_t TexturePacks::size_key(std::uint32_t width, std::uint32_t height, std::uint32_t format) noexcept {
    return (static_cast<std::uint64_t>(width) << 32u) | (static_cast<std::uint64_t>(height) << 8u) | format;
}

std::uint64_t TexturePacks::name_key(std::uint64_t size, std::uint64_t hash, std::uint64_t tlut, bool mipmapped) noexcept {
    // _m (the game samples the texture with its own mipmaps) names another file than the same texture without.
    size |= static_cast<std::uint64_t>(mipmapped) << 63u;
    std::uint64_t key = hash;
    key ^= (tlut + 0x9E3779B97F4A7C15ull + (key << 6u) + (key >> 2u));
    key ^= (size + 0x9E3779B97F4A7C15ull + (key << 6u) + (key >> 2u));
    return key;
}

void TexturePacks::start_workers() {
    if (!workers_.empty()) return;
    stopping_ = false;
    const unsigned count = std::clamp(std::thread::hardware_concurrency() / 8u, 2u, 4u);
    for (unsigned i = 0; i < count; ++i) {
        workers_.emplace_back([this] { worker_main(); });
    }
}

void TexturePacks::shutdown() noexcept {
    stop_workers();
    clear_slots();
    index_.reset();
    dumped_.clear();
    configured_ = false;
}

void TexturePacks::stop_workers() noexcept {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    for (std::thread& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
    workers_.clear();
    std::lock_guard<std::mutex> lock(mutex_);
    finished_.clear();
    built_.reset();
}

void TexturePacks::submit(std::function<void()> job) {
    start_workers();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

void TexturePacks::worker_main() {
    galaxy::host::name_current_thread(L"Nebula Texture Pack");
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
            if (stopping_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        try {
            job();
        } catch (const std::exception& error) {
            std::cerr << "[texture-pack] loader error: " << error.what() << '\n';
        }
    }
}

void TexturePacks::configure(const TexturePackSettings& settings) {
    const bool enable = settings.enabled && !settings.packs.empty();
    const bool was_enabled = configured_ && settings_.enabled && !settings_.packs.empty();
    const bool reindex = enable && (!was_enabled || settings.packs != settings_.packs);
    const bool disable = !enable && (was_enabled || !configured_);
    // Dumping covers textures decoded from now on: the cache decodes everything again.
    changed_ = changed_ || (configured_ && settings.dump != settings_.dump);
    configured_ = true;
    settings_ = settings;
    if (disable) {
        ++generation_;
        changed_ = changed_ || index_ != nullptr;
        index_.reset();
        clear_slots();
        publish_summary();
        return;
    }
    if (!reindex) {
        evict_to_budget(0u);
        return;
    }
    // The new index replaces the old one at a later pump; the old slots serve until then.
    const std::vector<std::filesystem::path> packs = settings_.packs;
    const std::uint64_t generation = ++generation_;
    submit([this, packs, generation] {
        std::shared_ptr<Index> index = build_index(packs, generation);
        std::lock_guard<std::mutex> lock(mutex_);
        built_ = std::move(index);
    });
}

void TexturePacks::clear_slots() noexcept {
    slots_.clear();
    ready_queue_.clear();
    outstanding_ = 0;
    preload_cursor_ = 0;
    resident_bytes_ = 0;
    resident_count_ = 0;
}

std::shared_ptr<TexturePacks::Index> TexturePacks::build_index(std::span<const std::filesystem::path> packs,
                                                               std::uint64_t generation) {
    const auto start = std::chrono::steady_clock::now();
    auto index = std::make_shared<Index>();
    index->generation = generation;
    const std::string id = ascii_lower(kGameId);
    const std::string region_free = id.substr(0, 3);
    struct Found {
        std::filesystem::path path;
        TexturePackName name;
        bool dds;
        std::size_t folder;  // priority: a later folder (and within one, a later pack by name) wins a name
    };
    std::vector<Found> found;
    for (std::size_t f = 0; f < packs.size(); ++f) {
        const std::filesystem::path& root = packs[f];
        std::error_code error;
        if (!std::filesystem::is_directory(root, error)) continue;
        ++index->folders;
        // Game folders: <root>/<GameID>, its 3-letter region-free prefix, or the same one or two levels down (a pack
        // unpacked into its own folder); the root itself when it is one or holds none (a mod's textures/ folder).
        std::vector<std::filesystem::path> games;
        const std::string root_name = ascii_lower(root.filename());
        if (root_name == id || root_name == region_free) {
            games.push_back(root);
        } else {
            for (auto it = std::filesystem::recursive_directory_iterator(
                     root, std::filesystem::directory_options::skip_permission_denied, error);
                 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                if (it.depth() > 2) {
                    it.disable_recursion_pending();
                    continue;
                }
                if (!it->is_directory(error)) continue;
                const std::string name = ascii_lower(it->path().filename());
                if (name == id || name == region_free) {
                    games.push_back(it->path());
                    it.disable_recursion_pending();
                }
            }
            if (games.empty()) games.push_back(root);
        }
        // Packs side by side in one folder: by name, a later one winning a name (as later folders do).
        std::sort(games.begin(), games.end());
        for (std::size_t g = 0; g < games.size(); ++g) {
            const std::filesystem::path& game = games[g];
            const std::size_t priority = f * 65536u + std::min<std::size_t>(g, 65535u);
            for (auto it = std::filesystem::recursive_directory_iterator(
                     game, std::filesystem::directory_options::skip_permission_denied, error);
                 !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
                if (!it->is_regular_file(error)) continue;
                const std::string ext = ascii_lower(it->path().extension());
                const bool dds = ext == ".dds";
                if (!dds && ext != ".png") continue;
                ++index->files;
                const std::string stem = ascii_name(it->path().stem());
                const auto name = parse_texture_pack_name(stem);
                if (name) {
                    found.push_back({it->path(), *name, dds, priority});
                    if (texture_pack_name(*name) != stem) ++index->corrected;
                } else if (stem.rfind("tex1_", 0) == 0) {
                    index->unreadable.push_back(display(it->path().filename()));
                }
            }
        }
    }
    // Deterministic: the latest folder first, a DDS before a PNG of the same name, then by path.
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) {
        if (a.folder != b.folder) return a.folder > b.folder;
        if (a.dds != b.dds) return a.dds;
        return a.path < b.path;
    });
    for (const Found& f : found) {
        if (f.name.level != 0u) continue;
        const std::uint64_t size = size_key(f.name.width, f.name.height, f.name.format);
        auto& map = f.name.any_tlut ? index->any_tlut : index->exact;
        const std::uint64_t key = name_key(size, f.name.hash, f.name.any_tlut ? 0u : f.name.tlut, f.name.mipmapped);
        if (map.find(key) != map.end()) {
            ++index->shadowed;
            continue;
        }
        map.emplace(key, static_cast<std::uint32_t>(index->records.size()));
        index->sizes.insert(size);
        Record record{};
        record.name = f.name;
        record.file = f.path;
        record.dds = f.dds;
        index->records.push_back(std::move(record));
    }
    // Custom lower levels (PNG): _mip<n> beside a PNG base of the same folder.
    for (const Found& f : found) {
        if (f.name.level == 0u || f.dds) continue;
        const std::uint64_t size = size_key(f.name.width, f.name.height, f.name.format);
        auto& map = f.name.any_tlut ? index->any_tlut : index->exact;
        const auto it = map.find(name_key(size, f.name.hash, f.name.any_tlut ? 0u : f.name.tlut, f.name.mipmapped));
        if (it == map.end()) continue;
        Record& record = index->records[it->second];
        if (record.dds || record.file.parent_path() != f.path.parent_path()) continue;
        if (record.mips.size() <= f.name.level) record.mips.resize(f.name.level + 1u);
        if (record.mips[f.name.level].empty()) {
            record.mips[f.name.level] = f.path;
            ++index->levels;
        }
    }
    index->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return index;
}

bool TexturePacks::may_match(std::uint32_t width, std::uint32_t height, TexFormat format) const noexcept {
    return index_ != nullptr && settings_.enabled &&
           index_->sizes.count(size_key(width, height, static_cast<std::uint32_t>(format))) != 0u;
}

std::uint32_t TexturePacks::find(std::uint32_t width, std::uint32_t height, TexFormat format, std::uint64_t hash,
                                 std::uint64_t tlut, bool mipmapped) const {
    if (!may_match(width, height, format)) return kNone;
    const std::uint64_t size = size_key(width, height, static_cast<std::uint32_t>(format));
    const auto matches = [&](std::uint32_t slot, bool any, bool mip) {
        const TexturePackName& name = index_->records[slot].name;
        return name.width == width && name.height == height && name.format == static_cast<std::uint8_t>(format) &&
               name.hash == hash && name.mipmapped == mip && (any || name.tlut == tlut);
    };
    // The name as the game uses the texture (with or without its mipmaps) first, then the other: a pack made where
    // the texture was used the other way still replaces it.
    for (const bool mip : {mipmapped, !mipmapped}) {
        if (const auto it = index_->exact.find(name_key(size, hash, tlut, mip));
            it != index_->exact.end() && matches(it->second, false, mip)) {
            return it->second;
        }
        if (texture_format_palettized(format)) {
            if (const auto it = index_->any_tlut.find(name_key(size, hash, 0u, mip));
                it != index_->any_tlut.end() && matches(it->second, true, mip)) {
                return it->second;
            }
        }
    }
    return kNone;
}

const TextureReplacement* TexturePacks::resident(std::uint32_t slot) const noexcept {
    return slot < slots_.size() && slots_[slot].state == SlotState::Resident ? &slots_[slot].replacement : nullptr;
}

std::filesystem::path TexturePacks::file(std::uint32_t slot) const {
    return index_ && slot < index_->records.size() ? index_->records[slot].file : std::filesystem::path{};
}

void TexturePacks::add_user(std::uint32_t slot) {
    if (slot >= slots_.size()) return;
    Slot& s = slots_[slot];
    ++s.users;
    s.last_use = frame_;
    if (s.state == SlotState::Idle) request(slot);
}

void TexturePacks::remove_user(std::uint32_t slot) noexcept {
    if (slot >= slots_.size() || slots_[slot].users == 0u) return;
    Slot& s = slots_[slot];
    --s.users;
    s.last_use = frame_;
}

void TexturePacks::request(std::uint32_t slot) {
    if (!index_ || slot >= slots_.size() || slots_[slot].state != SlotState::Idle) return;
    slots_[slot].state = SlotState::Loading;
    ++outstanding_;
    const std::shared_ptr<const Index> index = index_;
    const std::uint64_t generation = generation_;
    submit([this, index, slot, generation] {
        std::unique_ptr<Load> result = load(index->records[slot], slot, generation);
        std::lock_guard<std::mutex> lock(mutex_);
        finished_.push_back(std::move(result));
    });
}

std::unique_ptr<TexturePacks::Load> TexturePacks::load(const Record& record, std::uint32_t slot,
                                                       std::uint64_t generation) const {
    // A crash while this image loads names it (NEB-T01).
    const galaxy::crash::ScopedActivity activity(galaxy::crash::Activity::TexturePack, record.file);
    const auto start = std::chrono::steady_clock::now();
    auto result = std::make_unique<Load>();
    result->slot = slot;
    result->generation = generation;
    std::string error;
    if (!load_texture_pack_image(record.file, record.dds, result->pixels, &error)) {
        std::cerr << "[texture-pack] skipped " << display(record.file.filename()) << ": " << error << '\n';
        result->failed = true;
    } else {
        // Custom lower levels (PNG _mip<n>) in place of generated ones, where their size is right.
        for (std::size_t level = 1; level < record.mips.size() && level < result->pixels.levels.size(); ++level) {
            if (record.mips[level].empty()) continue;
            TexturePackPixels mip;
            if (load_texture_pack_image(record.mips[level], false, mip, nullptr) &&
                mip.width == std::max(1u, result->pixels.width >> level) &&
                mip.height == std::max(1u, result->pixels.height >> level)) {
                result->pixels.levels[level] = std::move(mip.levels[0]);
            }
        }
    }
    if (!result->failed && stager_) {
        // Staged here: the render thread then only records the copies (a 2048x8192 chain is 89 MB to write).
        TexturePackPixels& pixels = result->pixels;
        pixels.level_count = static_cast<std::uint32_t>(pixels.levels.size());
        const std::uint64_t size = texture_pack_staging_layout(pixels.width, pixels.height, pixels.level_count,
                                                               pixels.offsets, pixels.pitches);
        try {
            TextureBackend::UploadChunk chunk = stager_(size);
            if (chunk.cpu_base != nullptr) {
                for (std::uint32_t l = 0; l < pixels.level_count; ++l) {
                    const std::uint32_t lw = std::max<std::uint32_t>(1u, pixels.width >> l);
                    const std::uint32_t lh = std::max<std::uint32_t>(1u, pixels.height >> l);
                    for (std::uint32_t row = 0; row < lh; ++row) {
                        std::memcpy(chunk.cpu_base + pixels.offsets[l] + static_cast<std::uint64_t>(row) * pixels.pitches[l],
                                    pixels.levels[l].data() + static_cast<std::size_t>(row) * lw * 4u, lw * 4u);
                    }
                }
                pixels.levels.clear();
                pixels.levels.shrink_to_fit();
                pixels.staging = std::move(chunk);
            }
        } catch (const std::exception& error) {
            // Out of upload memory: the render thread stages it (or fails it) instead.
            std::cerr << "[texture-pack] staging " << display(record.file.filename()) << ": " << error.what() << '\n';
            pixels.offsets.clear();
            pixels.pitches.clear();
        }
    }
    result->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}

void TexturePacks::evict_to_budget(std::uint64_t incoming) {
    const std::uint64_t budget = settings_.budget_bytes;
    if (budget == 0u) return;
    while (resident_bytes_ + incoming > budget) {
        std::uint32_t victim = kNone;
        std::uint64_t oldest = ~0ull;
        for (std::uint32_t i = 0; i < slots_.size(); ++i) {
            const Slot& s = slots_[i];
            if (s.state == SlotState::Resident && s.users == 0u && s.last_use < oldest) {
                oldest = s.last_use;
                victim = i;
            }
        }
        if (victim == kNone) return;
        Slot& s = slots_[victim];
        resident_bytes_ -= s.replacement.bytes;
        --resident_count_;
        ++evictions_;
        // Entries that bound it hold their own reference: the GPU keeps it until their fence.
        s.replacement = {};
        s.state = SlotState::Idle;
    }
}

std::uint64_t texture_pack_staging_layout(std::uint32_t width, std::uint32_t height, std::uint32_t levels,
                                          std::vector<std::uint64_t>& offsets, std::vector<std::uint32_t>& pitches) {
    offsets.assign(levels, 0u);
    pitches.assign(levels, 0u);
    std::uint64_t size = 0;
    for (std::uint32_t l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, width >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, height >> l);
        size = (size + TextureBackend::kPlacementAlignment - 1u) &
               ~static_cast<std::uint64_t>(TextureBackend::kPlacementAlignment - 1u);
        offsets[l] = size;
        pitches[l] = (lw * 4u + TextureBackend::kPitchAlignment - 1u) & ~(TextureBackend::kPitchAlignment - 1u);
        size += static_cast<std::uint64_t>(pitches[l]) * lh;
    }
    return size;
}

namespace {
std::mutex s_summary_mutex;
TexturePacks::Summary s_summary;
}  // namespace

void TexturePacks::publish_summary() const {
    Summary summary{};
    if (index_) {
        summary.indexed = true;
        summary.textures = index_->records.size();
        summary.levels = index_->levels;
        summary.folders = index_->folders;
        summary.unreadable = index_->unreadable;
    }
    const std::lock_guard<std::mutex> lock(s_summary_mutex);
    s_summary = std::move(summary);
}

TexturePacks::Summary TexturePacks::summary() {
    const std::lock_guard<std::mutex> lock(s_summary_mutex);
    return s_summary;
}

bool TexturePacks::uploading(std::uint32_t slot) const noexcept {
    return slot < slots_.size() && slots_[slot].state == SlotState::Uploading;
}

bool TexturePacks::pump(std::vector<std::pair<std::uint32_t, TexturePackPixels>>& uploads) {
    bool changed = std::exchange(changed_, false);
    std::vector<std::unique_ptr<Load>> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (built_ && built_->generation == generation_) {
            index_ = std::move(built_);
            changed = true;
            clear_slots();
            slots_.resize(index_->records.size());
            preload_start_ns_ = static_cast<std::uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch() / std::chrono::nanoseconds(1));
            std::cerr << "[texture-pack] index folders=" << index_->folders << " files=" << index_->files
                      << " textures=" << index_->records.size() << " levels=" << index_->levels
                      << " shadowed=" << index_->shadowed << " corrected=" << index_->corrected
                      << " unreadable=" << index_->unreadable.size() << " ms=" << index_->ms << '\n';
            for (const std::string& name : index_->unreadable) {
                std::cerr << "[texture-pack] not a texture name: " << name << '\n';
            }
            publish_summary();
        }
        built_.reset();
        finished.swap(finished_);
    }
    for (auto& load : finished) {
        if (load->generation != generation_ || load->slot >= slots_.size() ||
            slots_[load->slot].state != SlotState::Loading) {
            continue;
        }
        ++loads_;
        load_ms_ += load->ms;
        Slot& s = slots_[load->slot];
        if (load->failed) {
            ++load_failures_;
            --outstanding_;
            s.state = SlotState::Failed;
            continue;
        }
        s.state = SlotState::Ready;
        ready_queue_.push_back(load->slot);
        s.ready = std::move(load);
    }
    if (index_ && settings_.preload) {
        while (preload_cursor_ < slots_.size() && outstanding_ < kPreloadOutstanding &&
               (settings_.budget_bytes == 0u || resident_bytes_ < settings_.budget_bytes)) {
            request(preload_cursor_++);
        }
    }
    std::uint64_t handed = 0;
    const bool hand_out = uploads.empty();
    while (hand_out && !ready_queue_.empty() && handed < kUploadBytesPerFrame) {
        const std::uint32_t i = ready_queue_.front();
        ready_queue_.pop_front();
        Slot& s = slots_[i];
        if (s.state != SlotState::Ready || !s.ready) continue;
        --outstanding_;
        std::uint64_t bytes = s.ready->pixels.staging ? s.ready->pixels.staging->size : 0u;
        for (const auto& level : s.ready->pixels.levels) bytes += level.size();
        evict_to_budget(bytes);
        if (settings_.budget_bytes != 0u && resident_bytes_ + bytes > settings_.budget_bytes) {
            // Over the budget with every resident replacement bound: the original stays.
            s.ready.reset();
            s.state = SlotState::Idle;
            ++deferred_;
            continue;
        }
        s.state = SlotState::Uploading;
        uploads.emplace_back(i, std::move(s.ready->pixels));
        s.ready.reset();
        handed += bytes;
    }
    if (settings_.preload && preload_start_ns_ != 0u && outstanding_ == 0u &&
        (preload_cursor_ >= slots_.size() ||
         (settings_.budget_bytes != 0u && resident_bytes_ >= settings_.budget_bytes))) {
        const auto now = static_cast<std::uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch() / std::chrono::nanoseconds(1));
        std::cerr << "[texture-pack] preloaded resident=" << resident_count_ << " mb=" << (resident_bytes_ >> 20u)
                  << " ms=" << (now - preload_start_ns_) / 1000000u << '\n';
        preload_start_ns_ = 0;
    }
    return changed;
}

void TexturePacks::make_resident(std::uint32_t slot, TextureReplacement&& replacement) {
    if (slot >= slots_.size() || slots_[slot].state != SlotState::Uploading) return;
    Slot& s = slots_[slot];
    if (!replacement.texture) {
        ++load_failures_;
        s.state = SlotState::Failed;
        return;
    }
    s.replacement = std::move(replacement);
    // The scale over the guest size the name gives: the samplers' LOD shift (pack_sampler_key).
    const std::uint32_t guest_width = index_ && slot < index_->records.size() ? index_->records[slot].name.width : 0u;
    s.replacement.lod_shift_x16 = 0u;
    s.replacement.arbitrary = index_ && slot < index_->records.size() && index_->records[slot].name.arbitrary;
    if (guest_width != 0u && s.replacement.width > guest_width) {
        const double scale = static_cast<double>(s.replacement.width) / static_cast<double>(guest_width);
        s.replacement.lod_shift_x16 = static_cast<std::uint8_t>(std::clamp(std::lround(16.0 * std::log2(scale)), 0l, 128l));
    }
    s.state = SlotState::Resident;
    s.last_use = frame_;
    resident_bytes_ += s.replacement.bytes;
    ++resident_count_;
    ++uploads_;
    upload_bytes_ += s.replacement.bytes;
}

void TexturePacks::dump(const TexturePackName& name, std::uint32_t width, std::uint32_t height,
                        const std::uint8_t* rgba) {
    if (!dumping() || rgba == nullptr || width == 0u || height == 0u) return;
    std::string stem = texture_pack_name(name);
    if (!dumped_.insert(stem).second) return;
    const std::filesystem::path folder = settings_.dumps / kGameId;
    std::vector<std::uint8_t> pixels(rgba, rgba + static_cast<std::size_t>(width) * height * 4u);
    submit([this, folder, stem = std::move(stem), width, height, pixels = std::move(pixels)]() mutable {
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        const std::filesystem::path path = folder / (stem + ".png");
        if (std::filesystem::exists(path, error)) return;
        // Written beside and renamed: a reader never sees a partial file.
        const std::filesystem::path partial = folder / (stem + ".png.part");
        if (write_png(partial, width, height, pixels.data())) {
            std::filesystem::rename(partial, path, error);
            if (!error) {
                std::lock_guard<std::mutex> lock(mutex_);
                ++dumped_count_;
                return;
            }
        }
        std::filesystem::remove(partial, error);
    });
}

TexturePacks::Stats TexturePacks::stats() const noexcept {
    Stats stats{};
    if (index_) {
        stats.names = index_->records.size();
        stats.files = index_->files;
        stats.folders = index_->folders;
        stats.levels = index_->levels;
        stats.unreadable = index_->unreadable.size();
        stats.index_ms = index_->ms;
    }
    stats.loads = loads_;
    stats.load_failures = load_failures_;
    stats.uploads = uploads_;
    stats.upload_bytes = upload_bytes_;
    stats.resident_bytes = resident_bytes_;
    stats.resident = resident_count_;
    stats.evictions = evictions_;
    stats.deferred = deferred_;
    stats.load_ms = load_ms_;
    std::lock_guard<std::mutex> lock(mutex_);
    stats.dumped = dumped_count_;
    return stats;
}

}  // namespace galaxy::gx
