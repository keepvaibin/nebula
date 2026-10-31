// The settings overlay's game art from the cooked content (game_asset_extract.h): an archive's file, a TPL's
// texture record.

#include "galaxy/render/game_asset_extract.h"
#include "nebula_cooked_texture.h"

#include <bit>
#include <cstring>
#include <fstream>

namespace galaxy::render {

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

}  // namespace

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

std::optional<DecodedTexture> tpl_texture(std::span<const std::byte> data) noexcept try {
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
    const std::size_t data_off = read_be32(data, header_off + 8);
    if (!contains_bytes(data, data_off, NEBULA_TEXTURE_HEADER)) {
        return std::nullopt;
    }
    const NebulaTexture* record = nebula_texture_record(data.data() + data_off);
    if (record == nullptr || record->width != width || record->height != height || width == 0 || height == 0 ||
        record->layout > NEBULA_TEXTURE_LA8 || !contains_bytes(data, data_off, record->size) ||
        NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(width, height, 1u, record->layout) > record->size) {
        return std::nullopt;
    }
    DecodedTexture result{};
    result.width = width;
    result.height = height;
    const std::size_t count = static_cast<std::size_t>(width) * height;
    result.rgba8.resize(count * 4u);
    const auto* texels = reinterpret_cast<const std::uint8_t*>(record) + NEBULA_TEXTURE_HEADER;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t* out = result.rgba8.data() + i * 4u;
        if (record->layout == NEBULA_TEXTURE_L8) {
            out[0] = out[1] = out[2] = out[3] = texels[i];
        } else if (record->layout == NEBULA_TEXTURE_LA8) {
            out[0] = out[1] = out[2] = texels[i * 2u];
            out[3] = texels[i * 2u + 1u];
        } else {
            std::memcpy(out, texels + i * 4u, 4u);
        }
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

    const std::optional<std::span<const std::byte>> entry = rarc_find_file(raw, texture_entry_name);
    if (!entry.has_value()) {
        return std::nullopt;
    }
    return tpl_texture(*entry);
} catch (...) {
    // Cosmetic extraction failures must not reach the renderer's failure path.
    return std::nullopt;
}

}  // namespace galaxy::render
