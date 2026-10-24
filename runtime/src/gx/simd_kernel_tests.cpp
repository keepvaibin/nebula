// SPDX-License-Identifier: GPL-3.0-only
// Tests the vector code (galaxy/simd128.h: SSE on x86, NEON on AArch64) in the texture tile decoders and cached index
// rebasing against scalar references written from the GX formats.

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/vertex_loader.h"
#include "galaxy/simd128.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace {

using galaxy::gx::RGBA8;
using galaxy::gx::TexFormat;

int g_failures = 0;

void fail(const char* what, std::uint32_t width, std::uint32_t height, std::uint32_t x, std::uint32_t y,
          const RGBA8& got, const RGBA8& want) {
    if (++g_failures <= 20) {
        std::printf("FAIL %s %ux%u at (%u,%u): got %02x%02x%02x%02x want %02x%02x%02x%02x\n", what, width, height, x,
                    y, got.r, got.g, got.b, got.a, want.r, want.g, want.b, want.a);
    }
}

std::uint32_t g_seed = 0x9E3779B9u;
std::uint8_t random_byte() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return static_cast<std::uint8_t>(g_seed >> 24);
}

std::uint8_t replicate(std::uint32_t value, int bits) {
    switch (bits) {
    case 3: return static_cast<std::uint8_t>((value << 5) | (value << 2) | (value >> 1));
    case 4: return static_cast<std::uint8_t>(value * 17u);
    case 5: return static_cast<std::uint8_t>((value << 3) | (value >> 2));
    default: return static_cast<std::uint8_t>((value << 2) | (value >> 4));
    }
}

RGBA8 rgb565(std::uint16_t v) { return {replicate(v >> 11, 5), replicate((v >> 5) & 63u, 6), replicate(v & 31u, 5), 255}; }

RGBA8 rgb5a3(std::uint16_t v) {
    if ((v & 0x8000u) != 0u) {
        return {replicate((v >> 10) & 31u, 5), replicate((v >> 5) & 31u, 5), replicate(v & 31u, 5), 255};
    }
    return {replicate((v >> 8) & 15u, 4), replicate((v >> 4) & 15u, 4), replicate(v & 15u, 4), replicate((v >> 12) & 7u, 3)};
}

struct Layout {
    std::uint32_t block_width;
    std::uint32_t block_height;
    std::uint32_t block_bytes;
};

Layout layout(TexFormat format) {
    switch (format) {
    case TexFormat::I4: return {8, 8, 32};
    case TexFormat::I8:
    case TexFormat::IA4: return {8, 4, 32};
    case TexFormat::RGBA8: return {4, 4, 64};
    default: return {4, 4, 32};
    }
}

// The texel at (x, y) of a tiled GX texture, from the format's definition.
RGBA8 reference_texel(TexFormat format, const std::vector<std::uint8_t>& source, std::uint32_t width, std::uint32_t x,
                      std::uint32_t y) {
    const Layout l = layout(format);
    const std::uint32_t blocks_x = (width + l.block_width - 1u) / l.block_width;
    const std::size_t block = static_cast<std::size_t>(y / l.block_height) * blocks_x + x / l.block_width;
    const std::uint8_t* tile = source.data() + block * l.block_bytes;
    const std::uint32_t tx = x % l.block_width;
    const std::uint32_t ty = y % l.block_height;
    switch (format) {
    case TexFormat::I4: {
        const std::uint8_t byte = tile[ty * 4u + tx / 2u];
        const std::uint8_t i = replicate((tx & 1u) == 0u ? byte >> 4 : byte & 15u, 4);
        return {i, i, i, i};
    }
    case TexFormat::I8: {
        const std::uint8_t i = tile[ty * 8u + tx];
        return {i, i, i, i};
    }
    case TexFormat::IA4: {
        const std::uint8_t byte = tile[ty * 8u + tx];
        const std::uint8_t i = replicate(byte & 15u, 4);
        return {i, i, i, replicate(byte >> 4, 4)};
    }
    case TexFormat::IA8: {
        const std::uint8_t* pair = tile + (ty * 4u + tx) * 2u;
        return {pair[1], pair[1], pair[1], pair[0]};
    }
    case TexFormat::RGB565:
    case TexFormat::RGB5A3: {
        const std::uint8_t* word = tile + (ty * 4u + tx) * 2u;
        const auto v = static_cast<std::uint16_t>((word[0] << 8) | word[1]);
        return format == TexFormat::RGB565 ? rgb565(v) : rgb5a3(v);
    }
    default: {
        const std::uint8_t* ar = tile + (ty * 4u + tx) * 2u;
        const std::uint8_t* gb = ar + 32u;
        return {ar[1], gb[0], gb[1], ar[0]};
    }
    }
}

