// The settings overlay's game art from the asset store (game_asset_extract.h): a file below a folder, a TPL's
// texture record.

#include "galaxy/render/game_asset_extract.h"
#include "galaxy/disc_files.h"
#include "galaxy/path_text.h"
#include "nebula_cooked_texture.h"

#include <bit>
#include <cstring>
#include <filesystem>

namespace galaxy::render {

namespace {

constexpr std::uint64_t kMaxCosmeticFileSize = 64u * 1024u * 1024u;

[[nodiscard]] bool contains_bytes(
    std::span<const std::byte> data, std::size_t offset,
    std::size_t count) noexcept {
    return offset <= data.size() && count <= data.size() - offset;
}

// The content folder's archives and TPLs are in host order, as the importer cooks them (cook/native_data.rs).
[[nodiscard]] std::uint32_t read_u32(
    std::span<const std::byte> data, std::size_t offset) noexcept {
    if (!contains_bytes(data, offset, 4)) {
        return 0;
    }
    return static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

[[nodiscard]] std::uint16_t read_u16(
    std::span<const std::byte> data, std::size_t offset) noexcept {
    if (!contains_bytes(data, offset, 2)) {
        return 0;
    }
    return static_cast<std::uint16_t>(
        static_cast<std::uint32_t>(data[offset]) |
        (static_cast<std::uint32_t>(data[offset + 1]) << 8));
}

// The file of that exact name at any depth below `folder`.
[[nodiscard]] std::optional<disc::Entry> find_file(
    const disc::AssetStore& store, const disc::Entry& folder, std::string_view name) {
    for (std::uint32_t i = 0; i < folder.children; ++i) {
        const std::optional<disc::Entry> entry = store.child(folder.index, i);
        if (!entry) {
            return std::nullopt;
        }
        if (entry->folder) {
            if (std::optional<disc::Entry> found = find_file(store, *entry, name)) {
                return found;
            }
        } else if (name == entry->name) {
            return entry;
        }
    }
    return std::nullopt;
}

}  // namespace

std::optional<DecodedTexture> tpl_texture(std::span<const std::byte> data) noexcept try {
    if (data.size() < 12 || read_u32(data, 0) != 0x0020AF30u) {
        return std::nullopt;
    }
    const std::uint32_t num_images = read_u32(data, 4);
    const std::size_t table_off = read_u32(data, 8);
    if (num_images == 0 || num_images > (data.size() / 8u) ||
        !contains_bytes(data, table_off, static_cast<std::size_t>(num_images) * 8u)) {
        return std::nullopt;
    }
    const std::size_t header_off = read_u32(data, table_off);
    if (header_off == 0 || !contains_bytes(data, header_off, 36u)) {
        return std::nullopt;
    }
    const std::uint16_t height = read_u16(data, header_off);
    const std::uint16_t width = read_u16(data, header_off + 2);
    const std::size_t data_off = read_u32(data, header_off + 8);
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
    std::string_view folder,
    std::string_view texture_entry_name) noexcept try {
    disc::AssetStore store;
    if (!store.open(text_path(content_root_utf8) / "game.pak")) {
        return std::nullopt;
    }
    const std::optional<disc::Entry> root = store.find(folder);
    if (!root || !root->folder) {
        return std::nullopt;
    }
    const std::optional<disc::Entry> file = find_file(store, *root, texture_entry_name);
    if (!file || file->length > kMaxCosmeticFileSize) {
        return std::nullopt;
    }
    std::vector<std::byte> raw(static_cast<std::size_t>(file->length));
    if (!store.read(file->position, raw.size(), raw.data())) {
        return std::nullopt;
    }
    return tpl_texture(raw);
} catch (...) {
    // Cosmetic extraction failures must not reach the renderer's failure path.
    return std::nullopt;
}

}  // namespace galaxy::render
