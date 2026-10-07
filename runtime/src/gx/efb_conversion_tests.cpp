#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#pragma warning(pop)

#include "galaxy/gx/efb_copy.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/pipeline_cache.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/texture_cache.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

struct Pixel {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    bool operator==(const Pixel&) const = default;
};

#pragma warning(push)
#pragma warning(disable : 4324)
struct GxPixelCase {
    galaxy::gx::PixelShaderKey shader{};
    galaxy::gx::RenderStateKey state{};
    galaxy::gx::GxPsConstants constants{};
    Pixel texture{18u, 52u, 86u, 120u};
};
#pragma warning(pop)
struct GxPixelResult { Pixel color{}; float depth = 0.0f; };

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        return false;
    }
    return true;
}

bool expect_pixel(Pixel actual, Pixel expected, const char* message) {
    if (actual == expected) {
        return true;
    }
    std::fprintf(
        stderr,
        "FAILED: %s actual=(%u,%u,%u,%u) expected=(%u,%u,%u,%u)\n",
        message,
        static_cast<unsigned>(actual.r),
        static_cast<unsigned>(actual.g),
        static_cast<unsigned>(actual.b),
        static_cast<unsigned>(actual.a),
        static_cast<unsigned>(expected.r),
        static_cast<unsigned>(expected.g),
        static_cast<unsigned>(expected.b),
        static_cast<unsigned>(expected.a));
    return false;
}

bool test_intensity_tile_decode() {
    using galaxy::gx::RGBA8;
    using galaxy::gx::TexFormat;
    constexpr std::array<std::uint32_t, 12> dimensions{
        1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 13u, 16u, 31u, 32u};
    constexpr RGBA8 guard{0xD3u, 0x62u, 0xA5u, 0x18u};
    const auto same = [](RGBA8 a, RGBA8 b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    const auto check = [&](TexFormat format, std::uint32_t width,
                           std::uint32_t height, std::uint32_t prefix) {
        const std::uint32_t tile_width = format == TexFormat::IA8 ? 4u : 8u;
        const std::uint32_t tile_height = format == TexFormat::I4 ? 8u : 4u;
        const auto blocks_x = (width + tile_width - 1u) / tile_width;
        const auto blocks_y = (height + tile_height - 1u) / tile_height;
        const auto byte_count = blocks_x * blocks_y * 32u;
        std::vector<std::uint8_t> source(byte_count + prefix, 0xEDu);
        for (std::uint32_t i = 0; i < byte_count; ++i) {
            if (format == TexFormat::IA8) {
                // 256x256 covers all 65,536 (A,I) pairs, not just matching
                // or correlated channels. Smaller cases retain tile variation.
                const auto word = i / 2u;
                source[prefix + i] = static_cast<std::uint8_t>(
                    (i & 1u) == 0u ? word >> 8u : word);
            } else {
                // Odd stride traverses every possible byte value.
                source[prefix + i] = static_cast<std::uint8_t>(i * 73u + 91u);
            }
        }
        std::vector<RGBA8> output(width * height + 2u, guard);
        galaxy::gx::detail::decode_intensity_tiles(
            format, source.data() + prefix, width, height, output.data() + 1u);
        if (!expect(same(output.front(), guard) && same(output.back(), guard),
                    "intensity edge tiles stay inside output")) {
            return false;
        }
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                // Independent global-pixel address oracle includes padding
                // in every tile; it does not traverse the decoder's row loops.
                const auto tile = (y / tile_height) * blocks_x + x / tile_width;
                const auto pixel = (y % tile_height) * tile_width + x % tile_width;
                const auto offset = prefix + tile * 32u;
                std::uint8_t intensity = 0u;
                std::uint8_t alpha = 0u;
                if (format == TexFormat::I4) {
                    const auto byte = source[offset + pixel / 2u];
                    const auto nibble = static_cast<std::uint8_t>(
                        ((pixel & 1u) == 0u ? byte >> 4u : byte) & 15u);
                    intensity = static_cast<std::uint8_t>((nibble << 4u) | nibble);
                    alpha = intensity;
                } else if (format == TexFormat::I8) {
                    intensity = alpha = source[offset + pixel];
                } else if (format == TexFormat::IA4) {
                    const auto byte = source[offset + pixel];
                    const auto i = static_cast<std::uint8_t>(byte & 15u);
                    const auto a = static_cast<std::uint8_t>(byte >> 4u);
                    intensity = static_cast<std::uint8_t>((i << 4u) | i);
                    alpha = static_cast<std::uint8_t>((a << 4u) | a);
                } else {
                    alpha = source[offset + pixel * 2u];
                    intensity = source[offset + pixel * 2u + 1u];
                }
                if (!expect(same(output[1u + y * width + x],
                                 RGBA8{intensity, intensity, intensity, alpha}),
                            "intensity tiled bytes, channel order and alpha match oracle")) {
                    return false;
                }
            }
        }
        return true;
    };
    for (const auto format : {TexFormat::I4, TexFormat::I8,
                              TexFormat::IA4, TexFormat::IA8}) {
        for (const auto width : dimensions) {
            for (const auto height : dimensions) {
                for (const auto prefix : {0u, 1u, 3u}) {
                    if (!check(format, width, height, prefix)) {
                        return false;
                    }
                }
            }
        }
        galaxy::gx::detail::decode_intensity_tiles(format, nullptr, 0u, 0u, nullptr);
        galaxy::gx::detail::decode_intensity_tiles(format, nullptr, 0u, 8u, nullptr);
        galaxy::gx::detail::decode_intensity_tiles(format, nullptr, 8u, 0u, nullptr);
    }
    return check(TexFormat::IA8, 256u, 256u, 1u);
}

