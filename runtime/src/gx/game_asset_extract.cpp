#include "galaxy/gx/game_asset_extract.h"

#include <bit>
#include <cstring>
#include <fstream>

namespace galaxy::gx {

namespace {

constexpr std::size_t kMaxCosmeticArchiveSize = 64u * 1024u * 1024u;

[[nodiscard]] bool contains_bytes(
    std::span<const std::byte> data, std::size_t offset,
    std::size_t count) noexcept {
    return offset <= data.size() && count <= data.size() - offset;
}

[[nodiscard]] std::uint32_t read_be32(
    std::span<const std::byte> data, std::size_t offset) noexcept {
    if (!contains_bytes(data, offset, 4)) {
        return 0;
    }
    return (static_cast<std::uint32_t>(data[offset]) << 24) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 8) |
        static_cast<std::uint32_t>(data[offset + 3]);
}

[[nodiscard]] std::uint16_t read_be16(
    std::span<const std::byte> data, std::size_t offset) noexcept {
    if (!contains_bytes(data, offset, 2)) {
        return 0;
    }
    return static_cast<std::uint16_t>(
        (static_cast<std::uint32_t>(data[offset]) << 8) |
        static_cast<std::uint32_t>(data[offset + 1]));
}

[[nodiscard]] std::uint8_t read_u8(
    std::span<const std::byte> data, std::size_t offset) noexcept {
    return offset < data.size() ? static_cast<std::uint8_t>(data[offset])
                                 : std::uint8_t{0};
}

}  // namespace

std::vector<std::byte> yaz0_decompress(std::span<const std::byte> data) {
    if (data.size() < 16 ||
        std::memcmp(data.data(), "Yaz0", 4) != 0) {
        return {};
    }
    const std::uint32_t dec_size = read_be32(data, 4);
    // Refuse implausible sizes from corrupt archives instead of allocating them.
    if (dec_size == 0 || dec_size > kMaxCosmeticArchiveSize) {
        return {};
    }
    std::vector<std::byte> out(dec_size);
    std::size_t src = 16;
    std::size_t dst = 0;
    while (dst < dec_size) {
        if (src >= data.size()) {
            return {};
        }
        std::uint8_t code = read_u8(data, src);
        ++src;
        for (int bit = 0; bit < 8 && dst < dec_size; ++bit) {
            if ((code & 0x80u) != 0u) {
                if (src >= data.size()) {
                    return {};
                }
                out[dst] = data[src];
                ++src;
                ++dst;
            } else {
                if (src + 1 >= data.size()) {
                    return {};
                }
                const std::uint8_t b1 = read_u8(data, src);
                const std::uint8_t b2 = read_u8(data, src + 1);
                src += 2;
                const std::uint32_t dist =
                    ((static_cast<std::uint32_t>(b1) & 0x0Fu) << 8) | b2;
                std::uint32_t copy_len = static_cast<std::uint32_t>(b1) >> 4;
                if (copy_len == 0) {
                    if (src >= data.size()) {
                        return {};
                    }
                    copy_len = static_cast<std::uint32_t>(read_u8(data, src)) +
                        0x12u;
                    ++src;
                } else {
                    copy_len += 2;
                }
                if (dist + 1 > dst) {
                    // Back-reference before the start of output: corrupt input.
                    return {};
                }
                std::size_t copy_src = dst - (dist + 1);
                for (std::uint32_t i = 0;
                     i < copy_len && dst < dec_size; ++i) {
                    out[dst] = out[copy_src];
                    ++dst;
                    ++copy_src;
                }
            }
            code = static_cast<std::uint8_t>(code << 1);
        }
    }
    return out;
}