void check_texture(TexFormat format, const char* name, std::uint32_t width, std::uint32_t height,
                   const std::vector<std::uint8_t>* fixed = nullptr) {
    const Layout l = layout(format);
    const std::size_t blocks = static_cast<std::size_t>((width + l.block_width - 1u) / l.block_width) *
                               ((height + l.block_height - 1u) / l.block_height);
    std::vector<std::uint8_t> source = fixed != nullptr ? *fixed : std::vector<std::uint8_t>(blocks * l.block_bytes);
    if (fixed == nullptr) {
        for (std::uint8_t& byte : source) byte = random_byte();
    }
    std::vector<RGBA8> out(static_cast<std::size_t>(width) * height, RGBA8{1, 2, 3, 4});
    switch (format) {
    case TexFormat::RGB565:
    case TexFormat::RGB5A3: galaxy::gx::detail::decode_color_tiles(format, source.data(), width, height, out.data()); break;
    case TexFormat::RGBA8: galaxy::gx::detail::decode_rgba8_tiles(source.data(), width, height, out.data()); break;
    default: galaxy::gx::detail::decode_intensity_tiles(format, source.data(), width, height, out.data()); break;
    }
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const RGBA8 want = reference_texel(format, source, width, x, y);
            const RGBA8& got = out[static_cast<std::size_t>(y) * width + x];
            if (got.r != want.r || got.g != want.g || got.b != want.b || got.a != want.a) {
                fail(name, width, height, x, y, got, want);
            }
        }
    }
}

// Every 16-bit value once: a 256x256 texture whose texels in tile order are 0..65535.
std::vector<std::uint8_t> every_word_texture() {
    std::vector<std::uint8_t> source(65536u * 2u);
    for (std::uint32_t v = 0; v < 65536u; ++v) {
        source[v * 2u] = static_cast<std::uint8_t>(v >> 8);
        source[v * 2u + 1u] = static_cast<std::uint8_t>(v);
    }
    return source;
}

void check_rebase() {
    std::vector<std::uint16_t> source(29);
    for (std::size_t i = 0; i < source.size(); ++i) source[i] = static_cast<std::uint16_t>(i * 2000u);
    std::vector<std::uint16_t> out(source.size());
    // The largest index is 56000: a bias up to 9535 fits, one more overflows.
    if (!galaxy::gx::detail::rebase_cached_indices(source, 9535u, out.data())) {
        ++g_failures;
        std::printf("FAIL rebase rejected a bias that fits\n");
    }
    for (std::size_t i = 0; i < source.size(); ++i) {
        if (out[i] != source[i] + 9535u && ++g_failures <= 20) std::printf("FAIL rebase index %zu\n", i);
    }
    for (std::size_t at : {std::size_t{0}, std::size_t{7}, std::size_t{8}, std::size_t{28}}) {
        std::vector<std::uint16_t> one(source.size(), 100u);
        one[at] = 65000u;
        if (galaxy::gx::detail::rebase_cached_indices(one, 536u, out.data()) && ++g_failures <= 20) {
            std::printf("FAIL rebase accepted an overflow at %zu\n", at);
        }
    }
}

}  // namespace

int main() {
    const std::vector<std::uint8_t> words = every_word_texture();
    check_texture(TexFormat::RGB565, "RGB565 every value", 256, 256, &words);
    check_texture(TexFormat::RGB5A3, "RGB5A3 every value", 256, 256, &words);
    struct Format {
        TexFormat format;
        const char* name;
    };
    const Format formats[] = {{TexFormat::I4, "I4"},         {TexFormat::I8, "I8"},
                              {TexFormat::IA4, "IA4"},       {TexFormat::IA8, "IA8"},
                              {TexFormat::RGB565, "RGB565"}, {TexFormat::RGB5A3, "RGB5A3"},
                              {TexFormat::RGBA8, "RGBA8"}};
    const std::uint32_t sizes[][2] = {{8, 8}, {64, 32}, {16, 24}, {13, 7}, {1, 1}, {33, 5}, {4, 9}};
    for (const Format& f : formats) {
        for (const auto& size : sizes) check_texture(f.format, f.name, size[0], size[1]);
    }
    check_rebase();
    std::printf("simd kernel tests (%s): %s\n", GALAXY_SIMD_NEON ? "NEON" : "SSE", g_failures == 0 ? "pass" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