bool test_rgba8_tile_decode() {
    using galaxy::gx::RGBA8;
    constexpr std::array<std::uint32_t, 12> dimensions{
        1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 13u, 16u, 31u, 32u};
    constexpr RGBA8 guard{0xD3u, 0x62u, 0xA5u, 0x18u};
    const auto same = [](RGBA8 a, RGBA8 b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    for (const auto width : dimensions) {
        for (const auto height : dimensions) {
            const auto blocks_x = (width + 3u) / 4u;
            const auto blocks_y = (height + 3u) / 4u;
            const auto bytes = blocks_x * blocks_y * 64u;
            for (const auto prefix : {0u, 1u, 3u}) {
                std::vector<std::uint8_t> source(bytes + prefix, 0xEDu);
                for (std::uint32_t i = 0; i < bytes; ++i) {
                    source[prefix + i] = static_cast<std::uint8_t>(
                        (i * 37u + (i / 64u) * 19u + (i / 8u) * 7u) ^ 0xB3u);
                }
                std::vector<RGBA8> output(width * height + 2u, guard);
                galaxy::gx::detail::decode_rgba8_tiles(
                    source.data() + prefix, width, height, output.data() + 1u);
                if (!expect(same(output.front(), guard) && same(output.back(), guard),
                            "RGBA8 edge tiles stay inside output")) {
                    return false;
                }
                for (std::uint32_t y = 0; y < height; ++y) {
                    for (std::uint32_t x = 0; x < width; ++x) {
                        // Independent address oracle: global pixel -> tile ->
                        // local pixel, including the full padded source tiles.
                        const auto tile = (y / 4u) * blocks_x + x / 4u;
                        const auto pixel = (y % 4u) * 4u + x % 4u;
                        const auto offset = prefix + tile * 64u + pixel * 2u;
                        const RGBA8 expected{source[offset + 1u], source[offset + 32u],
                                             source[offset + 33u], source[offset]};
                        if (!expect(same(output[1u + y * width + x], expected),
                                    "RGBA8 tiled channels and row stride match oracle")) {
                            return false;
                        }
                    }
                }
            }
        }
    }
    galaxy::gx::detail::decode_rgba8_tiles(nullptr, 0u, 0u, nullptr);
    galaxy::gx::detail::decode_rgba8_tiles(nullptr, 0u, 8u, nullptr);
    galaxy::gx::detail::decode_rgba8_tiles(nullptr, 8u, 0u, nullptr);
    return true;
}

bool test_generated_mip_uniform_alpha() {
    using galaxy::gx::RGBA8;
    std::vector<RGBA8> actual;
    const auto check = [&](const std::vector<RGBA8>& source,
                           std::uint32_t width, std::uint32_t height) {
        const auto dest_width = std::max(1u, width / 2u);
        const auto dest_height = std::max(1u, height / 2u);
        galaxy::gx::detail::generate_rgba8_mip_level(
            source, width, height, actual, dest_width, dest_height);
        if (!expect(actual.size() == dest_width * dest_height,
                    "generated mip retains exact output extent")) return false;
        // Original scalar premultiplied-alpha rule, including thin/empty
        // footprints and integer rounding; no uniform-alpha special case.
        for (std::uint32_t y = 0u; y < dest_height; ++y) {
            for (std::uint32_t x = 0u; x < dest_width; ++x) {
                std::array<std::uint32_t, 4> sums{};
                std::uint32_t count = 0u;
                for (std::uint32_t sy = y * 2u; sy < y * 2u + 2u && sy < height; ++sy) {
                    for (std::uint32_t sx = x * 2u; sx < x * 2u + 2u && sx < width; ++sx) {
                        const auto pixel = source[static_cast<std::size_t>(sy) * width + sx];
                        sums[0] += pixel.r * pixel.a;
                        sums[1] += pixel.g * pixel.a;
                        sums[2] += pixel.b * pixel.a;
                        sums[3] += pixel.a;
                        ++count;
                    }
                }
                count = std::max(1u, count);
                const auto divisor = sums[3] != 0u ? sums[3] : count;
                const RGBA8 expected{
                    static_cast<std::uint8_t>((sums[0] + divisor / 2u) / divisor),
                    static_cast<std::uint8_t>((sums[1] + divisor / 2u) / divisor),
                    static_cast<std::uint8_t>((sums[2] + divisor / 2u) / divisor),
                    static_cast<std::uint8_t>((sums[3] + count / 2u) / count)};
                const auto pixel = actual[y * dest_width + x];
                if (!expect(pixel.r == expected.r && pixel.g == expected.g &&
                            pixel.b == expected.b && pixel.a == expected.a,
                            "generated mip bytes match original premultiplied-alpha oracle")) return false;
            }
        }
        return true;
    };
    std::vector<RGBA8> block(4u);
    // Every representable uniform alpha and RGB sum, including each rounding
    // boundary; zero-alpha inputs keep zero RGB rather than hidden colors.
    for (std::uint32_t alpha = 0u; alpha < 256u; ++alpha) {
        for (std::uint32_t sum = 0u; sum <= 1020u; ++sum) {
            auto remaining = sum;
            for (auto& pixel : block) {
                const auto value = std::min(255u, remaining);
                remaining -= value;
                pixel = {static_cast<std::uint8_t>(value),
                         static_cast<std::uint8_t>(255u - value),
                         static_cast<std::uint8_t>(value), static_cast<std::uint8_t>(alpha)};
            }
            if (!check(block, 2u, 2u)) return false;
        }
    }
    constexpr std::array<std::uint32_t, 9> dimensions{0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 17u};
    for (const auto width : dimensions) for (const auto height : dimensions) {
        std::vector<RGBA8> source(width * height);
        for (std::uint32_t pattern = 0u; pattern < 4u; ++pattern) {
            for (std::size_t i = 0u; i < source.size(); ++i) {
                source[i] = {static_cast<std::uint8_t>(i * 73u + 29u),
                             static_cast<std::uint8_t>(i * 19u + 251u),
                             static_cast<std::uint8_t>(i * 113u + 17u),
                             static_cast<std::uint8_t>(pattern == 0u ? 0u :
                                 pattern == 1u ? 255u : pattern == 2u ? (i & 1u) * 255u : i * 67u + 3u)};
            }
            if (!check(source, width, height)) return false;
        }
    }
    return true;
}

bool test_cmpr_tile_decode() {
    using galaxy::gx::RGBA8;
    constexpr std::array<std::uint32_t, 12> dimensions{
        1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 13u, 16u, 31u, 32u};
    constexpr std::array<std::array<std::uint16_t, 2>, 6> endpoints{{
        {0xF800u, 0x07E0u}, {0x001Fu, 0xFFFFu}, {0x0000u, 0xFFFFu},
        {0xFFFFu, 0x0000u}, {0x1234u, 0x1234u}, {0x9A56u, 0x1234u}}};
    constexpr std::array<std::uint8_t, 4> selectors{0x1Bu, 0xE4u, 0x55u, 0xAAu};
    constexpr RGBA8 guard{0x92u, 0x3Au, 0xCFu, 0x57u};
    const auto same = [](RGBA8 a, RGBA8 b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    const auto expand = [](std::uint16_t word) -> RGBA8 {
        const auto r = (word >> 11u) & 31u;
        const auto g = (word >> 5u) & 63u;
        const auto b = word & 31u;
        return {static_cast<std::uint8_t>(r * 8u + r / 4u),
                static_cast<std::uint8_t>(g * 4u + g / 16u),
                static_cast<std::uint8_t>(b * 8u + b / 4u), 255u};
    };
    for (const auto width : dimensions) {
        for (const auto height : dimensions) {
            const auto macro_columns = (width + 7u) / 8u;
            const auto subblocks = macro_columns * ((height + 7u) / 8u) * 4u;
            for (std::size_t alignment = 0u; alignment < 4u; ++alignment) {
                std::vector<std::uint8_t> source(alignment + subblocks * 8u);
                auto* tiled = source.data() + alignment;
                for (std::uint32_t sub = 0u; sub < subblocks; ++sub) {
                    const auto pair = endpoints[sub % endpoints.size()];
                    auto* block = tiled + sub * 8u;
                    block[0] = static_cast<std::uint8_t>(pair[0] >> 8u);
                    block[1] = static_cast<std::uint8_t>(pair[0]);
                    block[2] = static_cast<std::uint8_t>(pair[1] >> 8u);
                    block[3] = static_cast<std::uint8_t>(pair[1]);
                    for (unsigned row = 0u; row < 4u; ++row) {
                        block[4u + row] = selectors[(sub + row) & 3u];
                    }
                }
                std::vector<RGBA8> output(width * height + 2u, guard);
                galaxy::gx::detail::decode_cmpr_tiles(tiled, width, height, output.data() + 1u);
                if (!expect(same(output.front(), guard) && same(output.back(), guard),
                            "CMPR clipped tiles stay inside output")) return false;
                // Pixel-directed oracle: derive a pixel's source subblock and
                // selector without sharing the decoder's block/row traversal.
                for (std::uint32_t y = 0u; y < height; ++y) {
                    for (std::uint32_t x = 0u; x < width; ++x) {
                        const auto sub = (y / 8u * macro_columns + x / 8u) * 4u +
                            (y % 8u / 4u) * 2u + (x % 8u / 4u);
                        const auto pair = endpoints[sub % endpoints.size()];
                        const auto a = expand(pair[0]);
                        const auto b = expand(pair[1]);
                        const auto selector = (tiled[sub * 8u + 4u + (y & 3u)] >>
                            (6u - (x & 3u) * 2u)) & 3u;
                        RGBA8 expected = selector == 0u ? a : b;
                        if (selector >= 2u) {
                            const auto channel = [&](std::uint8_t first, std::uint8_t second) {
                                if (pair[0] <= pair[1]) {
                                    return static_cast<std::uint8_t>((first + second) / 2u);
                                }
                                return static_cast<std::uint8_t>(selector == 2u
                                    ? (5u * first + 3u * second) / 8u
                                    : (3u * first + 5u * second) / 8u);
                            };
                            expected = {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b),
                                static_cast<std::uint8_t>(pair[0] <= pair[1] && selector == 3u ? 0u : 255u)};
                        }
                        if (!expect(same(output[1u + y * width + x], expected),
                                    "CMPR tile order, selectors, GX interpolation and transparent RGB match oracle")) {
                            return false;
                        }
                    }
                }
            }
        }
    }
    galaxy::gx::detail::decode_cmpr_tiles(nullptr, 0u, 0u, nullptr);
    galaxy::gx::detail::decode_cmpr_tiles(nullptr, 0u, 8u, nullptr);
    galaxy::gx::detail::decode_cmpr_tiles(nullptr, 8u, 0u, nullptr);
    return true;
}

bool test_direct_color_tile_decode() {
    using galaxy::gx::RGBA8;
    using galaxy::gx::TexFormat;
    constexpr std::array<std::uint32_t, 12> dimensions{
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 15u, 16u, 33u};
    constexpr RGBA8 guard{0x91u, 0x38u, 0xE2u, 0x57u};
    const auto same = [](RGBA8 a, RGBA8 b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    // Independent arithmetic expansion of GX channel fields, including both
    // RGB5A3 encodings. No production lookup/conversion helper is used here.
    const auto color = [](std::uint16_t word, TexFormat format) -> RGBA8 {
        const auto e5 = [](unsigned n) { return static_cast<std::uint8_t>(8u * n + n / 4u); };
        if (format == TexFormat::RGB565) {
            const unsigned green = (word / 32u) % 64u;
            return {e5(word / 2048u), static_cast<std::uint8_t>(4u * green + green / 16u),
                e5(word % 32u), 255u};
        }
        if (word >= 0x8000u) {
            return {e5((word / 1024u) % 32u), e5((word / 32u) % 32u), e5(word % 32u), 255u};
        }
        const unsigned alpha = word / 4096u;
        return {static_cast<std::uint8_t>(17u * ((word / 256u) % 16u)),
            static_cast<std::uint8_t>(17u * ((word / 16u) % 16u)),
            static_cast<std::uint8_t>(17u * (word % 16u)),
            static_cast<std::uint8_t>(32u * alpha + 4u * alpha + alpha / 2u)};
    };
    for (const auto format : {TexFormat::RGB565, TexFormat::RGB5A3}) {
        const auto check = [&](std::uint32_t width, std::uint32_t height,
                               unsigned offset, bool all_words) {
            const unsigned tiles_x = (width + 3u) / 4u;
            const unsigned tiles_y = (height + 3u) / 4u;
            const auto word_at = [all_words](unsigned index) {
                return static_cast<std::uint16_t>(all_words ? index : index * 421u + 0x3d37u);
            };
            std::vector<std::uint8_t> source(offset + tiles_x * tiles_y * 32u);
            for (unsigned index = 0; index < tiles_x * tiles_y * 16u; ++index) {
                const auto word = word_at(index);
                source[offset + index * 2u] = static_cast<std::uint8_t>(word / 256u);
                source[offset + index * 2u + 1u] = static_cast<std::uint8_t>(word % 256u);
            }
            std::vector<RGBA8> output(width * height + 2u, guard);
            galaxy::gx::detail::decode_color_tiles(format, source.data() + offset,
                width, height, output.data() + 1u);
            if (!expect(same(output.front(), guard) && same(output.back(), guard),
                        "RGB565/RGB5A3 tiled decode stays within output")) return false;
            for (unsigned y = 0; y < height; ++y) {
                for (unsigned x = 0; x < width; ++x) {
                    const unsigned index = ((y / 4u) * tiles_x + x / 4u) * 16u +
                        (y % 4u) * 4u + x % 4u;
                    if (!expect(same(output[1u + y * width + x], color(word_at(index), format)),
                                "RGB565/RGB5A3 tile coordinates and channel expansion match oracle")) return false;
                }
            }
            return true;
        };
        for (auto width : dimensions) {
            for (auto height : dimensions) {
                for (auto offset : {0u, 1u, 3u}) {
                    if (!check(width, height, offset, false)) return false;
                }
            }
        }
        // Exactly 65536 pixels in full tiles: every possible color word once.
        if (!check(256u, 256u, 1u, true)) return false;
        galaxy::gx::detail::decode_color_tiles(format, nullptr, 0u, 9u, nullptr);
        galaxy::gx::detail::decode_color_tiles(format, nullptr, 9u, 0u, nullptr);
    }
    return true;
}

bool test_small_index_tile_decode() {
    using galaxy::gx::RGBA8;
    using galaxy::gx::TexFormat;
    using galaxy::gx::TlutFormat;
    // Every tile-tail width/height plus full tiles, multiple tiles and both
    // sides of the raw/predecoded palette threshold. Keep the pixel oracle
    // independent: it computes each tiled source address from destination x/y.
    constexpr auto sizes = [] {
        constexpr std::array<std::uint32_t, 18> dimensions{
            1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
            15u, 16u, 31u, 32u, 33u, 64u, 65u, 256u, 257u};
        std::array<std::array<std::uint32_t, 2>, dimensions.size() * dimensions.size()> result{};
        std::size_t next = 0u;
        for (auto width : dimensions) {
            for (auto height : dimensions) result[next++] = {width, height};
        }
        return result;
    }();
    constexpr std::array<std::uint16_t, 8> edge_words{
        0x0000u, 0xFFFFu, 0x8000u, 0x7FFFu,
        0xF800u, 0x07E0u, 0x001Fu, 0x1234u};
    constexpr RGBA8 guard{0x91u, 0x38u, 0xE2u, 0x57u};
    const auto same = [](RGBA8 a, RGBA8 b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    };
    // Arithmetic channel expansion is independent of the decoder's shifts/ORs.
    const auto color = [](std::uint16_t word, TlutFormat format) -> RGBA8 {
        const auto e5 = [](unsigned n) { return static_cast<std::uint8_t>(8u * n + n / 4u); };
        const auto e6 = [](unsigned n) { return static_cast<std::uint8_t>(4u * n + n / 16u); };
        const auto e4 = [](unsigned n) { return static_cast<std::uint8_t>(17u * n); };
        if (format == TlutFormat::IA8) {
            const auto intensity = static_cast<std::uint8_t>(word % 256u);
            return {intensity, intensity, intensity, static_cast<std::uint8_t>(word / 256u)};
        }
        if (format == TlutFormat::RGB565) {
            return {e5(word / 2048u), e6((word / 32u) % 64u), e5(word % 32u), 255u};
        }
        if (word >= 0x8000u) {
            return {e5((word / 1024u) % 32u), e5((word / 32u) % 32u), e5(word % 32u), 255u};
        }
        const unsigned alpha = word / 4096u;
        return {e4((word / 256u) % 16u), e4((word / 16u) % 16u), e4(word % 16u),
                static_cast<std::uint8_t>(32u * alpha + 4u * alpha + alpha / 2u)};
    };
    const auto decode = [](TexFormat format, const std::uint8_t* src,
                           std::uint32_t width, std::uint32_t height,
                           const std::uint8_t* palette, TlutFormat tlut, RGBA8* out) {
        if (format == TexFormat::C14X2) {
            galaxy::gx::detail::decode_c14x2_tiles(src, width, height, palette, tlut, out);
        } else {
            galaxy::gx::detail::decode_small_index_tiles(format, src, width, height, palette, tlut, out);
        }
    };
    for (const auto format : {TexFormat::C4, TexFormat::C8, TexFormat::C14X2}) {
        const bool four_bits = format == TexFormat::C4;
        const bool wide_index = format == TexFormat::C14X2;
        const unsigned entries = wide_index ? 16384u : four_bits ? 16u : 256u;
        const unsigned tile_rows = four_bits ? 8u : 4u;
        const unsigned tile_columns = wide_index ? 4u : 8u;
        for (const auto tlut : {TlutFormat::IA8, TlutFormat::RGB565, TlutFormat::RGB5A3}) {
            for (const auto size : sizes) {
                const auto width = size[0], height = size[1];
                const auto tiles_x = (width + tile_columns - 1u) / tile_columns;
                const auto tiles_y = (height + tile_rows - 1u) / tile_rows;
                for (const auto prefix : {0u, 513u}) {
                    std::vector<std::uint8_t> source(3u + tiles_x * tiles_y * 32u);
                    for (std::size_t i = 3u; i < source.size(); ++i) {
                        source[i] = static_cast<std::uint8_t>((i - 3u) * 37u + 13u);
                    }
                    if (wide_index) {
                        // The exact 256x256 case visits every raw 16-bit index,
                        // including all four values of the two unused top bits.
                        for (std::size_t i = 3u; i < source.size(); i += 2u) {
                            const auto word = static_cast<std::uint16_t>((i - 3u) / 2u);
                            source[i] = static_cast<std::uint8_t>(word / 256u);
                            source[i + 1u] = static_cast<std::uint8_t>(word % 256u);
                        }
                    }
                    std::vector<std::uint8_t> bank(prefix + entries * 2u, 0xE7u);
                    std::vector<RGBA8> output(width * height + 2u, guard);
                    // Reuse the same input/output pointers with changed colors;
                    // no table may survive into the next call or palette format.
                    for (unsigned round = 0u; round < 2u; ++round) {
                        std::vector<std::uint16_t> words(entries);
                        for (unsigned i = 0u; i < entries; ++i) {
                            words[i] = static_cast<std::uint16_t>(i * 421u + round * 0x3D37u);
                            if (round == 0u && i < edge_words.size()) words[i] = edge_words[i];
                            bank[prefix + i * 2u] = static_cast<std::uint8_t>(words[i] >> 8u);
                            bank[prefix + i * 2u + 1u] = static_cast<std::uint8_t>(words[i]);
                        }
                        std::fill(output.begin(), output.end(), guard);
                        decode(format, source.data() + 3u,
                            width, height, bank.data() + prefix, tlut, output.data() + 1u);
                        if (!expect(same(output.front(), guard) && same(output.back(), guard),
                                    "indexed edge tiles stay inside output")) return false;
                        for (std::uint32_t y = 0; y < height; ++y) {
                            for (std::uint32_t x = 0; x < width; ++x) {
                                const auto tile = (y / tile_rows) * tiles_x + x / tile_columns;
                                const auto local_pixel = (y % tile_rows) * tile_columns + x % tile_columns;
                                const auto byte_offset = 3u + tile * 32u + (wide_index
                                    ? local_pixel * 2u : four_bits ? local_pixel / 2u : local_pixel);
                                const auto byte = source[byte_offset];
                                const unsigned index = four_bits
                                    ? (local_pixel % 2u == 0u ? byte / 16u : byte % 16u)
                                    : wide_index
                                        ? (static_cast<unsigned>(byte) * 256u + source[byte_offset + 1u]) % 16384u
                                        : byte;
                                if (!expect(same(output[1u + y * width + x], color(words[index], tlut)),
                                            "indexed tile address, TLUT channels and changed palette match oracle")) {
                                    return false;
                                }
                            }
                        }
                    }
                }
            }
        }
        decode(
            format, nullptr, 0u, 9u, nullptr, TlutFormat::IA8, nullptr);
        decode(
            format, nullptr, 9u, 0u, nullptr, TlutFormat::RGB565, nullptr);
    }
    return true;
}

galaxy::gx::EfbCopyParams make_filter_params(
    const std::array<std::uint32_t, 7>& taps) {
    galaxy::gx::EfbCopyParams params{};
    params.src_y = 1u;
    params.src_width = 1u;
    params.src_height = 1u;
    params.filter0_raw =
        (taps[0] & 63u) |
        ((taps[1] & 63u) << 6u) |
        ((taps[2] & 63u) << 12u) |
        ((taps[3] & 63u) << 18u);
    params.filter1_raw =
        (taps[4] & 63u) |
        ((taps[5] & 63u) << 6u) |
        ((taps[6] & 63u) << 12u);
    return params;
}

// Independent integer oracle derived from the GX copy pipeline's documented
// byte-domain behavior. Source rows are quantized before the 1/64 filter;
// sums shift instead of rounding, >=128 coefficient sums wrap to nine bits,
// and alpha is the unfiltered center sample.
Pixel oracle_convert(
    Pixel previous,
    Pixel current,
    Pixel next,
    const galaxy::gx::EfbCopyParams& params,
    bool intensity,
    std::uint8_t copy_format) {
    std::array<std::uint32_t, 3> coefficients{
        params.filter_upper(),
        params.filter_middle(),
        params.filter_lower(),
    };
    if (coefficients[0] == 0u && coefficients[1] == 0u &&
        coefficients[2] == 0u) {
        // Product startup policy while the filter registers are unprogrammed.
        coefficients[1] = 64u;
    }
    const std::uint32_t coefficient_sum =
        coefficients[0] + coefficients[1] + coefficients[2];
    const auto filtered = [&](std::uint8_t p, std::uint8_t c, std::uint8_t n) {
        std::uint32_t value =
            (static_cast<std::uint32_t>(p) * coefficients[0] +
             static_cast<std::uint32_t>(c) * coefficients[1] +
             static_cast<std::uint32_t>(n) * coefficients[2]) >> 6u;
        if (coefficient_sum >= 128u) {
            value &= 0x1ffu;
        }
        return static_cast<std::uint8_t>(std::min(value, 255u));
    };

    Pixel raw{
        filtered(previous.r, current.r, next.r),
        filtered(previous.g, current.g, next.g),
        filtered(previous.b, current.b, next.b),
        current.a,
    };

    static constexpr float kGamma[4] = {1.0f, 1.7f, 2.2f, 2.2f};
    const float gamma = kGamma[params.gamma & 3u];
    if (gamma != 1.0f) {
        const auto apply_gamma = [gamma](std::uint8_t value) {
            return static_cast<std::uint8_t>(std::lround(
                std::pow(static_cast<float>(value) / 255.0f, 1.0f / gamma) *
                255.0f));
        };
        raw.r = apply_gamma(raw.r);
        raw.g = apply_gamma(raw.g);
        raw.b = apply_gamma(raw.b);
    }

    if (intensity) {
        const int r = raw.r;
        const int g = raw.g;
        const int b = raw.b;
        const std::array<std::uint32_t, 3> numerators{
            static_cast<std::uint32_t>(66 * r + 129 * g + 25 * b + 4096),
            static_cast<std::uint32_t>(-38 * r - 74 * g + 112 * b + 32768),
            static_cast<std::uint32_t>(112 * r - 94 * g - 18 * b + 32768),
        };
        const auto round_div_256 = [](std::uint32_t value) {
            return static_cast<std::uint8_t>(
                (value >> 8u) + ((value >> 7u) & 1u));
        };
        raw.r = round_div_256(numerators[0]);
        raw.g = round_div_256(numerators[1]);
        raw.b = round_div_256(numerators[2]);
    }

    switch (copy_format) {
    case 0u: {
        const std::uint8_t i = static_cast<std::uint8_t>(
            (raw.r & 0xf0u) | (raw.r >> 4u));
        return Pixel{i, i, i, i};
    }
    case 1u:
    case 8u:
        return Pixel{raw.r, raw.r, raw.r, raw.r};
    case 2u: {
        const std::uint8_t i = static_cast<std::uint8_t>(
            (raw.r & 0xf0u) | (raw.r >> 4u));
        const std::uint8_t a = static_cast<std::uint8_t>(
            (raw.a & 0xf0u) | (raw.a >> 4u));
        return Pixel{i, i, i, a};
    }
    case 3u:
        return Pixel{raw.r, raw.r, raw.r, raw.a};
    case 4u:
        raw.r = static_cast<std::uint8_t>((raw.r & 0xf8u) | (raw.r >> 5u));
        raw.g = static_cast<std::uint8_t>((raw.g & 0xfcu) | (raw.g >> 6u));
        raw.b = static_cast<std::uint8_t>((raw.b & 0xf8u) | (raw.b >> 5u));
        raw.a = 255u;
        return raw;
    case 5u:
        if (raw.a > 224u) {
            raw.r = static_cast<std::uint8_t>((raw.r & 0xf8u) | (raw.r >> 5u));
            raw.g = static_cast<std::uint8_t>((raw.g & 0xf8u) | (raw.g >> 5u));
            raw.b = static_cast<std::uint8_t>((raw.b & 0xf8u) | (raw.b >> 5u));
            raw.a = 255u;
        } else {
            raw.r = static_cast<std::uint8_t>((raw.r & 0xf0u) | (raw.r >> 4u));
            raw.g = static_cast<std::uint8_t>((raw.g & 0xf0u) | (raw.g >> 4u));
            raw.b = static_cast<std::uint8_t>((raw.b & 0xf0u) | (raw.b >> 4u));
            raw.a = static_cast<std::uint8_t>(
                (raw.a & 0xe0u) | (raw.a >> 3u) | (raw.a >> 6u));
        }
        return raw;
    case 6u:
    case 255u:
        return raw;
    case 7u:
        return Pixel{raw.a, raw.a, raw.a, raw.a};
    case 9u:
        return Pixel{raw.g, raw.g, raw.g, raw.g};
    case 10u:
        return Pixel{raw.b, raw.b, raw.b, raw.b};
    case 11u:
        return Pixel{raw.r, raw.r, raw.r, raw.g};
    case 12u:
        return Pixel{raw.g, raw.g, raw.g, raw.b};
    case 15u:
        raw.a = 255u;
        return raw;
    default:
        throw std::runtime_error("oracle received an invalid EFB copy format");
    }
}

Pixel oracle_from_three_rows(
    const std::array<Pixel, 3>& rows,
    const galaxy::gx::EfbCopyParams& params,
    bool intensity,
    std::uint8_t copy_format) {
    const int minimum = params.clamp_top ? 1 : 0;
    const int maximum = params.clamp_bottom ? 1 : 2;
    return oracle_convert(
        rows[static_cast<std::size_t>(std::clamp(0, minimum, maximum))],
        rows[1],
        rows[static_cast<std::size_t>(std::clamp(2, minimum, maximum))],
        params,
        intensity,
        copy_format);
}

Pixel packed_depth(std::uint32_t z24) {
    z24 &= 0x00ffffffu;
    return Pixel{
        static_cast<std::uint8_t>((z24 >> 16u) & 0xffu),
        static_cast<std::uint8_t>((z24 >> 8u) & 0xffu),
        static_cast<std::uint8_t>(z24 & 0xffu),
        255u,
    };
}

void check_hr(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        char message[160]{};
        std::snprintf(
            message,
            sizeof(message),
            "%s failed with HRESULT 0x%08X",
            operation,
            static_cast<unsigned>(result));
        throw std::runtime_error(message);
    }
}

ComPtr<ID3DBlob> compile_shader(
    std::string_view source,
    const char* target) {
    ComPtr<ID3DBlob> code;
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source.data(),
        source.size(),
        "efb_conversion_tests",
        nullptr,
        nullptr,
        "main",
        target,
        D3DCOMPILE_ENABLE_STRICTNESS |
            D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
        0,
        &code,
        &errors);
    if (FAILED(result)) {
        const char* diagnostics = errors
            ? static_cast<const char*>(errors->GetBufferPointer())
            : "no compiler diagnostics";
        throw std::runtime_error(
            std::string("D3DCompile failed: ") + diagnostics);
    }
    return code;
}