std::vector<RarcFile> rarc_list_files(std::span<const std::byte> data) {
    std::vector<RarcFile> result;
    if (data.size() < 0x40 || std::memcmp(data.data(), "RARC", 4) != 0) {
        return result;
    }
    const std::size_t file_data_off = static_cast<std::size_t>(read_be32(data, 0x0C)) + 0x20u;
    const std::uint32_t num_nodes = read_be32(data, 0x20);
    const std::size_t node_off = static_cast<std::size_t>(read_be32(data, 0x24)) + 0x20u;
    const std::size_t entry_off = static_cast<std::size_t>(read_be32(data, 0x2C)) + 0x20u;
    const std::size_t string_off = static_cast<std::size_t>(read_be32(data, 0x34)) + 0x20u;

    // Header counts come from file data; bound them before looping.
    if (num_nodes > 4096u ||
        !contains_bytes(data, node_off, static_cast<std::size_t>(num_nodes) * 0x10u)) {
        return result;
    }
    for (std::uint32_t n = 0; n < num_nodes; ++n) {
        const std::size_t base =
            static_cast<std::size_t>(node_off) + n * 0x10u;
        const std::uint16_t entry_count = read_be16(data, base + 0x0A);
        const std::uint32_t first_entry_idx = read_be32(data, base + 0x0C);
        if (entry_count > 8192u) {
            continue;
        }
        for (std::uint32_t e = 0; e < entry_count; ++e) {
            const std::size_t ebase = static_cast<std::size_t>(entry_off) +
                (static_cast<std::size_t>(first_entry_idx) + e) * 0x14u;
            if (!contains_bytes(data, ebase, 0x14u)) {
                continue;
            }
            const std::uint32_t type_and_name_off = read_be32(data, ebase + 4);
            // Bit 17 of the (type << 16 | nameOffset) word marks a directory.
            const bool is_dir = (type_and_name_off & 0x00020000u) != 0u;
            const std::uint32_t name_off = type_and_name_off & 0xFFFFu;
            const std::uint32_t data_off = read_be32(data, ebase + 8);
            const std::uint32_t data_size = read_be32(data, ebase + 12);

            const std::size_t name_start =
                static_cast<std::size_t>(string_off) + name_off;
            if (name_start >= data.size()) {
                continue;
            }
            std::size_t name_end = name_start;
            while (name_end < data.size() &&
                   data[name_end] != std::byte{0}) {
                ++name_end;
            }
            if (name_end == data.size()) {
                continue;
            }
            std::string name(
                reinterpret_cast<const char*>(data.data() + name_start),
                name_end - name_start);
            if (is_dir || name == "." || name == "..") {
                continue;
            }
            const std::size_t file_start =
                static_cast<std::size_t>(file_data_off) + data_off;
            if (!contains_bytes(data, file_start, data_size)) {
                continue;
            }
            result.push_back(RarcFile{
                std::move(name),
                data.subspan(file_start, data_size)});
        }
    }
    return result;
}

std::optional<std::span<const std::byte>> rarc_find_file(
    std::span<const std::byte> data, std::string_view name) {
    for (const RarcFile& file : rarc_list_files(data)) {
        if (file.name == name) {
            return file.data;
        }
    }
    return std::nullopt;
}

namespace {

void decode_i4(
    std::span<const std::byte> data, std::size_t off, unsigned w,
    unsigned h, std::vector<std::uint8_t>& out) {
    std::size_t idx = off;
    for (unsigned by = 0; by < h; by += 8) {
        for (unsigned bx = 0; bx < w; bx += 8) {
            for (unsigned y = 0; y < 8; ++y) {
                for (unsigned x = 0; x < 8; x += 2) {
                    const std::uint8_t b = read_u8(data, idx);
                    ++idx;
                    const std::uint8_t nibbles[2] = {
                        static_cast<std::uint8_t>((b >> 4) & 0xFu),
                        static_cast<std::uint8_t>(b & 0xFu)};
                    for (int half = 0; half < 2; ++half) {
                        const unsigned px = bx + x + static_cast<unsigned>(half);
                        const unsigned py = by + y;
                        if (px < w && py < h) {
                            const std::uint8_t v = static_cast<std::uint8_t>(
                                nibbles[half] * 17u);
                            const std::size_t o = (static_cast<std::size_t>(py) * w + px) * 4u;
                            out[o] = v;
                            out[o + 1] = v;
                            out[o + 2] = v;
                            out[o + 3] = v;
                        }
                    }
                }
            }
        }
    }
}

void decode_ia4(
    std::span<const std::byte> data, std::size_t off, unsigned w,
    unsigned h, std::vector<std::uint8_t>& out) {
    std::size_t idx = off;
    for (unsigned by = 0; by < h; by += 4) {
        for (unsigned bx = 0; bx < w; bx += 8) {
            for (unsigned y = 0; y < 4; ++y) {
                for (unsigned x = 0; x < 8; ++x) {
                    const std::uint8_t b = read_u8(data, idx);
                    ++idx;
                    const unsigned px = bx + x;
                    const unsigned py = by + y;
                    if (px < w && py < h) {
                        const std::uint8_t a = static_cast<std::uint8_t>((b >> 4) * 17u);
                        const std::uint8_t i = static_cast<std::uint8_t>((b & 0xFu) * 17u);
                        const std::size_t o = (static_cast<std::size_t>(py) * w + px) * 4u;
                        out[o] = i;
                        out[o + 1] = i;
                        out[o + 2] = i;
                        out[o + 3] = a;
                    }
                }
            }
        }
    }
}

void decode_rgb5a3(
    std::span<const std::byte> data, std::size_t off, unsigned w,
    unsigned h, std::vector<std::uint8_t>& out) {
    std::size_t idx = off;
    for (unsigned by = 0; by < h; by += 4) {
        for (unsigned bx = 0; bx < w; bx += 4) {
            for (unsigned y = 0; y < 4; ++y) {
                for (unsigned x = 0; x < 4; ++x) {
                    const unsigned px = bx + x;
                    const unsigned py = by + y;
                    const std::uint16_t val = read_be16(data, idx);
                    idx += 2;
                    std::uint8_t r, g, b, a;
                    if ((val & 0x8000u) != 0u) {
                        const auto expand5 = [](unsigned value) {
                            return static_cast<std::uint8_t>((value << 3) | (value >> 2));
                        };
                        r = expand5((val >> 10) & 0x1Fu);
                        g = expand5((val >> 5) & 0x1Fu);
                        b = expand5(val & 0x1Fu);
                        a = 255u;
                    } else {
                        const unsigned alpha = (val >> 12) & 0x7u;
                        a = static_cast<std::uint8_t>((alpha << 5) | (alpha << 2) | (alpha >> 1));
                        r = static_cast<std::uint8_t>(((val >> 8) & 0xFu) * 17u);
                        g = static_cast<std::uint8_t>(((val >> 4) & 0xFu) * 17u);
                        b = static_cast<std::uint8_t>((val & 0xFu) * 17u);
                    }
                    if (px < w && py < h) {
                        const std::size_t o = (static_cast<std::size_t>(py) * w + px) * 4u;
                        out[o] = r;
                        out[o + 1] = g;
                        out[o + 2] = b;
                        out[o + 3] = a;
                    }
                }
            }
        }
    }
}

}  // namespace

