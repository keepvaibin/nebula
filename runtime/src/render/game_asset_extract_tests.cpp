// Game asset extraction: cooked TPL texture records, malformed containers, RARC lookup and the disk loader.

#include "galaxy/render/game_asset_extract.h"
#include "nebula_cooked_texture.h"

#include <algorithm>
#include <cstring>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {
using Bytes = std::vector<std::byte>;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void put16(Bytes& bytes, std::size_t offset, unsigned value) {
    bytes.at(offset) = static_cast<std::byte>((value >> 8) & 255u);
    bytes.at(offset + 1) = static_cast<std::byte>(value & 255u);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    put16(bytes, offset, value >> 16);
    put16(bytes, offset + 2, value & 65535u);
}

// A cooked TPL: one image whose data is a texture record of `layout` holding `texels` (the level's bytes).
Bytes tpl(unsigned width, unsigned height, std::uint8_t layout, const std::vector<std::uint8_t>& texels) {
    NebulaTexture record;
    nebula_texture_init(&record, static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height), 1u, layout, 0u);
    Bytes bytes(0x40u + record.size);
    put32(bytes, 0, 0x0020AF30u);
    put32(bytes, 4, 1);
    put32(bytes, 8, 12);
    put32(bytes, 12, 20);
    put16(bytes, 20, height);
    put16(bytes, 22, width);
    put32(bytes, 24, 6);
    put32(bytes, 28, 0x40);
    std::memcpy(bytes.data() + 0x40, &record, sizeof(record));
    std::memcpy(bytes.data() + 0x40 + sizeof(record), texels.data(), std::min<std::size_t>(texels.size(), record.size - sizeof(record)));
    return bytes;
}

void expect_pixel(const galaxy::render::DecodedTexture& texture, unsigned x,
                  unsigned y, std::array<std::uint8_t, 4> expected) {
    const auto offset = (static_cast<std::size_t>(y) * texture.width + x) * 4;
    for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        require(texture.rgba8.at(offset + channel) == expected[channel],
                "texel differs from its record's");
    }
}

void test_pixels() {
    const auto l8 = galaxy::render::tpl_texture(tpl(2, 1, NEBULA_TEXTURE_L8, {7, 200}));
    require(l8.has_value() && l8->width == 2 && l8->height == 1, "valid L8 record rejected");
    expect_pixel(*l8, 0, 0, {7, 7, 7, 7});
    expect_pixel(*l8, 1, 0, {200, 200, 200, 200});
    const auto la8 = galaxy::render::tpl_texture(tpl(1, 1, NEBULA_TEXTURE_LA8, {51, 170}));
    require(la8.has_value(), "valid LA8 record rejected");
    expect_pixel(*la8, 0, 0, {51, 51, 51, 170});
    const auto rgba = galaxy::render::tpl_texture(tpl(1, 2, NEBULA_TEXTURE_RGBA8, {1, 2, 3, 4, 5, 6, 7, 8}));
    require(rgba.has_value(), "valid RGBA8 record rejected");
    expect_pixel(*rgba, 0, 1, {5, 6, 7, 8});
}

void test_malformed_tpl() {
    const auto valid = tpl(1, 1, NEBULA_TEXTURE_L8, {9});
    for (std::size_t size = 0; size < valid.size(); ++size) {
        require(!galaxy::render::tpl_texture(std::span(valid).first(size)), "truncated TPL accepted");
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, std::uint32_t>, 6>{
             {{0, 0}, {4, 0}, {4, 0xFFFFFFFFu}, {8, 0xFFFFFFFCu}, {12, 0}, {28, 0xFFFFFFFFu}}}) {
        auto bytes = valid;
        put32(bytes, offset, value);
        require(!galaxy::render::tpl_texture(bytes), "invalid TPL field accepted");
    }
    auto other_size = valid;
    put16(other_size, 22, 2);
    require(!galaxy::render::tpl_texture(other_size), "a record of another size accepted");
    auto uncooked = valid;
    uncooked[0x40] = std::byte{0};
    require(!galaxy::render::tpl_texture(uncooked), "a TPL packed before cooking accepted");
}

Bytes archive(const Bytes& texture) {
    Bytes bytes(0x80 + texture.size());
    bytes[0] = std::byte{'R'}; bytes[1] = std::byte{'A'};
    bytes[2] = std::byte{'R'}; bytes[3] = std::byte{'C'};
    put32(bytes, 0x0C, 0x60); // File data at 0x80, relative to 0x20.
    put32(bytes, 0x20, 1);
    put32(bytes, 0x24, 0x20); // Node at 0x40.
    put32(bytes, 0x2C, 0x30); // Entry at 0x50.
    put32(bytes, 0x34, 0x44); // String at 0x64.
    put16(bytes, 0x4A, 1);
    put32(bytes, 0x54, 0x00010000);
    put32(bytes, 0x5C, static_cast<std::uint32_t>(texture.size()));
    constexpr char name[] = "image.tpl";
    for (std::size_t i = 0; i < sizeof(name); ++i) {
        bytes[0x64 + i] = static_cast<std::byte>(name[i]);
    }
    std::copy(texture.begin(), texture.end(), bytes.begin() + 0x80);
    return bytes;
}

void test_archive_and_loader() {
    const auto texture = tpl(1, 1, NEBULA_TEXTURE_LA8, {1, 2});
    const auto bytes = archive(texture);
    require(galaxy::render::rarc_find_file(bytes, "image.tpl").has_value(), "valid archive lookup failed");
    require(!galaxy::render::rarc_find_file(bytes, "IMAGE.TPL"), "archive lookup lost case sensitivity");
    for (const std::size_t offset : {0x0Cu, 0x24u, 0x2Cu, 0x34u}) {
        auto corrupt = bytes;
        put32(corrupt, offset, 0xFFFFFFE0u);
        require(!galaxy::render::rarc_find_file(corrupt, "image.tpl"), "wrapped archive offset accepted");
    }

    const auto root = std::filesystem::temp_directory_path() /
        ("galaxy-asset-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    require(std::filesystem::create_directory(root), "fresh loader fixture directory unavailable");
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(path, ignored); }
    } cleanup{root};
    const auto write = [&](const Bytes& contents) {
        std::ofstream output(root / "asset.arc", std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(contents.data()), static_cast<std::streamsize>(contents.size()));
        require(static_cast<bool>(output), "archive fixture write failed");
    };
    const auto root_string = root.string() + "/";
    require(!galaxy::render::load_game_texture(root_string, "missing.arc", "image.tpl"), "missing file accepted");
    write(bytes);
    require(galaxy::render::load_game_texture(root_string, "asset.arc", "image.tpl").has_value(), "valid disk archive failed");
    auto broken = texture;
    broken.resize(broken.size() - 1u);
    auto corrupt = archive(broken);
    write(corrupt);
    require(!galaxy::render::load_game_texture(root_string, "asset.arc", "image.tpl"), "bad file reused successful texture");
    std::filesystem::resize_file(root / "asset.arc", 64u * 1024u * 1024u + 1u);
    require(!galaxy::render::load_game_texture(root_string, "asset.arc", "image.tpl"), "oversized cosmetic archive accepted");
}
} // namespace

int main() {
    try {
        test_pixels();
        test_malformed_tpl();
        test_archive_and_loader();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