D3D12_HEAP_PROPERTIES heap_properties(D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES properties{};
    properties.Type = type;
    properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    properties.CreationNodeMask = 1u;
    properties.VisibleNodeMask = 1u;
    return properties;
}

D3D12_RESOURCE_DESC buffer_desc(UINT64 size) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = size;
    desc.Height = 1u;
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = 1u;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    return desc;
}

D3D12_RESOURCE_DESC texture_desc(
    DXGI_FORMAT format,
    UINT width,
    UINT height,
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1u;
    desc.MipLevels = 1u;
    desc.Format = format;
    desc.SampleDesc.Count = 1u;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;
    return desc;
}

class WarpConversionHarness {
public:
    WarpConversionHarness() {
        ComPtr<IDXGIFactory4> factory;
        check_hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        ComPtr<IDXGIAdapter> adapter;
        check_hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)), "EnumWarpAdapter");
        check_hr(
            D3D12CreateDevice(
                adapter.Get(),
                D3D_FEATURE_LEVEL_11_0,
                IID_PPV_ARGS(&device_)),
            "D3D12CreateDevice(WARP)");

        D3D12_COMMAND_QUEUE_DESC queue_desc{};
        queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check_hr(
            device_->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue_)),
            "CreateCommandQueue");

        D3D12_DESCRIPTOR_RANGE range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1u;
        range.BaseShaderRegister = 0u;
        range.OffsetInDescriptorsFromTableStart = 0u;
        D3D12_ROOT_PARAMETER root_parameters[2]{};
        root_parameters[0].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        root_parameters[0].Constants.ShaderRegister = 0u;
        root_parameters[0].Constants.Num32BitValues = 16u;
        root_parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        root_parameters[1].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        root_parameters[1].DescriptorTable.NumDescriptorRanges = 1u;
        root_parameters[1].DescriptorTable.pDescriptorRanges = &range;
        root_parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC samplers[2]{};
        samplers[0].Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[0].ShaderRegister = 0u;
        samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samplers[1] = samplers[0];
        samplers[1].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[1].ShaderRegister = 1u;

        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = 2u;
        root_desc.pParameters = root_parameters;
        root_desc.NumStaticSamplers = 2u;
        root_desc.pStaticSamplers = samplers;
        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> root_errors;
        check_hr(
            D3D12SerializeRootSignature(
                &root_desc,
                D3D_ROOT_SIGNATURE_VERSION_1,
                &serialized,
                &root_errors),
            "D3D12SerializeRootSignature");
        check_hr(
            device_->CreateRootSignature(
                0u,
                serialized->GetBufferPointer(),
                serialized->GetBufferSize(),
                IID_PPV_ARGS(&root_signature_)),
            "CreateRootSignature");

        static constexpr char kVertexShader[] = R"HLSL(
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
V main(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    V output;
    output.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    output.uv = uv;
    return output;
}
)HLSL";
        const ComPtr<ID3DBlob> vs = compile_shader(kVertexShader, "vs_5_0");
        const ComPtr<ID3DBlob> ps = compile_shader(
            galaxy::gx::efb_conversion_shader_source(), "ps_5_0");

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline_desc{};
        pipeline_desc.pRootSignature = root_signature_.Get();
        pipeline_desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
        pipeline_desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
        pipeline_desc.BlendState.RenderTarget[0].RenderTargetWriteMask =
            D3D12_COLOR_WRITE_ENABLE_ALL;
        pipeline_desc.SampleMask = UINT_MAX;
        pipeline_desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pipeline_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pipeline_desc.RasterizerState.DepthClipEnable = TRUE;
        pipeline_desc.DepthStencilState.DepthEnable = FALSE;
        pipeline_desc.DepthStencilState.DepthWriteMask =
            D3D12_DEPTH_WRITE_MASK_ZERO;
        pipeline_desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        pipeline_desc.DepthStencilState.StencilEnable = FALSE;
        pipeline_desc.PrimitiveTopologyType =
            D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pipeline_desc.NumRenderTargets = 1u;
        pipeline_desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pipeline_desc.SampleDesc.Count = 1u;
        check_hr(
            device_->CreateGraphicsPipelineState(
                &pipeline_desc, IID_PPV_ARGS(&pipeline_)),
            "CreateGraphicsPipelineState");
        check_hr(device_->CreateFence(0u, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)),
                 "CreateFence");
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fence_event_ == nullptr) {
            throw std::runtime_error("CreateEventW failed");
        }
    }

    ~WarpConversionHarness() {
        if (fence_event_ != nullptr) {
            CloseHandle(fence_event_);
        }
    }

    GxPixelResult run_gx(const GxPixelCase& input) {
        const std::vector<std::uint8_t> bytes{
            input.texture.r, input.texture.g, input.texture.b, input.texture.a};
        GxPixelResult result;
        result.color = run_bytes(DXGI_FORMAT_R8G8B8A8_UNORM, bytes,
            1u, 1u, 1u, {}, &input, &result.depth).front();
        return result;
    }

    std::vector<Pixel> run_color(
        const std::array<Pixel, 3>& rows,
        unsigned scale,
        const galaxy::gx::EfbConversionShaderConstants& constants) {
        const UINT width = scale;
        const UINT height = 3u * scale;
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(width) * height * 4u);
        for (UINT y = 0u; y < height; ++y) {
            const Pixel pixel = rows[y / scale];
            for (UINT x = 0u; x < width; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * 4u;
                bytes[offset + 0u] = pixel.r;
                bytes[offset + 1u] = pixel.g;
                bytes[offset + 2u] = pixel.b;
                bytes[offset + 3u] = pixel.a;
            }
        }
        return run_bytes(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            bytes,
            width,
            height,
            scale,
            constants);
    }

    std::vector<Pixel> run_depth(
        const std::array<std::uint32_t, 3>& z24_rows,
        unsigned scale,
        const galaxy::gx::EfbConversionShaderConstants& constants) {
        const UINT width = scale;
        const UINT height = 3u * scale;
        std::vector<std::uint8_t> bytes(
            static_cast<std::size_t>(width) * height * sizeof(float));
        for (UINT y = 0u; y < height; ++y) {
            const std::uint32_t z24 = z24_rows[y / scale] & 0x00ffffffu;
            const float depth =
                1.0f - static_cast<float>(z24) / 16777216.0f;
            for (UINT x = 0u; x < width; ++x) {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * sizeof(float);
                std::memcpy(bytes.data() + offset, &depth, sizeof(depth));
            }
        }
        return run_bytes(
            DXGI_FORMAT_R32_FLOAT,
            bytes,
            width,
            height,
            scale,
            constants);
    }

    Pixel run_color_peek(
        const std::vector<Pixel>& pixels,
        unsigned width,
        unsigned height,
        const galaxy::gx::EfbPeekSamplingGeometry& sampling) {
        if (pixels.size() !=
            static_cast<std::size_t>(width) * height) {
            throw std::invalid_argument(
                "color-peek source size does not match dimensions");
        }
        std::vector<std::uint8_t> bytes(pixels.size() * 4u);
        for (std::size_t index = 0u; index < pixels.size(); ++index) {
            bytes[index * 4u + 0u] = pixels[index].r;
            bytes[index * 4u + 1u] = pixels[index].g;
            bytes[index * 4u + 2u] = pixels[index].b;
            bytes[index * 4u + 3u] = pixels[index].a;
        }
        const std::vector<Pixel> result = run_bytes(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            bytes,
            width,
            height,
            1u,
            galaxy::gx::make_efb_peek_conversion_constants(
                sampling, galaxy::gx::EfbPeekKind::Color));
        if (result.size() != 1u) {
            throw std::runtime_error("color peek returned a non-1x1 result");
        }
        return result.front();
    }

    Pixel run_depth_peek(
        const std::vector<std::uint32_t>& z24_pixels,
        unsigned width,
        unsigned height,
        const galaxy::gx::EfbPeekSamplingGeometry& sampling) {
        if (z24_pixels.size() !=
            static_cast<std::size_t>(width) * height) {
            throw std::invalid_argument(
                "depth-peek source size does not match dimensions");
        }
        std::vector<std::uint8_t> bytes(
            z24_pixels.size() * sizeof(float));
        for (std::size_t index = 0u; index < z24_pixels.size(); ++index) {
            const std::uint32_t z24 =
                z24_pixels[index] & 0x00ffffffu;
            const float depth =
                1.0f - static_cast<float>(z24) / 16777216.0f;
            std::memcpy(
                bytes.data() + index * sizeof(float),
                &depth,
                sizeof(depth));
        }
        const std::vector<Pixel> result = run_bytes(
            DXGI_FORMAT_R32_FLOAT,
            bytes,
            width,
            height,
            1u,
            galaxy::gx::make_efb_peek_conversion_constants(
                sampling, galaxy::gx::EfbPeekKind::Depth));
        if (result.size() != 1u) {
            throw std::runtime_error("depth peek returned a non-1x1 result");
        }
        return result.front();
    }

    std::vector<Pixel> run_depth_field(
        const std::vector<std::uint32_t>& z24_pixels, unsigned width,
        unsigned height, unsigned scale, unsigned logical_size) {
        std::vector<std::uint8_t> bytes(z24_pixels.size() * sizeof(float));
        for (std::size_t i=0; i<z24_pixels.size(); ++i) {
            const float depth=1.0f-static_cast<float>(z24_pixels[i])/16777216.0f;
            std::memcpy(bytes.data()+i*sizeof(float), &depth, sizeof(depth));
        }
        auto constants=galaxy::gx::make_efb_peek_conversion_constants(
            galaxy::gx::compute_efb_peek_sampling_geometry(0,0,scale),
            galaxy::gx::EfbPeekKind::Depth);
        constants.values[15]=-1.0f;
        return run_bytes(DXGI_FORMAT_R32_FLOAT,bytes,width,height,logical_size,constants);
    }

    bool test_xfb_retirement_rollback() {
        using namespace galaxy::gx;
        EfbCopyManager manager;
        if (!manager.initialize(device_.Get(), 1u)) {
            throw std::runtime_error("initialize XFB rollback manager");
        }
        EfbCopyParams params{};
        params.src_width = params.src_height = 8u;
        params.dest_addr = 0x01000000u;
        params.copy_to_xfb = true;
        auto* original = manager.acquire_xfb(params, 1u);
        manager.mark_xfb_copied(*original, 1u);
        auto* original_resource = original->texture.Get();
        const auto original_serial = original->copy_serial;
        // Initial allocation needs no retirement list. Resizing before the
        // first begin_frame deliberately exercises its ownership failure path.
        params.src_width = 16u;
        bool rejected = false;
        try { (void)manager.acquire_xfb(params, 2u); }
        catch (const std::runtime_error&) { rejected = true; }
        const auto* preserved = manager.find_xfb(params.dest_addr);
        bool passed = expect(rejected && preserved == original &&
            preserved != nullptr && preserved->texture.Get() == original_resource &&
            preserved->width == 8u && preserved->height == 8u &&
            preserved->copy_serial == original_serial && preserved->frame_stamp == 1u &&
            manager.latest() == preserved,
            "failed resize retirement preserves the live XFB resource and presentation identity");
        manager.begin_frame(0u, kFramesInFlight);
        const auto* resized = manager.acquire_xfb(params, 2u);
        passed &= expect(resized->texture != nullptr && resized->width == 16u &&
            resized->height == 8u && !resized->presentable && resized->copy_serial == 0u,
            "resize succeeds after retirement storage is available without inventing a copied frame");
        // No GPU commands reference these resources in this lifecycle fixture.
        manager.shutdown();
        return passed;
    }

    bool test_xfb_idle_pool_admission() {
        using namespace galaxy::gx;
        EfbCopyManager manager;
        if (!manager.initialize(device_.Get(), 1u)) {
            throw std::runtime_error("initialize XFB pool manager");
        }
        manager.begin_frame(0u, kFramesInFlight);
        EfbCopyParams params{};
        params.src_height = 8u;
        params.dest_addr = 0x01000000u;
        params.copy_to_xfb = true;
        // Eight one-off sizes fill the idle pool; the ninth is the current XFB.
        for (unsigned i = 0; i < 9u; ++i) {
            params.src_width = static_cast<std::uint16_t>(32u + i);
            (void)manager.acquire_xfb(params, i + 1u);
        }
        ComPtr<ID3D12Resource> wanted = manager.acquire_xfb(params, 10u)->texture;
        manager.begin_frame(0u, kFramesInFlight);
        params.src_width = 41u;
        ComPtr<ID3D12Resource> other = manager.acquire_xfb(params, 11u)->texture;
        manager.begin_frame(1u, kFramesInFlight);
        params.src_width = 40u;
        const auto* before_fence = manager.acquire_xfb(params, 12u);
        // Witness ComPtrs keep identities distinct even in the broken version:
        // releasing a resource cannot let a new allocation reuse its address.
        bool passed = expect(before_fence->texture.Get() != wanted.Get(),
            "another frame slot cannot recycle the recently retired XFB");
        manager.begin_frame(0u, kFramesInFlight);
        params.src_width = 41u;
        (void)manager.acquire_xfb(params, 13u);
        params.src_width = 40u;
        const auto* reused = manager.acquire_xfb(params, 14u);
        passed &= expect(reused->texture.Get() == wanted.Get() &&
            reused->width == 40u && reused->height == 8u,
            "full idle pool admits a current size and reuses it after its owning fence");
        manager.begin_frame(1u, kFramesInFlight);
        params.src_width = 41u;
        passed &= expect(manager.acquire_xfb(params, 15u)->texture.Get() == other.Get(),
            "second size is reusable only after its own retirement slot is reclaimed");
        // No command lists are submitted; production begin_frame calls are
        // preceded by the corresponding renderer fence waits.
        manager.shutdown();
        return passed;
    }

    bool test_content_texture_identity() {
        using namespace galaxy::gx;
        bool passed = true;
        auto cache_owner = std::make_unique<TextureCache>();
        TextureCache& cache = *cache_owner;
        if (!cache.initialize(device_.Get())) {
            throw std::runtime_error("initialize content identity cache");
        }
        cache.begin_frame(0u, kFramesInFlight);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check_hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)), "Create content identity allocator");
        check_hr(device_->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create content identity command list");
        cache.set_upload_list(list.Get());
        constexpr std::uint32_t address = 0x10000000u;
        std::array<std::byte, 1024> bytes{};
        galaxy::GuestMemoryRegionV1 region{
            address, static_cast<std::uint32_t>(bytes.size()), bytes.data()};
        galaxy::GuestMemoryV1 memory{};
        memory.region_count = 1u;
        memory.regions = &region;
        TexImage image{};
        image.guest_addr = address;
        image.width = image.height = 4u;
        image.format = TexFormat::RGB565;
        TexMode mode{};
        mode.min_filter = TexMinFilter::Near;
        const auto original = cache.get(image, mode, TlutRef{}, &memory);
        image.guest_addr = address + 32u;
        const auto twin = cache.get(image, mode, TlutRef{}, &memory);
        passed &= expect(twin.resource == original.resource && twin.srv_index != original.srv_index,
            "identical captured content shares its resource across guest addresses");
        image.guest_addr = address + 64u;
        image.width = 2u; // same 32-byte tile, different resource extent
        const auto narrow = cache.get(image, mode, TlutRef{}, &memory);
        passed &= expect(narrow.resource != original.resource && narrow.resource->GetDesc().Width == 2u,
            "equal tiled source bytes do not alias distinct texture dimensions");
        image.guest_addr = address + 96u;
        image.width = 4u;
        image.format = TexFormat::RGB5A3;
        const auto other_format = cache.get(image, mode, TlutRef{}, &memory);
        passed &= expect(other_format.resource != original.resource,
            "equal captured bytes do not alias different decoding formats");
        image.guest_addr = address + 128u;
        image.format = TexFormat::RGB565;
        bytes[128u] = std::byte{0xf8};
        const auto changed_source = cache.get(image, mode, TlutRef{}, &memory);
        passed &= expect(changed_source.resource != original.resource,
            "a changed captured source byte produces a different content resource");
        image.guest_addr = address;
        cache.invalidate_all();
        const auto restored = cache.get(image, mode, TlutRef{}, &memory);
        passed &= expect(restored.resource == original.resource && restored.srv_index != original.srv_index,
            "immutable content keys remain usable after decoded-address invalidation");

        image.guest_addr = address + 256u;
        image.width = image.height = 8u;
        image.format = TexFormat::C4;
        const auto indexed = cache.get(image, mode, TlutRef{0u, TlutFormat::IA8}, &memory);
        const auto equal_palette = cache.get(image, mode, TlutRef{1u, TlutFormat::IA8}, &memory);
        const auto other_palette_format = cache.get(image, mode, TlutRef{0u, TlutFormat::RGB565}, &memory);
        passed &= expect(equal_palette.resource == indexed.resource &&
            other_palette_format.resource != indexed.resource,
            "equal palette bytes reuse content while palette decoding format remains distinct");
        bytes[768u] = bytes[769u] = std::byte{0xff};
        passed &= expect(cache.load_tlut((address + 768u) >> 5u, 1u << 10u, &memory),
            "changed palette word updates the content identity fixture");
        const auto changed_palette = cache.get(image, mode, TlutRef{0u, TlutFormat::IA8}, &memory);
        const auto unchanged_palette = cache.get(image, mode, TlutRef{1u, TlutFormat::IA8}, &memory);
        passed &= expect(changed_palette.resource != indexed.resource &&
            unchanged_palette.resource == indexed.resource,
            "changed captured palette bytes separate content without losing unrelated palette reuse");
        // Resource/identity fixture only; no command list is submitted.
        cache.set_upload_list(nullptr);
        cache.shutdown();
        return passed;
    }

    bool test_texture_descriptor_retirement() {
        using namespace galaxy::gx;
        bool passed = true;
        auto cache_owner = std::make_unique<TextureCache>();
        TextureCache& cache = *cache_owner;
        if (!cache.initialize(device_.Get())) {
            throw std::runtime_error("initialize WARP retirement cache");
        }
        cache.begin_frame(0u, kFramesInFlight);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check_hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)), "Create retirement allocator");
        check_hr(device_->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create retirement command list");
        cache.set_upload_list(list.Get());
        constexpr std::uint32_t address = 0x10000000u;
        std::array<std::byte, 512> bytes{};
        // Distinct content prevents the optional decoded-content cache from
        // turning the four lookups into shared-resource hits.
        for (unsigned block = 0; block < 4u; ++block) {
            bytes[block * 128u] = static_cast<std::byte>(block + 1u);
        }
        galaxy::GuestMemoryRegionV1 region{
            address, static_cast<std::uint32_t>(bytes.size()), bytes.data()};
        galaxy::GuestMemoryV1 memory{};
        memory.region_count = 1u;
        memory.regions = &region;
        TexImage image{};
        image.guest_addr = address;
        image.width = image.height = 8u;
        image.format = TexFormat::RGB565;
        TexMode mode{};
        mode.min_filter = TexMinFilter::Near;
        const auto initial_revision = cache.retirement_revision();
        const auto first = cache.get(image, mode, TlutRef{}, &memory);
        const auto hit = cache.get(image, mode, TlutRef{}, &memory);
        passed &= expect(hit.srv_index == first.srv_index &&
            !cache.srv_index_retired(first.srv_index) &&
            cache.retirement_revision() == initial_revision,
            "texture hits do not invalidate cross-frame handles");
        passed &= expect(cache.invalidate_guest_range(address, 128u) == 1u &&
            cache.srv_index_retired(first.srv_index) &&
            cache.retirement_revision() == initial_revision + 1u,
            "retirement publishes a cross-frame handle lifetime change");
        image.guest_addr = address + 128u;
        const auto same_frame = cache.get(image, mode, TlutRef{}, &memory);
        passed &= expect(same_frame.srv_index != first.srv_index &&
            !cache.srv_index_retired(same_frame.srv_index) &&
            first.resource->GetDesc().Width == 8u,
            "retired resource and CPU descriptor survive later lookups in the same frame");
        cache.begin_frame(1u, kFramesInFlight);
        image.guest_addr = address + 256u;
        const auto other_slot = cache.get(image, mode, TlutRef{}, &memory);
        passed &= expect(other_slot.srv_index != first.srv_index &&
            cache.srv_index_retired(first.srv_index) &&
            !cache.srv_index_retired(other_slot.srv_index),
            "another frame slot cannot reclaim the retired CPU descriptor");
        // No command list is submitted by this lifetime fixture. In production,
        // returning to slot 0 requires its renderer fence wait first.
        cache.begin_frame(0u, kFramesInFlight);
        passed &= expect(cache.srv_index_retired(first.srv_index),
            "reclaimed slot retains its retired identity until allocation");
        image.guest_addr = address + 384u;
        const auto reclaimed = cache.get(image, mode, TlutRef{}, &memory);
        passed &= expect(reclaimed.srv_index == first.srv_index &&
            !cache.srv_index_retired(reclaimed.srv_index) &&
            cache.retirement_revision() == initial_revision + 1u,
            "owning slot reclaims the descriptor without another retirement");
        cache.set_upload_list(nullptr);
        cache.shutdown();
        return passed;
    }

    bool test_texture_cache_ranges() {
        using namespace galaxy::gx;
        bool passed = true;
        // Full Wii TMEM is embedded in the cache; Windows' default test stack
        // cannot hold its 1MiB bank plus the caller/harness frames.
        auto cache_owner = std::make_unique<TextureCache>();
        TextureCache& cache = *cache_owner;
        if (!cache.initialize(device_.Get())) throw std::runtime_error("initialize WARP texture cache");
        cache.begin_frame(0u, kFramesInFlight);
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        check_hr(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&allocator)), "Create texture cache allocator");
        check_hr(device_->CreateCommandList(0u, D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(), nullptr, IID_PPV_ARGS(&list)), "Create texture cache command list");
        cache.set_upload_list(list.Get());
        constexpr std::uint32_t address = 0x10000000u;
        std::array<std::byte, 2048> bytes{};
        galaxy::GuestMemoryRegionV1 region{address, static_cast<std::uint32_t>(bytes.size()), bytes.data()};
        galaxy::GuestMemoryV1 memory{};
        memory.region_count = 1u;
        memory.regions = &region;
        TexImage image{};
        image.guest_addr = address;
        image.width = image.height = 8u;
        image.format = TexFormat::RGB565;
        TexMode mode{};
        mode.min_filter = TexMinFilter::NearMipNear;
        mode.max_lod_x16 = 32u;
        const auto decoded = cache.get(image, mode, TlutRef{}, &memory);
        // Independent tiled footprint: 8x8=4*32, 4x4=32, 2x2=32.
        passed &= expect(decoded.guest_byte_size == 192u && decoded.mip_levels == 3u,
            "authored mip handle retains all three block-rounded source levels");

        for (auto format : {TexFormat::I4, TexFormat::I8, TexFormat::IA4, TexFormat::IA8,
                            TexFormat::RGB565, TexFormat::RGB5A3, TexFormat::RGBA8, TexFormat::CMPR}) {
            auto direct_image = image; direct_image.guest_addr = address + 768u;
            direct_image.format = format;
            const auto original = cache.get(direct_image, mode, TlutRef{}, &memory);
            for (auto palette_format : {TlutFormat::IA8, TlutFormat::RGB565, TlutFormat::RGB5A3})
                for (std::uint16_t slot : {std::uint16_t{0}, std::uint16_t{1}, std::uint16_t{1023}}) {
                const auto reused = cache.get(direct_image, mode, TlutRef{slot, palette_format}, &memory);
                passed &= expect(reused.resource == original.resource && reused.srv_index == original.srv_index &&
                    reused.guest_byte_size == original.guest_byte_size && reused.mip_levels == original.mip_levels,
                    "direct texture formats reuse the same resource and descriptor across irrelevant palette changes");
            }
        }
        auto indexed_image = image; indexed_image.guest_addr = address + 512u;
        indexed_image.format = TexFormat::C4;
        TexMode indexed_mode{}; indexed_mode.min_filter = TexMinFilter::Near;
        const auto indexed = cache.get(indexed_image, indexed_mode, TlutRef{0u, TlutFormat::IA8}, &memory);
        const auto other_palette_format = cache.get(indexed_image, indexed_mode, TlutRef{0u, TlutFormat::RGB565}, &memory);
        const auto other_palette_slot = cache.get(indexed_image, indexed_mode, TlutRef{1u, TlutFormat::IA8}, &memory);
        passed &= expect(indexed.srv_index != other_palette_format.srv_index &&
            indexed.srv_index != other_palette_slot.srv_index,
            "indexed textures retain palette format and slot dependencies even when palette bytes match");
        constexpr auto palette_source = address + 1536u;
        passed &= expect(!cache.load_tlut(palette_source >> 5u, 1u << 10u, &memory),
            "32 unchanged palette bytes report no binding invalidation");
        const auto after_identical_reload = cache.get(indexed_image, indexed_mode, TlutRef{0u, TlutFormat::IA8}, &memory);
        passed &= expect(after_identical_reload.resource == indexed.resource &&
            after_identical_reload.srv_index == indexed.srv_index,
            "identical palette reload retains its decoded texture and descriptor");
        bytes[1536] = std::byte{0xff};
        passed &= expect(cache.load_tlut(palette_source >> 5u, 1u << 10u, &memory),
            "changed palette bytes report required binding invalidation");
        const auto after_changed_reload = cache.get(indexed_image, indexed_mode, TlutRef{0u, TlutFormat::IA8}, &memory);
        const auto unaffected_slot = cache.get(indexed_image, indexed_mode, TlutRef{1u, TlutFormat::IA8}, &memory);
        passed &= expect(after_changed_reload.resource != indexed.resource &&
            unaffected_slot.resource == other_palette_slot.resource && unaffected_slot.srv_index == other_palette_slot.srv_index,
            "changed palette bytes retire overlapping resources while retaining an unaffected slot");

        auto c8_image = indexed_image; c8_image.format = TexFormat::C8;
        const auto c8_before = cache.get(c8_image, indexed_mode, TlutRef{}, &memory);
        bytes[1536u + 510u] = std::byte{0x12};  // outside C4, inside C8
        passed &= expect(cache.load_tlut(palette_source >> 5u, 16u << 10u, &memory),
            "wide TLUT transfer reports changed C8-tail bytes");
        const auto c4_after_tail = cache.get(indexed_image, indexed_mode, TlutRef{}, &memory);
        const auto c8_after_tail = cache.get(c8_image, indexed_mode, TlutRef{}, &memory);
        passed &= expect(c4_after_tail.resource == after_changed_reload.resource &&
            c4_after_tail.srv_index == after_changed_reload.srv_index &&
            c8_after_tail.resource != c8_before.resource,
            "changed transfer preserves identical C4 prefix but retires changed C8 palette");
        bytes[1538u] = std::byte{0x77};  // now both palettes consume changed bytes
        passed &= expect(cache.load_tlut(palette_source >> 5u, 16u << 10u, &memory),
            "wide TLUT transfer reports changed shared prefix");
        const auto c4_after_prefix = cache.get(indexed_image, indexed_mode, TlutRef{}, &memory);
        const auto c8_after_prefix = cache.get(c8_image, indexed_mode, TlutRef{}, &memory);
        passed &= expect(c4_after_prefix.resource != c4_after_tail.resource &&
            c8_after_prefix.resource != c8_after_tail.resource,
            "changed shared prefix retires both C4 and C8 textures");
        passed &= expect(!cache.load_tlut(palette_source >> 5u, 16u << 10u, &memory),
            "identical wide reload reports no change");
        const auto c4_after_repeat = cache.get(indexed_image, indexed_mode, TlutRef{}, &memory);
        const auto c8_after_repeat = cache.get(c8_image, indexed_mode, TlutRef{}, &memory);
        passed &= expect(c4_after_repeat.resource == c4_after_prefix.resource &&
            c4_after_repeat.srv_index == c4_after_prefix.srv_index &&
            c8_after_repeat.resource == c8_after_prefix.resource &&
            c8_after_repeat.srv_index == c8_after_prefix.srv_index,
            "identical wide reload retains both resources and descriptors");

        auto c14_image = indexed_image; c14_image.format = TexFormat::C14X2;
        const auto c14_before_partial = cache.get(c14_image, indexed_mode, TlutRef{}, &memory);
        // Slot 1 is inside the C14 palette starting at slot 0, but outside
        // the C4/C8 palettes starting there. Incoming prefix differs from zero.
        passed &= expect(cache.load_tlut(palette_source >> 5u, (1u << 10u) | 1u, &memory),
            "partial transfer into later slot changes C14 palette bytes");
        const auto c14_after_partial = cache.get(c14_image, indexed_mode, TlutRef{}, &memory);
        const auto c8_after_partial = cache.get(c8_image, indexed_mode, TlutRef{}, &memory);
        passed &= expect(c14_after_partial.resource != c14_before_partial.resource &&
            c8_after_partial.resource == c8_after_repeat.resource &&
            c8_after_partial.srv_index == c8_after_repeat.srv_index,
            "partial transfer retires encompassing C14 palette without touching C8 prefix");

        const auto original_config = get_render_config();
        auto mip_config = original_config; mip_config.enhanced_mipmaps = false;
        set_render_config(mip_config);
        auto enhanced_image = image; enhanced_image.guest_addr = address + 1280u;
        enhanced_image.width = 64u; enhanced_image.height = 4u;
        TexMode enhanced_mode{};
        enhanced_mode.min_filter = TexMinFilter::Linear;
        enhanced_mode.mag_filter = TexMagFilter::Linear;
        const auto no_enhancement = cache.get(enhanced_image, enhanced_mode, TlutRef{}, &memory);
        mip_config.enhanced_mipmaps = true; set_render_config(mip_config);
        const auto enhancement = cache.get(enhanced_image, enhanced_mode, TlutRef{}, &memory);
        set_render_config(original_config);
        passed &= expect(no_enhancement.mip_levels == 1u && !no_enhancement.generated_mips &&
            enhancement.mip_levels == 7u && enhancement.generated_mips &&
            enhancement.resource != no_enhancement.resource && enhancement.guest_byte_size == 512u,
            "eligible native mip generation still reads live options and keeps its resource separate from the base texture");

        // Complete the actual recorded upload before retirement/shutdown. This
        // test owns its queue and waits for GPU completion, not just CPU submit.
        check_hr(list->Close(), "Close texture cache uploads");
        ID3D12CommandList* lists[]{list.Get()};
        queue_->ExecuteCommandLists(1u, lists);
        const auto value = ++fence_value_;
        check_hr(queue_->Signal(fence_.Get(), value), "Signal texture cache uploads");
        check_hr(fence_->SetEventOnCompletion(value, fence_event_), "Wait texture cache uploads");
        if (WaitForSingleObject(fence_event_, 30000u) != WAIT_OBJECT_0) {
            throw std::runtime_error("texture cache upload fence timeout");
        }
        passed &= expect(cache.invalidate_guest_range(address + 17u, 0u) == 0u,
            "zero-byte write inside decoded texture does not evict it");
        // The zero range is inside the separate direct textures at +768;
        // only the real write in the base texture's lower mip may retire one.
        const std::array<TextureCache::GuestRange, 2> mip_dirty{{
            {address + 160u, 1u}, {address + 785u, 0u}}};
        passed &= expect(cache.invalidate_guest_ranges(mip_dirty) == 1u,
            "write confined to a lower mip retires the decoded resource");

        const auto make_alias = [&] {
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = desc.Height = 8u;
            desc.DepthOrArraySize = desc.MipLevels = 1u;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1u;
            ComPtr<ID3D12Resource> texture;
            check_hr(device_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE,
                &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr,
                IID_PPV_ARGS(&texture)), "Create texture cache alias");
            return texture;
        };
        EfbCopyParams copy{};
        copy.src_width = copy.src_height = 8u;
        copy.target_format = 6u;
        copy.dest_addr = address;
        copy.dest_stride = 512u;
        auto alias = make_alias();
        passed &= expect(cache.register_efb_copy(address, copy, alias), "register strided alias");
        passed &= expect(cache.invalidate_guest_range(address + 17u, 0u) == 0u &&
            !cache.register_efb_copy(address, copy, alias),
            "zero-byte write preserves existing alias ownership");
        passed &= expect(cache.invalidate_guest_range(address + 512u, 1u) == 1u,
            "write to strided second block-row invalidates its alias");
        passed &= expect(cache.register_efb_copy(address, copy, alias), "register alias again");
        copy.dest_stride = 128u;
        passed &= expect(cache.register_efb_copy(address, copy, alias),
            "same GPU resource with changed guest footprint requests outer cache refresh");
        passed &= expect(cache.invalidate_guest_range(address + 512u, 1u) == 0u,
            "updated packed footprint excludes prior stride gap");
        copy.dest_stride = 512u;
        passed &= expect(cache.register_efb_copy(address, copy, alias) &&
            cache.invalidate_guest_range(address + 512u, 1u) == 1u,
            "same-resource alias extension publishes its new guest footprint");
        copy.dest_stride = 128u;
        passed &= expect(cache.register_efb_copy(address, copy, alias),
            "restore packed alias after footprint-extension eviction");
        image.format = TexFormat::RGBA8;
        mode.min_filter = TexMinFilter::Near;
        passed &= expect(cache.get(image, mode, TlutRef{}, &memory).guest_byte_size == 256u,
            "exact alias handle publishes current packed footprint");
        bool rejected = false;
        try { (void)cache.register_efb_copy(address, copy, {}); }
        catch (const std::runtime_error&) { rejected = true; }
        passed &= expect(rejected, "null alias is rejected before retiring live entry");
        passed &= expect(cache.get(image, mode, TlutRef{}, &memory).resource == alias.Get(),
            "rejected alias leaves the previous resource owned and bindable");
        cache.set_upload_list(nullptr);
        cache.shutdown();
        return passed;
    }

