#include "galaxy/gx/game_asset_extract.h"

#include <algorithm>
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

Bytes tpl(unsigned format, unsigned width, unsigned height, std::size_t tile_bytes) {
    Bytes bytes(0x40u + tile_bytes);
    put32(bytes, 0, 0x0020AF30u);
    put32(bytes, 4, 1);
    put32(bytes, 8, 12);
    put32(bytes, 12, 20);
    put16(bytes, 20, height);
    put16(bytes, 22, width);
    put32(bytes, 24, format);
    put32(bytes, 28, 0x40);
    return bytes;
}

void expect_pixel(const galaxy::gx::DecodedTexture& texture, unsigned x,
                  unsigned y, std::array<std::uint8_t, 4> expected) {
    const auto offset = (static_cast<std::size_t>(y) * texture.width + x) * 4;
    for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        require(texture.rgba8.at(offset + channel) == expected[channel],
                "decoded pixel differs from independent expected channels");
    }
}

void test_pixels() {
    // Every I4 nibble, including transparent zero, in tiled scan order.
    auto i4 = tpl(0, 8, 8, 32);
    for (unsigned i = 0; i < 8; ++i) {
        i4[0x40 + i] = static_cast<std::byte>((2 * i << 4) | (2 * i + 1));
    }
    const auto decoded_i4 = galaxy::gx::decode_tpl(i4);
    require(decoded_i4.has_value(), "valid I4 rejected");
    for (unsigned i = 0; i < 16; ++i) {
        const auto value = static_cast<std::uint8_t>(i * 17);
        expect_pixel(*decoded_i4, i % 8, i / 8, {value, value, value, value});
    }

    auto ia4 = tpl(2, 1, 1, 32);
    ia4[0x40] = std::byte{0xA3};
    const auto decoded_ia4 = galaxy::gx::decode_tpl(ia4);
    require(decoded_ia4.has_value(), "valid IA4 rejected");
    expect_pixel(*decoded_ia4, 0, 0, {51, 51, 51, 170});

    // Literal expansion tables catch floor(value*255/max) approximations.
    constexpr std::array<std::uint8_t, 32> expanded5 = {
        0, 8, 16, 24, 33, 41, 49, 57, 66, 74, 82, 90, 99, 107, 115, 123,
        132, 140, 148, 156, 165, 173, 181, 189, 198, 206, 214, 222, 231, 239, 247, 255};
    constexpr std::array<std::uint8_t, 8> expanded3 = {0, 36, 73, 109, 146, 182, 219, 255};
    for (unsigned value = 0; value < 32; ++value) {
        auto rgb = tpl(5, 1, 1, 32);
        put16(rgb, 0x40, 0x8000u | (value << 10) | (value << 5) | value);
        const auto decoded = galaxy::gx::decode_tpl(rgb);
        require(decoded.has_value(), "valid opaque RGB5A3 rejected");
        expect_pixel(*decoded, 0, 0, {expanded5[value], expanded5[value], expanded5[value], 255});
    }
    for (unsigned alpha = 0; alpha < 8; ++alpha) {
        auto rgb = tpl(5, 1, 1, 32);
        put16(rgb, 0x40, (alpha << 12) | 0x012Fu);
        const auto decoded = galaxy::gx::decode_tpl(rgb);
        require(decoded.has_value(), "valid alpha RGB5A3 rejected");
        expect_pixel(*decoded, 0, 0, {17, 34, 255, expanded3[alpha]});
    }

    for (const unsigned format : {0u, 2u, 5u}) {
        const unsigned tile_width = format == 5 ? 4u : 8u;
        const unsigned tile_height = format == 0 ? 8u : 4u;
        auto bytes = tpl(format, tile_width + 1, tile_height + 1, 128);
        for (unsigned tile = 0; tile < 4; ++tile) {
            for (unsigned i = 0; i < 32; ++i) {
                bytes[0x40 + tile * 32 + i] = static_cast<std::byte>((tile + 1) * 0x11);
            }
        }
        const auto decoded = galaxy::gx::decode_tpl(bytes);
        require(decoded.has_value(), "complete partial-edge tiles rejected");
        require(decoded->rgba8.size() == static_cast<std::size_t>(tile_width + 1) * (tile_height + 1) * 4,
                "logical output includes tile padding");
        if (format != 5) {
            expect_pixel(*decoded, tile_width, tile_height, {68, 68, 68, 68});
        } else {
            expect_pixel(*decoded, tile_width, tile_height, {68, 68, 68, 146});
        }
        bytes.pop_back();
        require(!galaxy::gx::decode_tpl(bytes), "truncated edge tile fabricated pixels");
    }
}

void test_malformed_tpl() {
    const auto valid = tpl(0, 1, 1, 32);
    for (std::size_t size = 0; size < valid.size(); ++size) {
        require(!galaxy::gx::decode_tpl(std::span(valid).first(size)),
                "truncated TPL accepted");
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, std::uint32_t>, 8>{
             {{0, 0}, {4, 0}, {4, 0xFFFFFFFFu}, {8, 0xFFFFFFFCu},
              {12, 0xFFFFFFFCu}, {12, 0}, {24, 6}, {28, 0xFFFFFFFFu}}}) {
        auto bytes = valid;
        put32(bytes, offset, value);
        require(!galaxy::gx::decode_tpl(bytes), "invalid TPL field accepted");
    }
    for (const unsigned dimension : {0u, 4097u}) {
        auto bytes = valid;
        put16(bytes, 22, dimension);
        require(!galaxy::gx::decode_tpl(bytes), "invalid TPL dimension accepted");
    }
    auto incomplete_table = valid;
    put32(incomplete_table, 4, 2);
    put32(incomplete_table, 8, static_cast<std::uint32_t>(valid.size() - 8));
    require(!galaxy::gx::decode_tpl(incomplete_table), "incomplete descriptor array accepted");
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
    const auto texture = tpl(2, 1, 1, 32);
    const auto bytes = archive(texture);
    require(galaxy::gx::rarc_find_file(bytes, "image.tpl").has_value(), "valid archive lookup failed");
    require(!galaxy::gx::rarc_find_file(bytes, "IMAGE.TPL"), "archive lookup lost case sensitivity");
    for (const std::size_t offset : {0x0Cu, 0x24u, 0x2Cu, 0x34u}) {
        auto corrupt = bytes;
        put32(corrupt, offset, 0xFFFFFFE0u);
        require(!galaxy::gx::rarc_find_file(corrupt, "image.tpl"), "wrapped archive offset accepted");
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
    require(!galaxy::gx::load_game_texture(root_string, "missing.arc", "image.tpl"), "missing file accepted");
    write(bytes);
    require(galaxy::gx::load_game_texture(root_string, "asset.arc", "image.tpl").has_value(), "valid disk archive failed");
    auto corrupt = archive(tpl(0, 1, 1, 31));
    write(corrupt);
    require(!galaxy::gx::load_game_texture(root_string, "asset.arc", "image.tpl"), "bad file reused successful texture");
    std::filesystem::resize_file(root / "asset.arc", 64u * 1024u * 1024u + 1u);
    require(!galaxy::gx::load_game_texture(root_string, "asset.arc", "image.tpl"), "oversized cosmetic archive accepted");
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