std::optional<DecodedTexture> decode_tpl(std::span<const std::byte> data) noexcept try {
    if (data.size() < 12 || read_be32(data, 0) != 0x0020AF30u) {
        return std::nullopt;
    }
    const std::uint32_t num_images = read_be32(data, 4);
    const std::size_t table_off = read_be32(data, 8);
    if (num_images == 0 || num_images > (data.size() / 8u) ||
        !contains_bytes(data, table_off, static_cast<std::size_t>(num_images) * 8u)) {
        return std::nullopt;
    }
    const std::size_t header_off = read_be32(data, table_off);
    if (header_off == 0 || !contains_bytes(data, header_off, 36u)) {
        return std::nullopt;
    }
    const std::uint16_t height = read_be16(data, header_off);
    const std::uint16_t width = read_be16(data, header_off + 2);
    const std::uint32_t format = read_be32(data, header_off + 4);
    const std::uint32_t data_off = read_be32(data, header_off + 8);
    if (width == 0 || height == 0 || width > 4096 || height > 4096) {
        return std::nullopt;
    }

    unsigned tile_width = 0;
    unsigned tile_height = 0;
    switch (format) {
        case 0: tile_width = 8; tile_height = 8; break;
        case 2: tile_width = 8; tile_height = 4; break;
        case 5: tile_width = 4; tile_height = 4; break;
        default: return std::nullopt;
    }
    const std::size_t encoded_size =
        ((static_cast<std::size_t>(width) + tile_width - 1u) / tile_width) *
        ((static_cast<std::size_t>(height) + tile_height - 1u) / tile_height) * 32u;
    if (!contains_bytes(data, data_off, encoded_size)) {
        return std::nullopt;
    }

    DecodedTexture result{};
    result.width = width;
    result.height = height;
    result.rgba8.assign(static_cast<std::size_t>(width) * height * 4u, 0u);

    switch (format) {
        case 0:
            decode_i4(data, data_off, width, height, result.rgba8);
            break;
        case 2:
            decode_ia4(data, data_off, width, height, result.rgba8);
            break;
        case 5:
            decode_rgb5a3(data, data_off, width, height, result.rgba8);
            break;
        default:
            // Not a format this reader has been verified against.
            return std::nullopt;
    }
    return result;
} catch (...) {
    return std::nullopt;
}

std::optional<DecodedTexture> load_game_texture(
    const std::string& content_root_utf8,
    std::string_view arc_relative_path,
    std::string_view texture_entry_name) noexcept try {
    std::string full_path = content_root_utf8;
    if (!full_path.empty() && full_path.back() != '\\' &&
        full_path.back() != '/') {
        full_path += '\\';
    }
    full_path += std::string(arc_relative_path);

    std::ifstream file(full_path, std::ios::binary | std::ios::ate);
    if (!file) {
        return std::nullopt;
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || static_cast<std::uintmax_t>(size) > kMaxCosmeticArchiveSize) {
        return std::nullopt;
    }
    file.seekg(0);
    std::vector<std::byte> raw(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(raw.data()), size)) {
        return std::nullopt;
    }

    std::vector<std::byte> decompressed;
    std::span<const std::byte> archive_bytes = raw;
    if (raw.size() >= 4 && std::memcmp(raw.data(), "Yaz0", 4) == 0) {
        decompressed = yaz0_decompress(raw);
        if (decompressed.empty()) {
            return std::nullopt;
        }
        archive_bytes = decompressed;
    }

    const std::optional<std::span<const std::byte>> entry =
        rarc_find_file(archive_bytes, texture_entry_name);
    if (!entry.has_value()) {
        return std::nullopt;
    }
    return decode_tpl(*entry);
} catch (...) {
    // Cosmetic extraction failures must not reach the renderer's failure path.
    return std::nullopt;
}

}  // namespace galaxy::gx