private:
    std::vector<Pixel> run_bytes(
        DXGI_FORMAT source_format,
        const std::vector<std::uint8_t>& source_bytes,
        UINT source_width,
        UINT source_height,
        UINT dest_size,
        const galaxy::gx::EfbConversionShaderConstants& constants,
        const GxPixelCase* gx = nullptr, float* depth_result = nullptr) {
        const D3D12_HEAP_PROPERTIES default_heap =
            heap_properties(D3D12_HEAP_TYPE_DEFAULT);
        const D3D12_HEAP_PROPERTIES upload_heap =
            heap_properties(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_HEAP_PROPERTIES readback_heap =
            heap_properties(D3D12_HEAP_TYPE_READBACK);

        ComPtr<ID3D12RootSignature> gx_root;
        ComPtr<ID3D12PipelineState> gx_pipeline;
        ComPtr<ID3D12Resource> gx_constants;
        if (gx != nullptr) {
            check_hr(galaxy::gx::create_gx_root_signature(device_.Get(), &gx_root), "Create GX test root");
            static constexpr char kGxVs[] = R"HLSL(
struct PSIn { float4 pos : SV_Position; float4 col0 : COLOR0;
              float4 col1 : COLOR1; float3 uv[8] : TEXCOORD; };
PSIn main(uint id : SV_VertexID) {
    float2 uv = float2((id << 1) & 2, id & 2);
    PSIn p;
    p.pos = float4(uv * float2(2,-2) + float2(-1,1), 0.75, 1);
    p.col0 = p.col1 = float4(1,0,0,0.5);
    [unroll] for (uint i=0; i<8; ++i) p.uv[i] = float3(0.5,0.5,1);
    return p;
})HLSL";
            const auto vs = compile_shader(kGxVs, "vs_5_1");
            const auto ps = compile_shader(galaxy::gx::ShaderGenerator{}.generate_ps(gx->shader), "ps_5_1");
            auto desc = galaxy::gx::make_gx_pso_desc(gx_root.Get(), gx->state,
                vs.Get(), ps.Get(), nullptr, nullptr);
            // The fixture VS emits a full-screen triangle using SV_VertexID.
            desc.InputLayout = {};
            check_hr(device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&gx_pipeline)), "Create generated GX test PSO");
            const auto desc_constants = buffer_desc(sizeof(gx->constants));
            check_hr(device_->CreateCommittedResource(&upload_heap, D3D12_HEAP_FLAG_NONE,
                &desc_constants, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                IID_PPV_ARGS(&gx_constants)), "Create GX constants");
            void* mapped = nullptr;
            const D3D12_RANGE no_read{0u, 0u};
            check_hr(gx_constants->Map(0u, &no_read, &mapped), "Map GX constants");
            std::memcpy(mapped, &gx->constants, sizeof(gx->constants));
            gx_constants->Unmap(0u, nullptr);
        }

        const D3D12_RESOURCE_DESC source_desc =
            texture_desc(source_format, source_width, source_height);
        ComPtr<ID3D12Resource> source;
        check_hr(
            device_->CreateCommittedResource(
                &default_heap,
                D3D12_HEAP_FLAG_NONE,
                &source_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&source)),
            "Create source texture");

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT source_footprint{};
        UINT source_rows = 0u;
        UINT64 source_row_size = 0u;
        UINT64 source_upload_size = 0u;
        device_->GetCopyableFootprints(
            &source_desc,
            0u,
            1u,
            0u,
            &source_footprint,
            &source_rows,
            &source_row_size,
            &source_upload_size);
        static_cast<void>(source_row_size);
        if (source_rows != source_height) {
            throw std::runtime_error("unexpected source footprint row count");
        }
        ComPtr<ID3D12Resource> upload;
        const D3D12_RESOURCE_DESC upload_desc = buffer_desc(source_upload_size);
        check_hr(
            device_->CreateCommittedResource(
                &upload_heap,
                D3D12_HEAP_FLAG_NONE,
                &upload_desc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&upload)),
            "Create source upload buffer");
        void* upload_mapping = nullptr;
        const D3D12_RANGE no_read{0u, 0u};
        check_hr(upload->Map(0u, &no_read, &upload_mapping), "Map source upload");
        const std::size_t tight_row_bytes =
            static_cast<std::size_t>(source_width) * 4u;
        if (source_bytes.size() != tight_row_bytes * source_height) {
            throw std::runtime_error("source byte count does not match dimensions");
        }
        auto* upload_base = static_cast<std::uint8_t*>(upload_mapping) +
            source_footprint.Offset;
        for (UINT y = 0u; y < source_height; ++y) {
            std::memcpy(
                upload_base +
                    static_cast<std::size_t>(y) *
                        source_footprint.Footprint.RowPitch,
                source_bytes.data() + static_cast<std::size_t>(y) * tight_row_bytes,
                tight_row_bytes);
        }
        const D3D12_RANGE upload_written{0u, static_cast<SIZE_T>(source_upload_size)};
        upload->Unmap(0u, &upload_written);

        const D3D12_RESOURCE_DESC dest_desc = texture_desc(
            DXGI_FORMAT_R8G8B8A8_UNORM,
            dest_size,
            dest_size,
            D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        ComPtr<ID3D12Resource> destination;
        check_hr(
            device_->CreateCommittedResource(
                &default_heap,
                D3D12_HEAP_FLAG_NONE,
                &dest_desc,
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                nullptr,
                IID_PPV_ARGS(&destination)),
            "Create conversion destination");

        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc{};
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.NumDescriptors = 1u;
        ComPtr<ID3D12DescriptorHeap> rtv_heap;
        check_hr(
            device_->CreateDescriptorHeap(
                &rtv_heap_desc, IID_PPV_ARGS(&rtv_heap)),
            "Create RTV heap");
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv =
            rtv_heap->GetCPUDescriptorHandleForHeapStart();
        device_->CreateRenderTargetView(destination.Get(), nullptr, rtv);

        ComPtr<ID3D12Resource> depth, depth_readback;
        ComPtr<ID3D12DescriptorHeap> dsv_heap;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT depth_footprint{};
        if (gx != nullptr) {
            const auto depth_desc = texture_desc(galaxy::gx::kEfbDepthResourceFormat,
                1u, 1u, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
            check_hr(device_->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
                &depth_desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, nullptr, IID_PPV_ARGS(&depth)), "Create GX depth");
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            heap.NumDescriptors = 1u;
            check_hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&dsv_heap)), "Create GX DSV heap");
            dsv = dsv_heap->GetCPUDescriptorHandleForHeapStart();
            D3D12_DEPTH_STENCIL_VIEW_DESC view{};
            view.Format = galaxy::gx::kEfbDepthFormat;
            view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(depth.Get(), &view, dsv);
            UINT64 bytes = 0u;
            device_->GetCopyableFootprints(&depth_desc, 0u, 1u, 0u,
                &depth_footprint, nullptr, nullptr, &bytes);
            const auto desc_readback = buffer_desc(bytes);
            check_hr(device_->CreateCommittedResource(&readback_heap, D3D12_HEAP_FLAG_NONE,
                &desc_readback, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(&depth_readback)), "Create GX depth readback");
        }

        D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc{};
        srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srv_heap_desc.NumDescriptors = gx != nullptr ? 8u : 1u;
        srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        ComPtr<ID3D12DescriptorHeap> srv_heap;
        check_hr(
            device_->CreateDescriptorHeap(
                &srv_heap_desc, IID_PPV_ARGS(&srv_heap)),
            "Create SRV heap");
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = source_format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MipLevels = 1u;
        device_->CreateShaderResourceView(
            source.Get(),
            &srv_desc,
            srv_heap->GetCPUDescriptorHandleForHeapStart());
        ComPtr<ID3D12DescriptorHeap> sampler_heap;
        if (gx != nullptr) {
            auto srv = srv_heap->GetCPUDescriptorHandleForHeapStart();
            const UINT stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            for (unsigned i = 1u; i < 8u; ++i) {
                srv.ptr += stride;
                device_->CreateShaderResourceView(source.Get(), &srv_desc, srv);
            }
            D3D12_DESCRIPTOR_HEAP_DESC heap{};
            heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
            heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            heap.NumDescriptors = 8u;
            check_hr(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&sampler_heap)), "Create GX sampler heap");
            D3D12_SAMPLER_DESC sampler{};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
            sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.MaxLOD = D3D12_FLOAT32_MAX;
            auto handle = sampler_heap->GetCPUDescriptorHandleForHeapStart();
            const UINT sampler_stride = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
            for (unsigned i = 0u; i < 8u; ++i) {
                device_->CreateSampler(&sampler, handle);
                handle.ptr += sampler_stride;
            }
        }

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT dest_footprint{};
        UINT dest_rows = 0u;
        UINT64 dest_row_size = 0u;
        UINT64 readback_size = 0u;
        device_->GetCopyableFootprints(
            &dest_desc,
            0u,
            1u,
            0u,
            &dest_footprint,
            &dest_rows,
            &dest_row_size,
            &readback_size);
        static_cast<void>(dest_row_size);
        if (dest_rows != dest_size) {
            throw std::runtime_error("unexpected destination footprint row count");
        }
        ComPtr<ID3D12Resource> readback;
        const D3D12_RESOURCE_DESC readback_desc = buffer_desc(readback_size);
        check_hr(
            device_->CreateCommittedResource(
                &readback_heap,
                D3D12_HEAP_FLAG_NONE,
                &readback_desc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&readback)),
            "Create destination readback");

        ComPtr<ID3D12CommandAllocator> allocator;
        check_hr(
            device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)),
            "Create command allocator");
        ComPtr<ID3D12GraphicsCommandList> command_list;
        check_hr(
            device_->CreateCommandList(
                0u,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(),
                nullptr,
                IID_PPV_ARGS(&command_list)),
            "Create command list");

        D3D12_TEXTURE_COPY_LOCATION upload_location{};
        upload_location.pResource = upload.Get();
        upload_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        upload_location.PlacedFootprint = source_footprint;
        D3D12_TEXTURE_COPY_LOCATION source_location{};
        source_location.pResource = source.Get();
        source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        command_list->CopyTextureRegion(
            &source_location, 0u, 0u, 0u, &upload_location, nullptr);
        D3D12_RESOURCE_BARRIER source_barrier{};
        source_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        source_barrier.Transition.pResource = source.Get();
        source_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        source_barrier.Transition.StateAfter =
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        source_barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1u, &source_barrier);

        const D3D12_VIEWPORT viewport{
            0.0f,
            0.0f,
            static_cast<float>(dest_size),
            static_cast<float>(dest_size),
            0.0f,
            1.0f,
        };
        const D3D12_RECT scissor{
            0,
            0,
            static_cast<LONG>(dest_size),
            static_cast<LONG>(dest_size),
        };
        command_list->RSSetViewports(1u, &viewport);
        command_list->RSSetScissorRects(1u, &scissor);
        command_list->OMSetRenderTargets(1u, &rtv, FALSE, gx != nullptr ? &dsv : nullptr);
        if (gx != nullptr) {
            const float background[4]{0.0f, 0.0f, 1.0f, 0.25f};
            command_list->ClearRenderTargetView(rtv, background, 0u, nullptr);
            command_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.25f, 0u, 0u, nullptr);
            command_list->SetGraphicsRootSignature(gx_root.Get());
            command_list->SetPipelineState(gx_pipeline.Get());
            ID3D12DescriptorHeap* heaps[]{srv_heap.Get(), sampler_heap.Get()};
            command_list->SetDescriptorHeaps(2u, heaps);
            command_list->SetGraphicsRootConstantBufferView(1u, gx_constants->GetGPUVirtualAddress());
            command_list->SetGraphicsRootDescriptorTable(2u, srv_heap->GetGPUDescriptorHandleForHeapStart());
            command_list->SetGraphicsRootDescriptorTable(3u, sampler_heap->GetGPUDescriptorHandleForHeapStart());
        } else {
            command_list->SetGraphicsRootSignature(root_signature_.Get());
            command_list->SetPipelineState(pipeline_.Get());
            ID3D12DescriptorHeap* descriptor_heaps[] = {srv_heap.Get()};
            command_list->SetDescriptorHeaps(1u, descriptor_heaps);
            command_list->SetGraphicsRoot32BitConstants(0u, 16u, constants.values.data(), 0u);
            command_list->SetGraphicsRootDescriptorTable(1u, srv_heap->GetGPUDescriptorHandleForHeapStart());
        }
        command_list->IASetPrimitiveTopology(
            D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        command_list->DrawInstanced(3u, 1u, 0u, 0u);

        D3D12_RESOURCE_BARRIER dest_barrier{};
        dest_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        dest_barrier.Transition.pResource = destination.Get();
        dest_barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        dest_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        dest_barrier.Transition.Subresource =
            D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1u, &dest_barrier);
        D3D12_TEXTURE_COPY_LOCATION dest_texture_location{};
        dest_texture_location.pResource = destination.Get();
        dest_texture_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION readback_location{};
        readback_location.pResource = readback.Get();
        readback_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        readback_location.PlacedFootprint = dest_footprint;
        command_list->CopyTextureRegion(
            &readback_location,
            0u,
            0u,
            0u,
            &dest_texture_location,
            nullptr);
        if (gx != nullptr) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = depth.Get();
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_DEPTH_WRITE;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            command_list->ResourceBarrier(1u, &barrier);
            D3D12_TEXTURE_COPY_LOCATION texture{}, buffer{};
            texture.pResource = depth.Get();
            texture.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            buffer.pResource = depth_readback.Get();
            buffer.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            buffer.PlacedFootprint = depth_footprint;
            command_list->CopyTextureRegion(&buffer, 0u, 0u, 0u, &texture, nullptr);
        }
        check_hr(command_list->Close(), "Close command list");
        ID3D12CommandList* lists[] = {command_list.Get()};
        queue_->ExecuteCommandLists(1u, lists);
        const UINT64 fence_value = ++fence_value_;
        check_hr(queue_->Signal(fence_.Get(), fence_value), "Signal WARP fence");
        if (fence_->GetCompletedValue() < fence_value) {
            check_hr(
                fence_->SetEventOnCompletion(fence_value, fence_event_),
                "SetEventOnCompletion");
            const DWORD wait_result = WaitForSingleObject(fence_event_, 30'000u);
            if (wait_result != WAIT_OBJECT_0) {
                throw std::runtime_error("timed out waiting for WARP conversion");
            }
        }

        void* mapped = nullptr;
        const D3D12_RANGE read_range{0u, static_cast<SIZE_T>(readback_size)};
        check_hr(readback->Map(0u, &read_range, &mapped), "Map readback");
        const auto* base = static_cast<const std::uint8_t*>(mapped) +
            dest_footprint.Offset;
        std::vector<Pixel> pixels(
            static_cast<std::size_t>(dest_size) * dest_size);
        for (UINT y = 0u; y < dest_size; ++y) {
            const auto* row = base +
                static_cast<std::size_t>(y) * dest_footprint.Footprint.RowPitch;
            for (UINT x = 0u; x < dest_size; ++x) {
                const std::size_t source_offset = static_cast<std::size_t>(x) * 4u;
                pixels[static_cast<std::size_t>(y) * dest_size + x] = Pixel{
                    row[source_offset + 0u],
                    row[source_offset + 1u],
                    row[source_offset + 2u],
                    row[source_offset + 3u],
                };
            }
        }
        const D3D12_RANGE no_write{0u, 0u};
        readback->Unmap(0u, &no_write);
        if (depth_result != nullptr) {
            void* depth_mapping = nullptr;
            const D3D12_RANGE range{0u, 4u};
            check_hr(depth_readback->Map(0u, &range, &depth_mapping), "Map GX depth readback");
            std::uint32_t encoded = 0u;
            std::memcpy(&encoded, static_cast<const std::uint8_t*>(depth_mapping) + depth_footprint.Offset, sizeof(encoded));
            *depth_result = static_cast<float>(encoded & 0xffffffu) / 16777215.0f;
            depth_readback->Unmap(0u, &no_write);
        }
        return pixels;
    }

    ComPtr<ID3D12Device> device_;
    ComPtr<ID3D12CommandQueue> queue_;
    ComPtr<ID3D12RootSignature> root_signature_;
    ComPtr<ID3D12PipelineState> pipeline_;
    ComPtr<ID3D12Fence> fence_;
    HANDLE fence_event_ = nullptr;
    UINT64 fence_value_ = 0u;
};

GxPixelCase gx_pixel_fixture() {
    GxPixelCase input;
    input.shader.num_tev_stages = 1u;
    input.shader.num_texgens = 1u;
    input.shader.num_chans = 1u;
    input.shader.alpha_comp0 = input.shader.alpha_comp1 = 7u;
    input.shader.flags = 1u; // Late testing unless a case explicitly selects early.
    input.shader.stages[0].color_env = 10u | (15u << 4u) | (15u << 8u) |
        (15u << 12u) | (1u << 19u); // D = raster, A/B/C = zero.
    input.shader.stages[0].alpha_env = (5u << 4u) | (7u << 7u) |
        (7u << 10u) | (7u << 13u) | (1u << 19u);
    input.shader.stages[0].order = 1u << 6u;
    input.shader.stages[0].ksel = (1u << 12u) | (2u << 14u) | (3u << 16u) |
        (1u << 21u) | (2u << 23u) | (3u << 25u);
    input.state.primitive_topology = 3u;
    input.state.blend_bits = (1u << 25u) | (1u << 26u);
    input.state.zmode_bits = 31u; // Enable, ALWAYS, update.
    for (auto& dims : input.constants.tex_dims) std::fill(std::begin(dims), std::end(dims), 1.0f);
    return input;
}

bool test_gx_pixel_readbacks() {
    bool passed = true;
    try {
        WarpConversionHarness harness;
        auto input = gx_pixel_fixture();
        input.shader.alpha_comp0 = 4u; // Greater than 3/4 fails for alpha=1/2.
        input.constants.alpha_refs[0] = 0.75f;
        for (bool early : {true, false}) {
            input.shader.flags = early ? 0u : 1u;
            const auto output = harness.run_gx(input);
            passed &= expect_pixel(output.color, {0u, 0u, 255u, 64u},
                "alpha-discard leaves the cleared color untouched");
            passed &= expect(std::abs(output.depth - (early ? 0.75f : 0.25f)) < 2.0f / 16777216.0f,
                "early alpha failure writes geometric depth while late failure preserves depth");
        }

        // All formats use bytes from the unmodified, last enabled texture.
        // The raw source has unequal channels so swizzle/format mistakes differ.
        constexpr std::uint32_t depths[]{120u, 30738u, 0x123456u};
        for (unsigned format = 0u; format < 3u; ++format) {
            for (bool disabled_tail : {false, true}) {
                input = gx_pixel_fixture();
                input.shader.flags = static_cast<std::uint8_t>(1u | 2u | (2u << 3u) | (format << 5u));
                // Deliberately reverse TEV's texture swap independently of Z.
                input.shader.stages[0].ksel = (input.shader.stages[0].ksel & ((1u << 18u) - 1u)) |
                    (3u << 19u) | (2u << 21u) | (1u << 23u);
                if (disabled_tail) {
                    input.shader.num_tev_stages = 2u;
                    input.shader.stages[1] = input.shader.stages[0];
                    input.shader.stages[1].order = 0u;
                }
                const auto output = harness.run_gx(input);
                const float expected = 1.0f - static_cast<float>(depths[format]) / 16777216.0f;
                passed &= expect(std::abs(output.depth - expected) < 2.0f / 16777216.0f,
                    "late Z8/Z16/Z24 depth preserves raw sample across swaps and disabled tail");
            }
        }
        input = gx_pixel_fixture();
        input.shader.flags = 1u | 2u | (2u << 3u); // Z8 replace.
        input.constants.ztex_params[0] = static_cast<float>(0xfffff0u) / 16777216.0f;
        passed &= expect(std::abs(harness.run_gx(input).depth -
            (1.0f - 104.0f / 16777216.0f)) < 2.0f / 16777216.0f,
            "Z texture plus bias wraps modulo 24 bits instead of saturating");
        input.shader.flags = 1u | 2u | (1u << 3u); // Z8 add to geometric z=0x400000.
        passed &= expect(std::abs(harness.run_gx(input).depth -
            (1.0f - static_cast<float>(0x400068u) / 16777216.0f)) < 2.0f / 16777216.0f,
            "additive Z texture includes geometric depth in the 24-bit wrapped sum");

        input = gx_pixel_fixture();
        input.texture = {128u, 0u, 0u, 32u};
        input.shader.flags = 2u | (2u << 3u) | (2u << 5u); // Early Z24 replace.
        input.shader.fog_type = 2u;
        input.constants.fog_params[0] = 1.0f;
        input.constants.fog_params[5] = 1.0f; // Green fog.
        input.constants.fog_params[7] = 1.0f; // Orthographic.
        const auto fog = harness.run_gx(input);
        // Each 255*128 contribution truncates separately in the integer
        // final sum's >>8: both red and green are 127, not float-rounded 128.
        passed &= expect_pixel(fog.color, {127u, 127u, 0u, 130u},
            "early Z-texture modifies integer fog color before physical output quantization");
        passed &= expect(std::abs(fog.depth - 0.75f) < 2.0f / 16777216.0f,
            "early Z-texture fog retains geometric depth in the EFB");

        for (bool overwrite : {false, true}) {
            input = gx_pixel_fixture();
            input.state.pixfmt = input.shader.output_flags = 1u;
            input.shader.flags = static_cast<std::uint8_t>(1u | 128u | (overwrite ? 4u : 0u));
            input.constants.alpha_refs[3] = 64.0f / 255.0f;
            input.state.blend_bits |= 1u | (4u << 2u) | (5u << 5u) | (1u << 27u) |
                (overwrite ? (1u << 28u) : 0u);
            const auto output = harness.run_gx(input);
            passed &= expect(std::abs(static_cast<int>(output.color.r) - 128) <= 1 &&
                std::abs(static_cast<int>(output.color.b) - 127) <= 1,
                "dual-source RGB blending consumes original TEV alpha in both overwrite states");
            passed &= expect(std::abs(static_cast<int>(output.color.a) - (overwrite ? 65 : 98)) <= 1,
                "constant EFB alpha overwrite is independent of dual-source routing");
        }
        input = gx_pixel_fixture();
        input.state.pixfmt = input.shader.output_flags = 1u;
        input.shader.flags = 1u | 4u; // Constant alpha without dual-source routing.
        input.constants.alpha_refs[3] = 64.0f / 255.0f;
        input.state.blend_bits |= 1u | (1u << 2u) | (1u << 28u); // ONE/ZERO RGB.
        const auto constant_only = harness.run_gx(input);
        passed &= expect_pixel(constant_only.color, {255u, 0u, 0u, 65u},
            "constant alpha overwrite works independently when RGB needs no SRC1 alpha");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAILED: WARP GX pixel semantics: %s\n", e.what());
        passed = false;
    }
    return passed;
}

bool test_clear_precision() {
    bool passed = true;
    const auto bytes = [](const galaxy::gx::EfbClearValues& v) {
        return Pixel{static_cast<std::uint8_t>(std::lround(v.color[0] * 255.0f)),
            static_cast<std::uint8_t>(std::lround(v.color[1] * 255.0f)),
            static_cast<std::uint8_t>(std::lround(v.color[2] * 255.0f)),
            static_cast<std::uint8_t>(std::lround(v.color[3] * 255.0f))};
    };
    passed &= expect_pixel(bytes(galaxy::gx::make_efb_clear_values(0x7f13u, 0x579bu, 0x123456u, 0u)),
        {19u, 87u, 155u, 255u}, "RGB8 clears preserve each full 8-bit color channel");
    passed &= expect_pixel(bytes(galaxy::gx::make_efb_clear_values(0x7f13u, 0x579bu, 0x123456u, 1u)),
        {16u, 85u, 154u, 125u}, "RGBA6 clears quantize only to the 6-bit lattice");
    const auto rgb565 = galaxy::gx::make_efb_clear_values(0x7f13u, 0x579bu, 0x123456u, 2u);
    passed &= expect_pixel(bytes(rgb565), {16u, 85u, 156u, 255u}, "RGB565 clears use 5/6/5 precision");
    passed &= expect(rgb565.reversed_depth == 1.0f - 0x123412u / 16777216.0f,
        "RGB565 Z16 clear expands the high sixteen depth bits to Z24");
    return passed;
}

galaxy::gx::EfbConversionShaderConstants constants_for_case(
    const galaxy::gx::EfbCopyParams& params,
    unsigned scale,
    bool is_depth,
    bool intensity,
    std::uint8_t format) {
    const galaxy::gx::EfbCopySamplingGeometry sampling =
        galaxy::gx::compute_efb_copy_sampling_geometry(
            params, scale, scale, scale);
    return galaxy::gx::make_efb_conversion_constants(
        params,
        is_depth,
        intensity,
        format == 2u || format == 3u,
        format,
        sampling.src_x,
        sampling.src_y,
        sampling.step_x,
        sampling.step_y,
        sampling.filter_row_offset,
        scale,
        3u);
}

bool expect_uniform_pixels(
    const std::vector<Pixel>& pixels,
    unsigned scale,
    Pixel expected,
    const char* label) {
    if (pixels.size() != static_cast<std::size_t>(scale) * scale) {
        std::fprintf(stderr, "FAILED: %s returned the wrong pixel count\n", label);
        return false;
    }
    for (const Pixel pixel : pixels) {
        if (!(pixel == expected)) {
            return expect_pixel(pixel, expected, label);
        }
    }
    return true;
}

bool test_scaled_xfb_filter() {
    bool passed = true;
    WarpConversionHarness harness;
    const std::array<Pixel, 3> rows{
        Pixel{0u, 0u, 0u, 1u}, Pixel{255u, 0u, 0u, 77u},
        Pixel{0u, 0u, 0u, 2u}};
    const auto params = make_filter_params({8u, 8u, 10u, 12u, 10u, 8u, 8u});
    for (const unsigned scale : {1u, 2u, 6u}) {
        const auto sampling = galaxy::gx::compute_xfb_sampling_geometry(
            params, scale, scale, scale);
        const auto texture = galaxy::gx::compute_efb_copy_sampling_geometry(
            params, scale, scale, scale);
        passed &= expect(sampling.src_y == static_cast<float>(scale) &&
            sampling.step_x == 1.0f && sampling.step_y == 1.0f &&
            sampling.filter_row_offset == 1.0f &&
            texture.filter_row_offset == static_cast<float>(scale),
            "display filter footprint leaves source mapping and texture copies intact");
        const auto constants = galaxy::gx::make_efb_conversion_constants(
            params, false, false, true, 15u, sampling.src_x, sampling.src_y,
            sampling.step_x, sampling.step_y, sampling.filter_row_offset, scale, 3u);
        const auto result = harness.run_color(rows, scale, constants);
        for (unsigned y = 0; y < scale; ++y) {
            const auto red = static_cast<std::uint8_t>(
                ((y == 0 ? 0u : 255u) * 16u + 255u * 32u +
                 (y + 1u == scale ? 0u : 255u) * 16u) >> 6u);
            for (unsigned x = 0; x < scale; ++x) {
                passed &= expect_pixel(result[y * scale + x], {red, 0u, 0u, 255u},
                    "scaled XFB filter affects adjacent edge rows, retaining brightness and opaque XFB alpha");
            }
        }
    }
    return passed;
}

bool test_cpu_oracle() {
    bool passed = true;
    const std::array<Pixel, 3> rows{
        Pixel{10u, 20u, 30u, 40u},
        Pixel{40u, 50u, 60u, 77u},
        Pixel{70u, 80u, 90u, 100u},
    };
    galaxy::gx::EfbCopyParams filter = make_filter_params(
        {1u, 2u, 4u, 8u, 16u, 17u, 16u});
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 6u),
        Pixel{54u, 64u, 74u, 77u},
        "integer oracle unbounded vertical filter");
    filter.clamp_top = true;
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 6u),
        Pixel{55u, 65u, 75u, 77u},
        "integer oracle top-only clamp");
    filter.clamp_top = false;
    filter.clamp_bottom = true;
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 6u),
        Pixel{38u, 48u, 58u, 77u},
        "integer oracle bottom-only clamp");
    filter.clamp_top = true;
    passed &= expect_pixel(
        oracle_from_three_rows(rows, filter, false, 6u),
        Pixel{40u, 50u, 60u, 77u},
        "integer oracle clamps both filter edges");

    const std::array<Pixel, 3> white_rows{
        Pixel{255u, 255u, 255u, 100u},
        Pixel{255u, 255u, 255u, 200u},
        Pixel{255u, 255u, 255u, 250u},
    };
    const galaxy::gx::EfbCopyParams overflow = make_filter_params(
        {63u, 63u, 63u, 63u, 63u, 63u, 63u});
    passed &= expect_pixel(
        oracle_from_three_rows(white_rows, overflow, false, 6u),
        Pixel{221u, 221u, 221u, 200u},
        "integer oracle applies nine-bit copy-filter overflow");

    const std::array<Pixel, 3> red_rows{
        Pixel{0u, 0u, 0u, 1u},
        Pixel{255u, 0u, 0u, 77u},
        Pixel{0u, 0u, 0u, 2u},
    };
    const galaxy::gx::EfbCopyParams identity = make_filter_params(
        {0u, 0u, 21u, 22u, 21u, 0u, 0u});
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 8u),
        Pixel{82u, 82u, 82u, 82u},
        "integer oracle intensity R8 selects Y");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 9u),
        Pixel{90u, 90u, 90u, 90u},
        "integer oracle intensity G8 selects U");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 10u),
        Pixel{240u, 240u, 240u, 240u},
        "integer oracle intensity B8 selects V");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 11u),
        Pixel{82u, 82u, 82u, 90u},
        "integer oracle intensity RG8 selects Y/U");
    passed &= expect_pixel(
        oracle_from_three_rows(red_rows, identity, true, 12u),
        Pixel{90u, 90u, 90u, 240u},
        "integer oracle intensity GB8 selects U/V");

    const std::array<Pixel, 3> depth_rows{
        packed_depth(0x102030u),
        packed_depth(0x405060u),
        packed_depth(0x708090u),
    };
    passed &= expect_pixel(
        oracle_from_three_rows(depth_rows, make_filter_params(
            {1u, 2u, 4u, 8u, 16u, 17u, 16u}), false, 6u),
        Pixel{86u, 102u, 118u, 255u},
        "integer oracle filters packed depth bytes");
    return passed;
}

bool test_warp_against_oracle() {
    bool passed = true;
    try {
        WarpConversionHarness harness;
        const std::array<Pixel, 3> rows{
            Pixel{10u, 20u, 30u, 40u},
            Pixel{40u, 50u, 60u, 77u},
            Pixel{70u, 80u, 90u, 100u},
        };
        const std::array<Pixel, 3> white_rows{
            Pixel{255u, 255u, 255u, 100u},
            Pixel{255u, 255u, 255u, 200u},
            Pixel{255u, 255u, 255u, 250u},
        };
        const std::array<Pixel, 3> red_rows{
            Pixel{0u, 0u, 0u, 1u},
            Pixel{255u, 0u, 0u, 77u},
            Pixel{0u, 0u, 0u, 2u},
        };
        const std::array<std::uint32_t, 3> z24_rows{
            0x102030u,
            0x405060u,
            0x708090u,
        };
        const std::array<Pixel, 3> packed_depth_rows{
            packed_depth(z24_rows[0]),
            packed_depth(z24_rows[1]),
            packed_depth(z24_rows[2]),
        };

        for (const unsigned scale : {1u, 2u, 3u, 4u, 5u, 6u}) {
            for (unsigned clamp_case = 0u; clamp_case < 4u; ++clamp_case) {
                galaxy::gx::EfbCopyParams params = make_filter_params(
                    {1u, 2u, 4u, 8u, 16u, 17u, 16u});
                params.clamp_top = (clamp_case & 1u) != 0u;
                params.clamp_bottom = (clamp_case & 2u) != 0u;
                const Pixel expected =
                    oracle_from_three_rows(rows, params, false, 6u);
                char label[96]{};
                std::snprintf(
                    label,
                    sizeof(label),
                    "%ux color filter clamp_top=%u clamp_bottom=%u",
                    scale,
                    params.clamp_top ? 1u : 0u,
                    params.clamp_bottom ? 1u : 0u);
                passed &= expect_uniform_pixels(
                    harness.run_color(
                        rows,
                        scale,
                        constants_for_case(params, scale, false, false, 6u)),
                    scale,
                    expected,
                    label);
            }

            const galaxy::gx::EfbCopyParams overflow = make_filter_params(
                {63u, 63u, 63u, 63u, 63u, 63u, 63u});
            char overflow_label[64]{};
            std::snprintf(
                overflow_label,
                sizeof(overflow_label),
                "%ux coefficient overflow",
                scale);
            passed &= expect_uniform_pixels(
                harness.run_color(
                    white_rows,
                    scale,
                    constants_for_case(overflow, scale, false, false, 6u)),
                scale,
                oracle_from_three_rows(white_rows, overflow, false, 6u),
                overflow_label);

            const galaxy::gx::EfbCopyParams identity = make_filter_params(
                {0u, 0u, 21u, 22u, 21u, 0u, 0u});
            constexpr std::array<std::uint8_t, 5> kIntensityFormats{
                8u, 9u, 10u, 11u, 12u};
            for (const std::uint8_t format : kIntensityFormats) {
                char intensity_label[64]{};
                std::snprintf(
                    intensity_label,
                    sizeof(intensity_label),
                    "%ux intensity format %u",
                    scale,
                    static_cast<unsigned>(format));
                passed &= expect_uniform_pixels(
                    harness.run_color(
                        red_rows,
                        scale,
                        constants_for_case(identity, scale, false, true, format)),
                    scale,
                    oracle_from_three_rows(red_rows, identity, true, format),
                    intensity_label);
            }

            const galaxy::gx::EfbCopyParams depth_filter = make_filter_params(
                {1u, 2u, 4u, 8u, 16u, 17u, 16u});
            char depth_label[64]{};
            std::snprintf(
                depth_label,
                sizeof(depth_label),
                "%ux packed depth vertical filter",
                scale);
            passed &= expect_uniform_pixels(
                harness.run_depth(
                    z24_rows,
                    scale,
                    constants_for_case(depth_filter, scale, true, false, 6u)),
                scale,
                oracle_from_three_rows(
                    packed_depth_rows, depth_filter, false, 6u),
                depth_label);
        }

        // GXPeekZ reads one native EFB pixel. At scaled internal resolutions
        // that maps to one deterministic physical texel, never a bilinear
        // blend. Four logical blocks exercise both edges and exact z24
        // endpoints while keeping non-selected subpixels adversarial.
        constexpr std::array<std::uint32_t, 4> kPeekDepths{
            0x000000u,
            0x123456u,
            0x654321u,
            0xffffffu,
        };
        for (const unsigned scale : {1u, 2u, 3u, 4u, 5u, 6u}) {
            const unsigned width = 2u * scale;
            const unsigned height = 2u * scale;
            std::vector<std::uint32_t> depth_grid(
                static_cast<std::size_t>(width) * height,
                0x345678u);
            for (unsigned logical_y = 0u; logical_y < 2u; ++logical_y) {
                for (unsigned logical_x = 0u; logical_x < 2u; ++logical_x) {
                    const galaxy::gx::EfbPeekSamplingGeometry sampling =
                        galaxy::gx::compute_efb_peek_sampling_geometry(
                            static_cast<std::uint16_t>(logical_x),
                            static_cast<std::uint16_t>(logical_y),
                            scale);
                    const std::size_t index =
                        static_cast<std::size_t>(sampling.depth_texel_y) *
                            width +
                        sampling.depth_texel_x;
                    depth_grid[index] =
                        kPeekDepths[logical_y * 2u + logical_x];
                }
            }

            for (unsigned logical_y = 0u; logical_y < 2u; ++logical_y) {
                for (unsigned logical_x = 0u; logical_x < 2u; ++logical_x) {
                    const galaxy::gx::EfbPeekSamplingGeometry sampling =
                        galaxy::gx::compute_efb_peek_sampling_geometry(
                            static_cast<std::uint16_t>(logical_x),
                            static_cast<std::uint16_t>(logical_y),
                            scale);
                    const std::uint32_t expected_z24 =
                        kPeekDepths[logical_y * 2u + logical_x];
                    char label[128]{};
                    std::snprintf(
                        label,
                        sizeof(label),
                        "%ux point depth peek (%u,%u) preserves exact z24",
                        scale,
                        logical_x,
                        logical_y);
                    passed &= expect_pixel(
                        harness.run_depth_peek(
                            depth_grid, width, height, sampling),
                        packed_depth(expected_z24),
                        label);
                }
            }

            const auto field=harness.run_depth_field(depth_grid,width,height,scale,2u);
            for (std::size_t i=0; i<kPeekDepths.size(); ++i) {
                passed &= expect_pixel(field.at(i),packed_depth(kPeekDepths[i]),
                    "whole pointer depth field preserves exact GXPeekZ pixel and z24");
            }

            // Color peeks intentionally retain their complete NxN box
            // reduction. The center and corner deltas cancel only when every
            // physical subpixel participates in the reduction.
            constexpr Pixel kColorMean{64u, 96u, 128u, 160u};
            std::vector<Pixel> color_block(
                static_cast<std::size_t>(scale) * scale,
                kColorMean);
            if (scale > 1u) {
                const galaxy::gx::EfbPeekSamplingGeometry sampling =
                    galaxy::gx::compute_efb_peek_sampling_geometry(
                        0u, 0u, scale);
                color_block[0] = Pixel{32u, 64u, 96u, 128u};
                const std::size_t center =
                    static_cast<std::size_t>(sampling.depth_texel_y) *
                        scale +
                    sampling.depth_texel_x;
                color_block[center] = Pixel{96u, 128u, 160u, 192u};
            }
            const galaxy::gx::EfbPeekSamplingGeometry color_sampling =
                galaxy::gx::compute_efb_peek_sampling_geometry(
                    0u, 0u, scale);
            char color_label[96]{};
            std::snprintf(
                color_label,
                sizeof(color_label),
                "%ux color peek retains full logical-pixel box reduction",
                scale);
            passed &= expect_pixel(
                harness.run_color_peek(
                    color_block, scale, scale, color_sampling),
                kColorMean,
                color_label);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: WARP EFB conversion suite: %s\n", error.what());
        passed = false;
    }
    return passed;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--content-key-hash") == 0) {
        if (_putenv_s("GALAXY_GX_CONTENT_TEXTURE_CACHE", "1") != 0 ||
            _putenv_s("GALAXY_GX_DECODED_TEXTURE_BUDGET_MB", "0") != 0) {
            std::fprintf(stderr, "FAILED: configure content identity regression\n");
            return 1;
        }
        try {
            WarpConversionHarness harness;
            return harness.test_content_texture_identity() ? 0 : 1;
        } catch (const std::exception& error) {
            std::fprintf(stderr, "FAILED: content identity regression: %s\n", error.what());
            return 1;
        }
    }
    bool passed = test_cpu_oracle();
    passed &= test_direct_color_tile_decode();
    passed &= test_intensity_tile_decode();
    passed &= test_rgba8_tile_decode();
    passed &= test_generated_mip_uniform_alpha();
    passed &= test_cmpr_tile_decode();
    passed &= test_small_index_tile_decode();
    passed &= test_clear_precision();
    passed &= test_warp_against_oracle();
    passed &= test_gx_pixel_readbacks();
    try {
        WarpConversionHarness harness;
        passed &= harness.test_texture_descriptor_retirement();
        passed &= harness.test_xfb_retirement_rollback();
        passed &= harness.test_xfb_idle_pool_admission();
        passed &= harness.test_texture_cache_ranges();
        passed &= test_scaled_xfb_filter();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: WARP texture cache suite: %s\n", error.what());
        passed = false;
    }
    return passed ? 0 : 1;
}
