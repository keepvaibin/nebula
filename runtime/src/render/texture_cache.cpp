// GX texture decode from the images the game handed over into GPU textures. An unknown format is fatal.
// The SRV heap is not shader-visible; the renderer copies its slots into the frame's descriptor ring per draw.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "galaxy/render/texture_cache.h"
#include "galaxy/render/rt_materials.h"
#include "nebula_cooked_texture.h"

#include "galaxy/render/render_config.h"
#include "galaxy/windows_power.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace galaxy::render {

namespace {

static constexpr std::uint64_t kUploadArenaChunkBytes = 16ull << 20;

// Bound on the live decoded map's committed allocations (RGBA8 levels plus alignment, not source bytes).
// Frame-retired textures are outside it.
static constexpr std::uint64_t kDecodedResidentBudgetBytes = 64ull << 20;

// Bound on the recycled-texture pool (pool_retired_texture).
static constexpr std::uint64_t kTexturePoolCapBytes = 256ull << 20;

std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

bool trace_gx_stalls_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_GX_STALLS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

const TextureCache::MemoryRange* first_overlapping_dirty_range(
    std::span<const TextureCache::MemoryRange> ranges,
    std::uint64_t target_begin,
    std::uint64_t target_end) {
    if (ranges.empty() || target_begin >= target_end) {
        return nullptr;
    }

    auto it = std::lower_bound(
        ranges.begin(),
        ranges.end(),
        target_begin,
        [](const TextureCache::MemoryRange& range,
           std::uint64_t value) noexcept {
            return static_cast<std::uint64_t>(range.address) +
                       static_cast<std::uint64_t>(range.size) <=
                   value;
        });
    for (; it != ranges.end() && it->address < target_end; ++it) {
        if (it->size != 0u) return &*it;
    }
    return nullptr;
}

std::uint64_t read_env_u64(const char* name, std::uint64_t fallback) {
    char value[32]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), name) != 0 ||
        length <= 1u || length > sizeof(value)) {
        return fallback;
    }
    for (std::size_t i = 0; i + 1u < length; ++i) {
        if (value[i] < '0' || value[i] > '9') return fallback;
    }
    char* end = nullptr;
    errno = 0;
    const auto parsed = std::strtoull(value, &end, 10);
    return errno == ERANGE || end == value || *end != '\0'
        ? fallback : static_cast<std::uint64_t>(parsed);
}

std::uint64_t trace_gx_stall_threshold_us() {
    static const std::uint64_t threshold =
        read_env_u64("GALAXY_TRACE_GX_STALL_US", 1000u);
    return threshold;
}

[[nodiscard]] std::uint8_t efb_copy_alias_texture_format_impl(
    std::uint8_t format) {
    switch (format) {
    case 0u:
        return static_cast<std::uint8_t>(TexFormat::I4);
    case 1u:
    case 7u:
    case 8u:
    case 9u:
    case 10u:
        return static_cast<std::uint8_t>(TexFormat::I8);
    case 2u:
        return static_cast<std::uint8_t>(TexFormat::IA4);
    case 3u:
    case 11u:
    case 12u:
        return static_cast<std::uint8_t>(TexFormat::IA8);
    case 4u:
        return static_cast<std::uint8_t>(TexFormat::RGB565);
    case 5u:
        return static_cast<std::uint8_t>(TexFormat::RGB5A3);
    case 6u:
        return static_cast<std::uint8_t>(TexFormat::RGBA8);
    default:
        throw std::runtime_error(
            "[TextureCache] unsupported EFB copy alias format");
    }
}

std::uint64_t elapsed_us(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count());
}

// An image the game never handed over decodes from zeros (transparent): the renderer reads no game memory. The
// largest GX image (1024x1024 RGBA8 with every level) fits.
static const std::byte* missing_image_bytes(std::uint32_t size) {
    static const std::vector<std::byte> zeros(texture_source_bytes(TexFormat::RGBA8, 1024u, 1024u, 11u));
    if (size > zeros.size()) {
        throw std::runtime_error("[TextureCache] image larger than any GX image");
    }
    return zeros.data();
}

[[nodiscard]] static inline std::uint16_t read_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

[[nodiscard]] static unsigned full_mip_chain_levels(
    std::uint32_t width,
    std::uint32_t height) {
    unsigned levels = 1;
    while (((width >> levels) | (height >> levels)) != 0u) {
        ++levels;
    }
    return levels;
}

[[nodiscard]] static bool generated_mips_color_format(TexFormat format) {
    switch (format) {
    case TexFormat::RGB565:
    case TexFormat::RGB5A3:
    case TexFormat::RGBA8:
    case TexFormat::C4:
    case TexFormat::C8:
    case TexFormat::C14X2:
    case TexFormat::CMPR:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] static bool should_generate_native_mips(
    const TexImage& image,
    const TexMode& mode) {
    // Only repeating textures get a generated mip chain: a clamped texture is a decal placed by a texture matrix
    // (eyes, HUD digits) whose small detail blended lower levels would wash out.
    if ((static_cast<unsigned>(mode.min_filter) & 0x3u) != 0u ||
        mode.min_filter != TexMinFilter::Linear ||
        mode.mag_filter != TexMagFilter::Linear ||
        mode.wrap_s == TexWrap::Clamp || mode.wrap_t == TexWrap::Clamp ||
        (image.width < 64u && image.height < 64u) ||
        !generated_mips_color_format(image.format)) {
        return false;
    }
    // The settings (a lock and a copy) are read only once the guest descriptor allows generated mips.
    return get_render_config().enhanced_mipmaps;
}

}  // namespace

bool detail::flatten_texel_checker(std::vector<RGBA8>& pixels, std::uint32_t width, std::uint32_t height) {
    if (width < 4u || height < 4u || pixels.size() < static_cast<std::size_t>(width) * height) {
        return false;
    }
    const RGBA8 even = pixels[0];
    const RGBA8 odd = pixels[1];
    const auto same = [](RGBA8 a, RGBA8 b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
    if (same(even, odd)) {
        return false;
    }
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            if (!same(pixels[static_cast<std::size_t>(y) * width + x], ((x + y) & 1u) == 0u ? even : odd)) {
                return false;
            }
        }
    }
    const auto mean = [](std::uint8_t a, std::uint8_t b) { return static_cast<std::uint8_t>((a + b + 1u) >> 1u); };
    const RGBA8 flat{mean(even.r, odd.r), mean(even.g, odd.g), mean(even.b, odd.b), mean(even.a, odd.a)};
    std::fill_n(pixels.begin(), static_cast<std::size_t>(width) * height, flat);
    return true;
}

bool detail::remove_texel_checker(std::vector<RGBA8>& pixels, std::uint32_t width, std::uint32_t height) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    if (width < 4u || height < 4u || pixels.size() < count) {
        return false;
    }
    const auto at = [&](std::uint32_t x, std::uint32_t y) -> const RGBA8& {
        return pixels[static_cast<std::size_t>(y) * width + x];
    };
    // Four times a value against the sum of its four neighbours (clamped at the edges), signed by the checker.
    const auto checker_detail = [&](std::uint32_t x, std::uint32_t y, auto channel) {
        const std::int32_t sum = channel(at(x > 0u ? x - 1u : x, y)) + channel(at(x + 1u < width ? x + 1u : x, y)) +
                                 channel(at(x, y > 0u ? y - 1u : y)) + channel(at(x, y + 1u < height ? y + 1u : y));
        const std::int32_t d = 4 * channel(at(x, y)) - sum;
        return ((x + y) & 1u) == 0u ? d : -d;
    };
    // Luminance x 256 (Rec. 709 weights).
    const auto luma = [](const RGBA8& p) {
        return 54 * static_cast<std::int32_t>(p.r) + 183 * static_cast<std::int32_t>(p.g) + 19 * static_cast<std::int32_t>(p.b);
    };
    std::size_t strong = 0u;
    std::int64_t phase = 0;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            const std::int32_t d = checker_detail(x, y, luma);
            if (d >= 8 * 256 || d <= -8 * 256) {
                ++strong;
                phase += d > 0 ? 1 : -1;
            }
        }
    }
    if (strong * 5u < count || static_cast<std::size_t>(phase < 0 ? -phase : phase) * 100u < strong * 95u) {
        return false;
    }
    std::vector<std::int32_t> detail(count);
    std::vector<RGBA8> out(pixels.begin(), pixels.begin() + static_cast<std::ptrdiff_t>(count));
    const auto remove = [&](auto channel, auto store) {
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                detail[static_cast<std::size_t>(y) * width + x] = checker_detail(x, y, channel);
            }
        }
        for (std::uint32_t y = 0; y < height; ++y) {
            for (std::uint32_t x = 0; x < width; ++x) {
                std::int32_t sum = 0;
                for (std::uint32_t sy = y > 0u ? y - 1u : y; sy <= std::min(y + 1u, height - 1u); ++sy) {
                    for (std::uint32_t sx = x > 0u ? x - 1u : x; sx <= std::min(x + 1u, width - 1u); ++sx) {
                        sum += detail[static_cast<std::size_t>(sy) * width + sx];
                    }
                }
                // The checker's amplitude: the mean signed detail (four times twice the amplitude) over the 3x3 texels
                // within the level.
                const std::int32_t taps = static_cast<std::int32_t>((std::min(x + 1u, width - 1u) - (x > 0u ? x - 1u : x) + 1u) *
                                                                    (std::min(y + 1u, height - 1u) - (y > 0u ? y - 1u : y) + 1u));
                const std::int32_t scale = 8 * taps;
                const std::int32_t amplitude = (sum >= 0 ? sum + scale / 2 : sum - scale / 2) / scale;
                const std::int32_t value = channel(at(x, y)) - (((x + y) & 1u) == 0u ? amplitude : -amplitude);
                store(out[static_cast<std::size_t>(y) * width + x], static_cast<std::uint8_t>(std::clamp(value, 0, 255)));
            }
        }
    };
    remove([](const RGBA8& p) { return static_cast<std::int32_t>(p.r); }, [](RGBA8& p, std::uint8_t v) { p.r = v; });
    remove([](const RGBA8& p) { return static_cast<std::int32_t>(p.g); }, [](RGBA8& p, std::uint8_t v) { p.g = v; });
    remove([](const RGBA8& p) { return static_cast<std::int32_t>(p.b); }, [](RGBA8& p, std::uint8_t v) { p.b = v; });
    std::copy(out.begin(), out.end(), pixels.begin());
    return true;
}

void detail::generate_rgba8_mip_level(
    const std::vector<RGBA8>& source,
    std::uint32_t source_width,
    std::uint32_t source_height,
    std::vector<RGBA8>& dest,
    std::uint32_t dest_width,
    std::uint32_t dest_height) {
    dest.resize(
        static_cast<std::size_t>(dest_width) *
        static_cast<std::size_t>(dest_height));
    for (std::uint32_t y = 0; y < dest_height; ++y) {
        for (std::uint32_t x = 0; x < dest_width; ++x) {
            const std::uint32_t src_x = x * 2u;
            const std::uint32_t src_y = y * 2u;
            if (src_x + 1u < source_width && src_y + 1u < source_height) {
                const std::size_t top = static_cast<std::size_t>(src_y) * source_width + src_x;
                const RGBA8& p0 = source[top];
                const RGBA8& p1 = source[top + 1u];
                const RGBA8& p2 = source[top + source_width];
                const RGBA8& p3 = source[top + source_width + 1u];
                if (((p0.a ^ p1.a) | (p0.a ^ p2.a) | (p0.a ^ p3.a)) == 0u) {
                    // Equal nonzero alpha cancels from (alpha * sum + 2 * alpha) / (4 * alpha).
                    // Fully transparent input keeps zero RGB.
                    RGBA8 pixel{};
                    if (p0.a != 0u) {
                        pixel = {
                            static_cast<std::uint8_t>((p0.r + p1.r + p2.r + p3.r + 2u) >> 2u),
                            static_cast<std::uint8_t>((p0.g + p1.g + p2.g + p3.g + 2u) >> 2u),
                            static_cast<std::uint8_t>((p0.b + p1.b + p2.b + p3.b + 2u) >> 2u),
                            p0.a};
                    }
                    dest[static_cast<std::size_t>(y) * dest_width + x] = pixel;
                    continue;
                }
            }
            std::uint32_t premul_r = 0;
            std::uint32_t premul_g = 0;
            std::uint32_t premul_b = 0;
            std::uint32_t a = 0;
            std::uint32_t count = 0;
            for (std::uint32_t dy = 0; dy < 2u; ++dy) {
                const std::uint32_t sample_y = src_y + dy;
                if (sample_y >= source_height) {
                    continue;
                }
                for (std::uint32_t dx = 0; dx < 2u; ++dx) {
                    const std::uint32_t sample_x = src_x + dx;
                    if (sample_x >= source_width) {
                        continue;
                    }
                    const RGBA8& px =
                        source[static_cast<std::size_t>(sample_y) *
                               source_width + sample_x];
                    premul_r += static_cast<std::uint32_t>(px.r) * px.a;
                    premul_g += static_cast<std::uint32_t>(px.g) * px.a;
                    premul_b += static_cast<std::uint32_t>(px.b) * px.a;
                    a += px.a;
                    ++count;
                }
            }
            if (count == 0u) {
                count = 1u;
            }
            const std::uint32_t avg_a = (a + count / 2u) / count;
            const std::uint32_t color_divisor = a == 0u ? count : a;
            dest[static_cast<std::size_t>(y) * dest_width + x] = RGBA8{
                static_cast<std::uint8_t>(
                    (premul_r + color_divisor / 2u) / color_divisor),
                static_cast<std::uint8_t>(
                    (premul_g + color_divisor / 2u) / color_divisor),
                static_cast<std::uint8_t>(
                    (premul_b + color_divisor / 2u) / color_divisor),
                static_cast<std::uint8_t>(avg_a),
            };
        }
    }
}

namespace {

// RGB5A3: a 16-bit big-endian texel, opaque 5:5:5 or 3-bit alpha 4:4:4, widened by bit replication.
[[nodiscard]] static RGBA8 decode_rgb5a3(const std::uint8_t* texel) {
    const std::uint16_t v = read_be16(texel);
    const auto five = [](std::uint32_t c) { return static_cast<std::uint8_t>((c << 3) | (c >> 2)); };
    const auto four = [](std::uint32_t c) { return static_cast<std::uint8_t>((c << 4) | c); };
    if ((v & 0x8000u) != 0u) {
        return RGBA8{five((v >> 10) & 0x1Fu), five((v >> 5) & 0x1Fu), five(v & 0x1Fu), 0xFFu};
    }
    const std::uint32_t a3 = (v >> 12) & 0x7u;
    return RGBA8{four((v >> 8) & 0xFu), four((v >> 4) & 0xFu), four(v & 0xFu),
                 static_cast<std::uint8_t>((a3 << 5) | (a3 << 2) | (a3 >> 1))};
}

}  // namespace

// The console formats some images still reach the renderer in (RVLFaceLib's parts, NebulaIcons): I4 (8x8 tiles of
// 4-bit intensity), IA4 (8x4 tiles of 4-bit alpha and intensity) and RGB5A3 (4x4 tiles of 16-bit texels), each tile
// 32 bytes in raster order. Clipped edge tiles still advance by their whole 32 bytes.
void detail::decode_console_tiles(TexFormat format, const std::uint8_t* src, std::uint32_t width,
                                  std::uint32_t height, RGBA8* out) {
    if (format != TexFormat::I4 && format != TexFormat::IA4 && format != TexFormat::RGB5A3) {
        throw std::runtime_error("[TextureCache] a console format no image arrives in");
    }
    const std::uint32_t tile_width = format == TexFormat::RGB5A3 ? 4u : 8u;
    const std::uint32_t tile_height = format == TexFormat::I4 ? 8u : 4u;
    for (std::uint32_t ty = 0; ty < (height + tile_height - 1u) / tile_height; ++ty) {
        for (std::uint32_t tx = 0; tx < (width + tile_width - 1u) / tile_width; ++tx) {
            for (std::uint32_t py = 0; py < tile_height; ++py) {
                for (std::uint32_t px = 0; px < tile_width; ++px) {
                    const std::uint32_t x = tx * tile_width + px;
                    const std::uint32_t y = ty * tile_height + py;
                    if (x >= width || y >= height) {
                        continue;
                    }
                    const std::uint32_t i = py * tile_width + px;
                    RGBA8& texel = out[static_cast<std::size_t>(y) * width + x];
                    if (format == TexFormat::I4) {
                        const auto v = static_cast<std::uint8_t>(((src[i / 2u] >> ((i & 1u) == 0u ? 4u : 0u)) & 0xFu) * 17u);
                        texel = RGBA8{v, v, v, v};
                    } else if (format == TexFormat::IA4) {
                        const auto v = static_cast<std::uint8_t>((src[i] & 0xFu) * 17u);
                        texel = RGBA8{v, v, v, static_cast<std::uint8_t>((src[i] >> 4u) * 17u)};
                    } else {
                        texel = decode_rgb5a3(src + i * 2u);
                    }
                }
            }
            src += 32u;
        }
    }
}

namespace {

// Guest byte size of one level, in whole blocks.
[[nodiscard]] static std::uint32_t tex_data_size(
    TexFormat fmt,
    std::uint32_t width,
    std::uint32_t height) {
    auto blocks = [](std::uint32_t dim, std::uint32_t block) {
        return (dim + block - 1u) / block;
    };
    switch (fmt) {
    case TexFormat::I4:
        return blocks(width, 8u) * blocks(height, 8u) * 32u;
    case TexFormat::I8:
    case TexFormat::IA4:
        return blocks(width, 8u) * blocks(height, 4u) * 32u;
    case TexFormat::IA8:
    case TexFormat::RGB565:
    case TexFormat::RGB5A3:
        return blocks(width, 4u) * blocks(height, 4u) * 32u;
    case TexFormat::RGBA8:
        return blocks(width, 4u) * blocks(height, 4u) * 64u;
    case TexFormat::CMPR:
        // Each 8x8 macro-block = 4 DXT1 sub-blocks x 8 bytes = 32 bytes.
        return blocks(width, 8u) * blocks(height, 8u) * 32u;
    default:
        throw std::runtime_error("[TextureCache] tex_data_size: unknown TexFormat");
    }
}

[[nodiscard]] static bool texture_decode_format_supported(TexFormat format) {
    return format == TexFormat::I4 || format == TexFormat::IA4 || format == TexFormat::RGB5A3;
}

// Decodes every guest-authored level (contiguous, block-aligned from `src`)
// and generates any enhancement levels from the decoded level above. Pure:
// reads only `src`, writes only `level_pixels`. Runs on the render
// thread or a TextureDecodePool worker.
static void decode_texture_levels(
    TexFormat format,
    const std::uint8_t* src,
    std::uint32_t w,
    std::uint32_t h,
    unsigned source_levels,
    unsigned levels,
    bool generated_mips,
    std::vector<std::vector<RGBA8>>& level_pixels) {
    for (unsigned l = 0; l < source_levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, w >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, h >> l);
        level_pixels[l].resize(
            static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh));
        RGBA8* out = level_pixels[l].data();

        if (texture_decode_format_supported(format)) {
            detail::decode_console_tiles(format, src, lw, lh, out);
        } else {
            // Bytes in a format every image of which arrives as a record (nebula_cooked_texture.h): none to draw.
            std::fill_n(out, static_cast<std::size_t>(lw) * lh, RGBA8{});
        }
        if (!detail::flatten_texel_checker(level_pixels[l], lw, lh)) {
            detail::remove_texel_checker(level_pixels[l], lw, lh);
        }
        src += tex_data_size(format, lw, lh);
    }
    if (generated_mips) {
        for (unsigned l = 1; l < levels; ++l) {
            const std::uint32_t src_w =
                std::max<std::uint32_t>(1u, w >> (l - 1u));
            const std::uint32_t src_h =
                std::max<std::uint32_t>(1u, h >> (l - 1u));
            const std::uint32_t dst_w = std::max<std::uint32_t>(1u, w >> l);
            const std::uint32_t dst_h = std::max<std::uint32_t>(1u, h >> l);
            detail::generate_rgba8_mip_level(
                level_pixels[l - 1u],
                src_w,
                src_h,
                level_pixels[l],
                dst_w,
                dst_h);
        }
    }
}

}  // namespace

void detail::expand_record_level(std::uint8_t layout, const std::uint8_t* src, std::uint32_t width, std::uint32_t height,
                                 RGBA8* out) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    switch (layout) {
    case NEBULA_TEXTURE_L8:
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = RGBA8{src[i], src[i], src[i], src[i]};
        }
        break;
    case NEBULA_TEXTURE_LA8:
        for (std::size_t i = 0; i < count; ++i) {
            out[i] = RGBA8{src[i * 2u], src[i * 2u], src[i * 2u], src[i * 2u + 1u]};
        }
        break;
    case NEBULA_TEXTURE_BLOCK4: {
        const auto widen = [](std::uint16_t v) {
            const auto r = static_cast<std::uint8_t>((v >> 11) & 0x1Fu);
            const auto g = static_cast<std::uint8_t>((v >> 5) & 0x3Fu);
            const auto b = static_cast<std::uint8_t>(v & 0x1Fu);
            return RGBA8{static_cast<std::uint8_t>((r << 3) | (r >> 2)), static_cast<std::uint8_t>((g << 2) | (g >> 4)),
                         static_cast<std::uint8_t>((b << 3) | (b >> 2)), 0xFFu};
        };
        const std::uint32_t blocks_x = (width + 3u) / 4u;
        for (std::uint32_t by = 0; by < (height + 3u) / 4u; ++by) {
            for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
                const std::uint8_t* block = src + (static_cast<std::size_t>(by) * blocks_x + bx) * 8u;
                const auto c0 = static_cast<std::uint16_t>(block[0] | block[1] << 8);
                const auto c1 = static_cast<std::uint16_t>(block[2] | block[3] << 8);
                const RGBA8 a = widen(c0);
                const RGBA8 b = widen(c1);
                RGBA8 entries[4] = {a, b, {}, {}};
                if (c0 > c1) {
                    entries[2] = {cmpr_interpolated_channel(a.r, b.r), cmpr_interpolated_channel(a.g, b.g),
                                  cmpr_interpolated_channel(a.b, b.b), 0xFFu};
                    entries[3] = {cmpr_interpolated_channel(b.r, a.r), cmpr_interpolated_channel(b.g, a.g),
                                  cmpr_interpolated_channel(b.b, a.b), 0xFFu};
                } else {
                    entries[2] = {static_cast<std::uint8_t>((a.r + b.r) / 2u), static_cast<std::uint8_t>((a.g + b.g) / 2u),
                                  static_cast<std::uint8_t>((a.b + b.b) / 2u), 0xFFu};
                    entries[3] = entries[2];
                    entries[3].a = 0u;
                }
                const std::uint32_t indices = static_cast<std::uint32_t>(block[4]) | block[5] << 8 | block[6] << 16 |
                                              static_cast<std::uint32_t>(block[7]) << 24;
                for (std::uint32_t y = 0; y < 4u && by * 4u + y < height; ++y) {
                    for (std::uint32_t x = 0; x < 4u && bx * 4u + x < width; ++x) {
                        out[static_cast<std::size_t>(by * 4u + y) * width + bx * 4u + x] =
                            entries[(indices >> (2u * (y * 4u + x))) & 3u];
                    }
                }
            }
        }
        break;
    }
    default:
        std::memcpy(out, src, count * sizeof(RGBA8));
        break;
    }
}

namespace {

// A texture record's levels (nebula_cooked_texture.h) as RGBA8: the first `source_levels` it holds, widened from
// its layout, through the dither-checker filters when it asks for them; then any generated levels, as for a decoded
// image. Pure, like decode_texture_levels.
static void expand_record_levels(
    const NebulaTexture* record,
    unsigned source_levels,
    unsigned levels,
    bool generated_mips,
    std::vector<std::vector<RGBA8>>& level_pixels) {
    const std::uint32_t w = record->width;
    const std::uint32_t h = record->height;
    const auto* src = reinterpret_cast<const std::uint8_t*>(record) + NEBULA_TEXTURE_HEADER;
    for (unsigned l = 0; l < source_levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, w >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, h >> l);
        const std::size_t count = static_cast<std::size_t>(lw) * lh;
        level_pixels[l].resize(count);
        detail::expand_record_level(record->layout, src, lw, lh, level_pixels[l].data());
        if ((record->flags & NEBULA_TEXTURE_FILTER_AT_UPLOAD) != 0u &&
            !detail::flatten_texel_checker(level_pixels[l], lw, lh)) {
            detail::remove_texel_checker(level_pixels[l], lw, lh);
        }
        src += nebula_texture_level_bytes(lw, lh, 1u, record->layout);
    }
    if (generated_mips) {
        for (unsigned l = 1; l < levels; ++l) {
            detail::generate_rgba8_mip_level(
                level_pixels[l - 1u],
                std::max<std::uint32_t>(1u, w >> (l - 1u)),
                std::max<std::uint32_t>(1u, h >> (l - 1u)),
                level_pixels[l],
                std::max<std::uint32_t>(1u, w >> l),
                std::max<std::uint32_t>(1u, h >> l));
        }
    }
}

// Copies decoded levels into their placed footprints in mapped staging memory.
static void write_staging_levels(
    std::uint8_t* dst_base,
    std::uint32_t w,
    std::uint32_t h,
    unsigned levels,
    const std::uint64_t* level_offset,
    const std::uint32_t* level_pitch,
    const std::vector<std::vector<RGBA8>>& level_pixels) {
    for (unsigned l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, w >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, h >> l);
        const std::size_t row_bytes =
            static_cast<std::size_t>(lw) * sizeof(RGBA8);
        const auto* src_pixels = reinterpret_cast<const std::uint8_t*>(
            level_pixels[l].data());
        auto* dst_level = dst_base + level_offset[l];
        if (static_cast<std::size_t>(level_pitch[l]) == row_bytes) {
            std::memcpy(
                dst_level,
                src_pixels,
                row_bytes * static_cast<std::size_t>(lh));
            continue;
        }
        for (std::uint32_t row = 0; row < lh; ++row) {
            std::memcpy(
                dst_level + static_cast<std::size_t>(row) * level_pitch[l],
                src_pixels + static_cast<std::size_t>(row) * row_bytes,
                row_bytes);
        }
    }
}

} // anonymous namespace

std::uint8_t efb_copy_alias_texture_format(std::uint8_t copy_format) {
    return efb_copy_alias_texture_format_impl(copy_format);
}

std::uint32_t texture_source_bytes(
    TexFormat format, std::uint32_t width, std::uint32_t height,
    unsigned source_levels) {
    if (width == 0u || height == 0u || width > 1024u || height > 1024u ||
        source_levels == 0u || source_levels > full_mip_chain_levels(width, height)) {
        throw std::runtime_error("[TextureCache] invalid game texture extent");
    }
    std::uint64_t total = 0u;
    for (unsigned level = 0; level < source_levels; ++level) {
        total += tex_data_size(format, std::max(1u, width >> level),
                               std::max(1u, height >> level));
    }
    if (total > UINT32_MAX) {
        throw std::runtime_error("[TextureCache] game texture footprint overflow");
    }
    return static_cast<std::uint32_t>(total);
}

std::uint32_t efb_copy_source_bytes(const EfbCopyParams& params) {
    if (params.copy_to_xfb || params.src_width == 0u || params.src_height == 0u ||
        params.src_width > 1024u || params.src_height > 1024u) {
        throw std::runtime_error("[TextureCache] invalid texture-copy extent");
    }
    const std::uint32_t width = params.half_scale
        ? std::max(1u, static_cast<unsigned>(params.src_width) / 2u) : params.src_width;
    const std::uint32_t height = params.half_scale
        ? std::max(1u, static_cast<unsigned>(params.src_height) / 2u) : params.src_height;
    const auto format = static_cast<TexFormat>(efb_copy_alias_texture_format(params.target_format));
    const unsigned block_height = format == TexFormat::I4 ? 8u : 4u;
    const std::uint32_t row_bytes = texture_source_bytes(format, width, 1u);
    const std::uint32_t rows = (height + block_height - 1u) / block_height;
    const std::uint64_t span = row_bytes +
        static_cast<std::uint64_t>(rows - 1u) * params.dest_stride;
    if (span > UINT32_MAX) {
        throw std::runtime_error("[TextureCache] texture-copy footprint overflow");
    }
    return static_cast<std::uint32_t>(span);
}

bool TextureCache::initialize(gpu::Device* device) {
    next_srv_index_ = 0u;
    free_srv_indices_.clear();
    retired_srv_indices_.reset();
    texture_pool_.clear();
    texture_pool_bytes_ = 0;
    return backend_.initialize(device, kSrvHeapCapacity);
}

void TextureCache::finish_pending_decodes() {
    if (!async_decode_pending_ || !decode_pool_) {
        replaced_images_.clear();
        return;
    }
    async_decode_pending_ = false;
    decode_pool_->wait_all();
    replaced_images_.clear();
}

void TextureCache::register_image(std::uint64_t address, std::shared_ptr<const std::vector<std::byte>> bytes) {
    if (bytes == nullptr || bytes->empty()) {
        return;
    }
    const auto size = static_cast<std::uint32_t>(bytes->size());
    (void)invalidate_range(address, size);
    auto& slot = registered_images_[address];
    if (slot != nullptr) {
        replaced_images_.push_back(std::move(slot));
    }
    slot = std::move(bytes);
}

void TextureCache::drop_registered_images(std::span<const MemoryRange> ranges) {
    for (const MemoryRange& range : ranges) {
        const std::uint64_t begin = range.address, end = begin + range.size;
        auto it = registered_images_.lower_bound(end);
        while (it != registered_images_.begin()) {
            --it;
            if (it->first + it->second->size() <= begin) {
                break;
            }
            static const bool log_drops = read_env_u64("GALAXY_LOG_GUEST_TEXTURES", 0u) != 0u;
            if (log_drops) {
                std::fprintf(stderr, "[texture] handed-over image dropped: %llX (%zu bytes) by a write at %08llX+%llu\n", static_cast<unsigned long long>(it->first),
                             it->second->size(), static_cast<unsigned long long>(begin),
                             static_cast<unsigned long long>(end - begin));
            }
            replaced_images_.push_back(std::move(it->second));
            it = registered_images_.erase(it);
        }
    }
}

const std::byte* TextureCache::registered_bytes(std::uint64_t address, std::uint32_t size) const {
    // The image whose bytes start at or before the address and cover the whole range.
    auto it = registered_images_.upper_bound(address);
    if (it == registered_images_.begin()) {
        return nullptr;
    }
    --it;
    const std::uint64_t offset = address - it->first;
    if (offset + size > it->second->size()) {
        return nullptr;
    }
    return it->second->data() + offset;
}

const NebulaTexture* TextureCache::registered_record(std::uint64_t address) const {
    const auto it = registered_images_.find(address);
    if (it == registered_images_.end()) {
        return nullptr;
    }
    const std::vector<std::byte>& bytes = *it->second;
    const NebulaTexture* record = bytes.size() >= NEBULA_TEXTURE_HEADER ? nebula_texture_record(bytes.data()) : nullptr;
    if (record == nullptr || record->size != bytes.size() || record->levels == 0u ||
        record->levels > NEBULA_TEXTURE_MAX_LEVELS || record->width == 0u || record->height == 0u ||
        record->layout > NEBULA_TEXTURE_BLOCK4 ||
        NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(record->width, record->height, record->levels,
                                                          record->layout) > bytes.size()) {
        return nullptr;
    }
    return record;
}

void TextureCache::finish_pending_decodes_noexcept() noexcept {
    if (!async_decode_pending_ || !decode_pool_) {
        return;
    }
    async_decode_pending_ = false;
    (void)decode_pool_->wait_all_noexcept();
}

void TextureCache::begin_frame(
    unsigned frame_slot,
    unsigned frames_in_flight) {
    // Staging memory of the slot being rewound must not still be written.
    finish_pending_decodes();
    if (frames_in_flight == 0u || frame_slot >= frames_in_flight) {
        throw std::runtime_error("[TextureCache] invalid frame slot");
    }
    if (retired_entries_.empty()) {
        retired_entries_.resize(frames_in_flight);
    } else if (retired_entries_.size() != frames_in_flight) {
        throw std::runtime_error("[TextureCache] frame count changed");
    }
    if (upload_arenas_.empty()) {
        upload_arenas_.resize(frames_in_flight);
    } else if (upload_arenas_.size() != frames_in_flight) {
        throw std::runtime_error("[TextureCache] upload arena frame count changed");
    }

    // The renderer has waited for this slot's fence, so everything retired in its previous use is GPU-idle.
    // Descriptors retire with the frame too: a handle chosen earlier in a frame can still be copied into a
    // table after a later lookup evicts it.
    auto& retired = retired_entries_[frame_slot];
    // Reserve before publishing any slots, so allocation failure cannot leave
    // a partially reclaimed list that would publish duplicate slots on retry.
    free_srv_indices_.reserve(free_srv_indices_.size() + retired.size());
    std::vector<Entry>* held = hold_segment_ < kMaxHoldSegments
        ? &held_entries_[hold_segment_] : nullptr;
    if (held != nullptr) [[unlikely]] {
        held->reserve(held->size() + retired.size());
    }
    for (Entry& entry : retired) {
        free_srv_indices_.push_back(entry.handle.srv_index);
        if (held != nullptr) [[unlikely]] {
            held->push_back(std::move(entry));
            ++held_texture_count_;
        } else {
            pool_retired_texture(entry);
        }
    }
    retired.clear();
    upload_arenas_[frame_slot].active_chunk = 0;
    upload_arenas_[frame_slot].cursor = 0;
    current_frame_slot_ = frame_slot;
    if (pack_staging_.size() != frames_in_flight) {
        pack_staging_.resize(frames_in_flight);
    }
    for (TextureBackend::UploadChunk& chunk : pack_staging_[frame_slot]) {
        TextureBackend::release_upload_chunk(chunk);
    }
    pack_staging_[frame_slot].clear();
    if (movie_staging_.size() != frames_in_flight) {
        movie_staging_.resize(frames_in_flight);
    }
    movie_staging_[frame_slot].clear();
    packs_.begin_frame();
}

void TextureCache::release_held_entries(std::vector<Entry>& held) {
    for (Entry& entry : held) {
        pool_retired_texture(entry);
    }
    held.clear();
}

void TextureCache::set_retirement_hold_segment(unsigned segment) {
    if (segment == hold_segment_) {
        return;
    }
    if (segment >= kMaxHoldSegments) {
        // Interpolation ended: the renderer drained the GPU when its rings
        // left the per-game-frame mode.
        for (auto& held : held_entries_) {
            release_held_entries(held);
        }
        hold_segment_ = kNoHoldSegment;
        return;
    }
    // The renderer waited for this segment's fence before reusing it: every
    // replay that could name these textures has completed.
    release_held_entries(held_entries_[segment]);
    hold_segment_ = segment;
}

bool TextureCache::preallocate_upload_arenas(unsigned frames_in_flight) {
    if (!backend_.ready() || frames_in_flight == 0u) {
        return false;
    }
    try {
        if (upload_arenas_.empty()) {
            upload_arenas_.resize(frames_in_flight);
        } else if (upload_arenas_.size() != frames_in_flight) {
            release_upload_arenas();
            upload_arenas_.clear();
            upload_arenas_.resize(frames_in_flight);
        }
        for (UploadArenaFrame& frame : upload_arenas_) {
            if (frame.chunks.empty()) {
                append_upload_arena_chunk(frame, kUploadArenaChunkBytes);
            }
            frame.active_chunk = 0;
            frame.cursor = 0;
        }
        current_frame_slot_ = 0;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[TextureCache] upload arena preallocation failed: "
                  << e.what() << '\n';
        release_upload_arenas();
        upload_arenas_.clear();
        return false;
    }
}

TextureCache::DecodedMapStats TextureCache::decoded_map_stats() const noexcept {
    DecodedMapStats stats;
    stats.resident_bytes = decoded_resident_bytes_;
    stats.budget_bytes = kDecodedResidentBudgetBytes;
    stats.evicted_bytes = decoded_evicted_bytes_;
    stats.evicted_entries = decoded_evicted_entries_;
    stats.entries = entries_.size();
    stats.lru_entries = decoded_lru_.size();
    return stats;
}

void TextureCache::touch_cached_handle(const TextureHandle& handle) {
    if (handle.resource == nullptr || handle.from_efb_copy ||
        handle.srv_index >= decoded_srv_keys_.size()) return;
    const auto& key = decoded_srv_keys_[handle.srv_index];
    if (!key) return;
    const auto entry = entries_.find(*key);
    if (entry == entries_.end() ||
        entry->second.handle.resource != handle.resource ||
        entry->second.handle.srv_index != handle.srv_index) return;
    decoded_lru_touch(*key);
}

void TextureCache::decoded_lru_touch(const Key& key) {
    const auto pos = decoded_lru_pos_.find(key);
    if (pos == decoded_lru_pos_.end()) {
        return;
    }
    decoded_lru_.splice(decoded_lru_.begin(), decoded_lru_, pos->second);
    pos->second = decoded_lru_.begin();
}

void TextureCache::decoded_lru_forget(const Key& key) {
    const auto pos = decoded_lru_pos_.find(key);
    if (pos == decoded_lru_pos_.end()) {
        return;
    }
    decoded_lru_.erase(pos->second);
    decoded_lru_pos_.erase(pos);
}

void TextureCache::decoded_lru_release(const Key& key, Entry& entry) {
    // Every entries_ removal goes through here: a resident total that drifts high would evict textures that are
    // within budget.
    decoded_resident_bytes_ -= std::min<std::uint64_t>(
        decoded_resident_bytes_, entry.allocation_bytes);
    decoded_source_bytes_ -= std::min<std::uint64_t>(
        decoded_source_bytes_, entry.handle.source_bytes);
    if (entry.handle.srv_index < decoded_srv_keys_.size()) {
        auto& indexed_key = decoded_srv_keys_[entry.handle.srv_index];
        if (indexed_key && *indexed_key == key) indexed_key.reset();
    }
    decoded_lru_forget(key);
}

void TextureCache::decoded_lru_track(const Key& key) {
    const auto entry = entries_.find(key);
    if (entry == entries_.end()) {
        throw std::logic_error("[TextureCache] tracking absent decoded entry");
    }
    if (decoded_lru_pos_.find(key) != decoded_lru_pos_.end()) {
        decoded_lru_touch(key);
        return;
    }
    decoded_lru_.push_front(key);
    try {
        decoded_lru_pos_.emplace(key, decoded_lru_.begin());
    } catch (...) {
        decoded_lru_.pop_front();
        throw;
    }
    const auto index = entry->second.handle.srv_index;
    if (index < decoded_srv_keys_.size()) decoded_srv_keys_[index] = key;
    decoded_resident_bytes_ += entry->second.allocation_bytes;
    decoded_source_bytes_ += entry->second.handle.source_bytes;
    decoded_lru_evict_to_budget();
}

void TextureCache::decoded_lru_evict_to_budget() {
    constexpr std::uint64_t budget = kDecodedResidentBudgetBytes;
    if (decoded_resident_bytes_ <= budget) {
        return;
    }
    // Victims are retired, not destroyed: retire() parks the resource and its SRV slot in the current frame slot's
    // list until that slot's fence has been waited on, so submitted command lists stay valid. Live plus in-flight
    // retired descriptors must fit kSrvHeapCapacity.
    while (!decoded_lru_.empty() &&
           decoded_resident_bytes_ > budget) {
        const Key victim = decoded_lru_.back();
        const auto it = entries_.find(victim);
        if (it == entries_.end()) {
            decoded_lru_.pop_back();
            decoded_lru_pos_.erase(victim);
            continue;
        }
        const std::uint64_t victim_bytes = it->second.allocation_bytes;
        decoded_evicted_bytes_ += victim_bytes;
        ++decoded_evicted_entries_;
        // decoded_envelope_ only widens: an evicted range keeps it conservative (a scan may see a false
        // "may overlap", never a stale texture) until it is rebuilt when entries_ empties.
        decoded_lru_release(victim, it->second);
        retire(std::move(it->second));
        entries_.erase(it);
    }
}

// Called from begin_frame for a slot whose fence has been waited on: the
// texture is GPU-idle and still in the shader-resource state.
void TextureCache::pool_retired_texture(Entry& entry) {
    if (!entry.texture || entry.handle.from_efb_copy || entry.allocation_bytes == 0u) {
        return;
    }
    if (texture_pool_bytes_ + entry.allocation_bytes > kTexturePoolCapBytes) {
        return;
    }
    std::uint32_t width = 0, height = 0, levels = 0;
    if (!TextureBackend::poolable(gpu::raw(entry.texture), width, height, levels)) {
        return;
    }
    texture_pool_.push_back(PooledTexture{
        std::move(entry.texture),
        width,
        height,
        levels,
        entry.allocation_bytes});
    texture_pool_bytes_ += entry.allocation_bytes;
}

bool TextureCache::take_pooled_texture(
    std::uint32_t width, std::uint32_t height, unsigned levels,
    gpu::TextureRef& texture, std::uint64_t& allocation_bytes) {
    for (std::size_t i = 0; i < texture_pool_.size(); ++i) {
        PooledTexture& pooled = texture_pool_[i];
        if (pooled.width != width || pooled.height != height || pooled.levels != levels) {
            continue;
        }
        texture = std::move(pooled.texture);
        allocation_bytes = pooled.allocation_bytes;
        texture_pool_bytes_ -= pooled.allocation_bytes;
        texture_pool_[i] = std::move(texture_pool_.back());
        texture_pool_.pop_back();
        return true;
    }
    return false;
}

void TextureCache::retire(Entry&& entry) {
    if (entry.pack_slot != TexturePacks::kNone) {
        packs_.remove_user(entry.pack_slot);
        entry.pack_slot = TexturePacks::kNone;
    }
    if (!entry.texture) {
        return;
    }
    if (retired_entries_.empty()) {
        throw std::runtime_error(
            "[TextureCache] resource retired before begin_frame");
    }
    // Allocate before transferring ownership; retain both the resource and its
    // CPU descriptor until begin_frame has waited for this slot's fence.
    auto& retired = retired_entries_[current_frame_slot_];
    retired.emplace_back();
    retired.back() = std::move(entry);
    retired_srv_indices_.set(retired.back().handle.srv_index);
    ++retirement_revision_;
}

void TextureCache::release_upload_arenas() {
    for (UploadArenaFrame& frame : upload_arenas_) {
        for (TextureBackend::UploadChunk& chunk : frame.chunks) {
            TextureBackend::release_upload_chunk(chunk);
        }
    }
}

void TextureCache::shutdown() {
    finish_pending_decodes_noexcept();
    decode_pool_.reset();
    packs_.shutdown();
    pack_waiters_.clear();
    pack_uploads_.clear();
    for (auto& slot : pack_staging_) {
        for (TextureBackend::UploadChunk& chunk : slot) TextureBackend::release_upload_chunk(chunk);
    }
    pack_staging_.clear();
    movie_staging_.clear();
    movie_ring_ = {};
    movie_active_ = false;
    {
        const auto heap = backend_.heap_stats();
        std::cerr << "[texture-heap] heaps=" << heap.heaps << " heap-bytes=" << heap.heap_bytes
                  << " used-bytes=" << heap.used_bytes << " placed=" << heap.placed
                  << " committed-fallbacks=" << heap.committed_fallbacks << '\n';
    }
    entries_.clear();
    decoded_envelope_.clear();
    decoded_lru_.clear();
    decoded_lru_pos_.clear();
    decoded_srv_keys_ = {};
    decoded_resident_bytes_ = 0;
    decoded_source_bytes_ = 0;
    decoded_evicted_bytes_ = 0;
    decoded_evicted_entries_ = 0;
    efb_aliases_.clear();
    alias_envelope_.clear();
    retired_entries_.clear();
    for (auto& held : held_entries_) {
        held.clear();
    }
    hold_segment_ = kNoHoldSegment;
    texture_pool_.clear();
    texture_pool_bytes_ = 0;
    release_upload_arenas();
    upload_arenas_.clear();
    level_pixels_scratch_.clear();
    level_offsets_scratch_.clear();
    level_pitches_scratch_.clear();
    backend_.shutdown();
    next_srv_index_ = 0u;
    free_srv_indices_.clear();
}

void TextureCache::append_upload_arena_chunk(
    UploadArenaFrame& frame,
    std::uint64_t min_size) {
    const std::uint64_t chunk_size = align_up(
        std::max<std::uint64_t>(kUploadArenaChunkBytes, min_size),
        TextureBackend::kPlacementAlignment);
    TextureBackend::UploadChunk chunk = backend_.create_upload_chunk(chunk_size);
    try {
        frame.chunks.push_back(std::move(chunk));
    } catch (...) {
        TextureBackend::release_upload_chunk(chunk);
        throw;
    }
}

TextureCache::UploadAllocation TextureCache::allocate_upload_bytes(
    std::uint64_t size,
    std::uint64_t alignment) {
    if (size == 0u || alignment == 0u || (alignment & (alignment - 1u)) != 0u) {
        throw std::runtime_error("[TextureCache] invalid upload arena request");
    }
    if (upload_arenas_.empty()) {
        throw std::runtime_error(
            "[TextureCache] upload arena used before begin_frame");
    }

    UploadArenaFrame& frame = upload_arenas_[current_frame_slot_];
    for (;;) {
        if (frame.active_chunk >= frame.chunks.size()) {
            append_upload_arena_chunk(frame, size);
        }

        TextureBackend::UploadChunk& chunk = frame.chunks[frame.active_chunk];
        const std::uint64_t offset = align_up(frame.cursor, alignment);
        if (offset + size <= chunk.size) {
            frame.cursor = offset + size;
            return {&chunk, chunk.cpu_base, offset};
        }

        ++frame.active_chunk;
        frame.cursor = 0;
    }
}

std::uint32_t TextureCache::allocate_srv_index() {
    if (!free_srv_indices_.empty()) {
        const std::uint32_t index = free_srv_indices_.back();
        free_srv_indices_.pop_back();
        retired_srv_indices_.reset(index);
        return index;
    }
    if (next_srv_index_ >= kSrvHeapCapacity) {
        throw std::runtime_error("[TextureCache] SRV heap exhausted");
    }
    return next_srv_index_++;
}


bool TextureCache::register_efb_copy(
    std::uint64_t address,
    const EfbCopyParams& params,
    gpu::TextureRef texture) {
    // Validate before evicting a still-owned alias or consuming an SRV slot.
    if (!backend_.ready() || !texture) {
        throw std::runtime_error("[TextureCache] invalid EFB alias resource");
    }
    const std::uint32_t source_bytes = efb_copy_source_bytes(params);
    if (address + source_bytes < address) {
        throw std::runtime_error("[TextureCache] EFB alias address overflow");
    }
    std::uint32_t resource_width = 0, resource_height = 0;
    if (!TextureBackend::alias_compatible(gpu::raw(texture), resource_width, resource_height)) {
        throw std::runtime_error("[TextureCache] incompatible EFB alias resource");
    }
    const std::uint16_t alias_width = static_cast<std::uint16_t>(
        params.half_scale
            ? std::max<std::uint16_t>(1u, params.src_width / 2u)
            : params.src_width);
    const std::uint16_t alias_height = static_cast<std::uint16_t>(
        params.half_scale
            ? std::max<std::uint16_t>(1u, params.src_height / 2u)
            : params.src_height);
    const EfbAliasKey alias_key{
        address,
        alias_width,
        alias_height,
        efb_copy_alias_texture_format(params.target_format),
        {},
    };

    // Called on every copy. The renderer keys destination textures by address and format, so one address can
    // alternate resources. A same-resource alias still runs the overlap eviction below before returning.
    const auto current_alias_it = efb_aliases_.find(alias_key);
    const bool current_alias_same_resource =
        current_alias_it != efb_aliases_.end() &&
        gpu::raw(current_alias_it->second.texture) == gpu::raw(texture);

    const auto overlaps = [](std::uint64_t a_begin, std::uint64_t a_end,
                             std::uint64_t b_begin, std::uint64_t b_end) {
        return a_begin < b_end && b_begin < a_end;
    };

    const std::uint64_t new_begin = address;
    const std::uint64_t new_end =
        new_begin + source_bytes;

    // Evict every decoded entry and older alias overlapping the copy, not only the same address: the game reuses
    // capture buffers for glass and blur effects.
    bool evicted_overlap = false;
    for (auto it = decoded_envelope_.may_overlap(address, source_bytes)
             ? entries_.begin() : entries_.end(); it != entries_.end(); ) {
        const std::uint64_t entry_begin = it->first.address;
        const std::uint64_t entry_end =
            entry_begin + it->second.handle.source_bytes;
        if (overlaps(new_begin, new_end, entry_begin, entry_end)) {
            decoded_lru_release(it->first, it->second);
            retire(std::move(it->second));
            it = entries_.erase(it);
            evicted_overlap = true;
        } else {
            ++it;
        }
    }
    for (auto it = alias_envelope_.may_overlap(address, source_bytes)
             ? efb_aliases_.begin() : efb_aliases_.end(); it != efb_aliases_.end(); ) {
        const std::uint64_t alias_begin = it->first.address;
        const std::uint64_t alias_end =
            alias_begin + it->second.handle.source_bytes;
        const bool is_current_same_resource =
            it->first == alias_key &&
            gpu::raw(it->second.texture) == gpu::raw(texture);
        if (!is_current_same_resource &&
            overlaps(new_begin, new_end, alias_begin, alias_end)) {
            retire(std::move(it->second));
            it = efb_aliases_.erase(it);
            evicted_overlap = true;
        } else {
            ++it;
        }
    }

    // Removal never shrinks the envelope; an empty map resets it.
    if (efb_aliases_.empty()) alias_envelope_.clear();
    alias_envelope_.include(address, source_bytes);
    if (current_alias_same_resource) {
        const bool footprint_changed =
            current_alias_it->second.handle.source_bytes != source_bytes;
        current_alias_it->second.handle.source_bytes = source_bytes;
        return evicted_overlap || footprint_changed;
    }

    const std::uint32_t srv_idx = allocate_srv_index();

    // The renderer's EFB copy path creates a Texture2D.
    backend_.write_view(srv_idx, gpu::raw(texture), 1u);

    TextureHandle handle{};
    handle.resource      = gpu::raw(texture);
    handle.srv_index     = srv_idx;
    handle.width         = static_cast<std::uint16_t>(resource_width);
    handle.height        = static_cast<std::uint16_t>(resource_height);
    handle.mip_levels    = 1u;
    handle.from_efb_copy = true;
    handle.generated_mips = false;
    handle.source_bytes = source_bytes;

    Entry entry{};
    entry.texture = std::move(texture);
    entry.handle  = handle;

    auto alias_it = efb_aliases_.find(alias_key);
    if (alias_it != efb_aliases_.end()) {
        retire(std::move(alias_it->second));
        alias_it->second = std::move(entry);
    } else {
        efb_aliases_.emplace(alias_key, std::move(entry));
    }
    return true;
}

void TextureCache::invalidate_all() {
    for (auto& [key, entry] : entries_) {
        (void)key;
        retire(std::move(entry));
    }
    entries_.clear();
    decoded_envelope_.clear();
    decoded_lru_.clear();
    decoded_lru_pos_.clear();
    decoded_srv_keys_ = {};
    decoded_resident_bytes_ = 0;
    decoded_source_bytes_ = 0;
    // EFB aliases are kept: their content lives on the GPU.
    // Retired SRV slots return to the free list after their frame-slot fence.
}

std::size_t TextureCache::invalidate_range(
    std::uint64_t address,
    std::uint32_t size) {
    const MemoryRange range{address, size};
    return invalidate_ranges(std::span<const MemoryRange>(&range, 1u));
}
std::size_t TextureCache::invalidate_ranges(
    std::span<const MemoryRange> sorted_coalesced_ranges) {
    if (sorted_coalesced_ranges.empty() ||
        (entries_.empty() && efb_aliases_.empty())) {
        return 0u;
    }

    std::size_t evicted = 0;
    for (auto it = decoded_envelope_.may_overlap(sorted_coalesced_ranges)
             ? entries_.begin() : entries_.end(); it != entries_.end(); ) {
        const std::uint64_t tex_begin = it->first.address;
        const std::uint64_t tex_end = tex_begin + it->second.handle.source_bytes;
        if (first_overlapping_dirty_range(sorted_coalesced_ranges, tex_begin, tex_end) != nullptr) {
            decoded_lru_release(it->first, it->second);
            retire(std::move(it->second));
            it = entries_.erase(it);
            ++evicted;
        } else {
            ++it;
        }
    }
    for (auto it = alias_envelope_.may_overlap(sorted_coalesced_ranges)
             ? efb_aliases_.begin() : efb_aliases_.end(); it != efb_aliases_.end(); ) {
        const std::uint64_t alias_begin = it->first.address;
        const std::uint64_t alias_end =
            alias_begin + it->second.handle.source_bytes;
        if (first_overlapping_dirty_range(sorted_coalesced_ranges, alias_begin, alias_end) != nullptr) {
            retire(std::move(it->second));
            it = efb_aliases_.erase(it);
            ++evicted;
        } else {
            ++it;
        }
    }
    return evicted;
}

namespace {

// GALAXY_TEXTURE_PACK_TRACE=1: each replacement bound or swapped in, with its address and file.
bool trace_texture_packs_enabled() {
    static const bool enabled = read_env_u64("GALAXY_TEXTURE_PACK_TRACE", 0u) != 0u;
    return enabled;
}

}  // namespace

TextureCache::Entry TextureCache::make_replacement_entry(const TextureReplacement& replacement, std::uint32_t slot,
                                                         std::uint16_t width, std::uint16_t height,
                                                         bool generated_mips, std::uint32_t source_bytes) {
    const std::uint32_t srv_idx = allocate_srv_index();
    backend_.write_view(srv_idx, gpu::raw(replacement.texture), replacement.levels);
    Entry entry{};
    entry.texture = replacement.texture;
    // Not charged to the decoded budget and never pooled (pool_retired_texture): the packs' own budget covers it.
    entry.allocation_bytes = 0u;
    entry.handle.resource = gpu::raw(replacement.texture);
    entry.handle.srv_index = srv_idx;
    entry.handle.width = width;
    entry.handle.height = height;
    entry.handle.mip_levels = static_cast<std::uint8_t>(std::min(replacement.levels, 255u));
    entry.handle.from_efb_copy = false;
    entry.handle.generated_mips = generated_mips;
    entry.handle.lod_shift_x16 = replacement.lod_shift_x16;
    entry.handle.lod_arbitrary = replacement.arbitrary;
    entry.handle.source_bytes = source_bytes;
    entry.pack_slot = slot;
    packs_.add_user(slot);
    return entry;
}

void TextureCache::wait_for_replacement(const Key& key, Entry& entry, std::uint32_t slot) {
    entry.pack_slot = slot;
    entry.pack_waiting = true;
    packs_.add_user(slot);
    std::vector<Key>& waiters = pack_waiters_[slot];
    if (waiters.size() >= 64u) {
        // A slot whose load failed keeps its waiters: drop those already gone.
        std::erase_if(waiters, [&](const Key& waiting) {
            const auto it = entries_.find(waiting);
            return it == entries_.end() || it->second.pack_slot != slot || !it->second.pack_waiting;
        });
    }
    waiters.push_back(key);
}

void TextureCache::apply_texture_packs() {
    if (!backend_.has_upload_list() || upload_arenas_.empty()) {
        return;
    }
    const auto apply_start = std::chrono::steady_clock::now();
    if (packs_.pump(pack_uploads_)) {
        // New index, packs turned off, or dumping toggled: every decoded texture is looked up again at its next use.
        invalidate_all();
        pack_waiters_.clear();
    }
    // At most ~3 ms of uploads a frame (at least one); the rest wait, so a pack's first textures never stall a
    // frame (a 2048x2048 level chain is 22 MB to stage).
    constexpr auto kApplyBudget = std::chrono::microseconds(3000);
    std::size_t done = 0;
    for (; done < pack_uploads_.size(); ++done) {
        if (done != 0u && std::chrono::steady_clock::now() - apply_start > kApplyBudget) {
            break;
        }
        auto& [slot, pixels] = pack_uploads_[done];
        if (!packs_.uploading(slot)) {
            continue;  // handed out under an index replaced since
        }
        const auto upload_start = std::chrono::steady_clock::now();
        // The replacement's levels through this frame's staging, as a decoded texture's (get()).
        const auto levels = static_cast<unsigned>(pixels.levels.size());
        std::vector<std::uint64_t> offsets(levels);
        std::vector<std::uint32_t> pitches(levels);
        std::uint64_t staging_size = 0;
        for (unsigned l = 0; l < levels; ++l) {
            const std::uint32_t lw = std::max<std::uint32_t>(1u, pixels.width >> l);
            const std::uint32_t lh = std::max<std::uint32_t>(1u, pixels.height >> l);
            staging_size = (staging_size + TextureBackend::kPlacementAlignment - 1u) &
                           ~static_cast<std::uint64_t>(TextureBackend::kPlacementAlignment - 1u);
            offsets[l] = staging_size;
            pitches[l] = (lw * 4u + TextureBackend::kPitchAlignment - 1u) & ~(TextureBackend::kPitchAlignment - 1u);
            staging_size += static_cast<std::uint64_t>(pitches[l]) * lh;
        }
        TextureReplacement replacement{};
        try {
        if (pixels.staging && pixels.level_count != 0u && !current_frame_slot_invalid()) {
            // Staged by the loader: create and record the copy; the buffer lives until this slot comes round.
            std::uint64_t allocation_bytes = 0;
            gpu::TextureRef texture =
                backend_.create_texture(pixels.width, pixels.height, pixels.level_count, allocation_bytes);
            if (texture) {
                backend_.record_upload(gpu::raw(texture), false, *pixels.staging, 0u, pixels.width, pixels.height,
                                       pixels.level_count, pixels.offsets.data(), pixels.pitches.data());
                pack_staging_[current_frame_slot_].push_back(std::move(*pixels.staging));
                pixels.staging.reset();
                replacement.texture = std::move(texture);
                replacement.width = pixels.width;
                replacement.height = pixels.height;
                replacement.levels = pixels.level_count;
                replacement.bytes = allocation_bytes;
            }
        } else if (levels != 0u && staging_size != 0u) {
            const UploadAllocation staging = allocate_upload_bytes(staging_size, TextureBackend::kPlacementAlignment);
            if (staging.cpu_base != nullptr) {
                std::uint8_t* const base = staging.cpu_base + staging.offset;
                for (unsigned l = 0; l < levels; ++l) {
                    const std::uint32_t lw = std::max<std::uint32_t>(1u, pixels.width >> l);
                    const std::uint32_t lh = std::max<std::uint32_t>(1u, pixels.height >> l);
                    for (std::uint32_t row = 0; row < lh; ++row) {
                        std::memcpy(base + offsets[l] + static_cast<std::uint64_t>(row) * pitches[l],
                                    pixels.levels[l].data() + static_cast<std::size_t>(row) * lw * 4u, lw * 4u);
                    }
                }
                std::uint64_t allocation_bytes = 0;
                gpu::TextureRef texture = backend_.create_texture(pixels.width, pixels.height, levels, allocation_bytes);
                if (texture) {
                    backend_.record_upload(gpu::raw(texture), false, *staging.chunk, staging.offset, pixels.width,
                                           pixels.height, levels, offsets.data(), pitches.data());
                    replacement.texture = std::move(texture);
                    replacement.width = pixels.width;
                    replacement.height = pixels.height;
                    replacement.levels = levels;
                    replacement.bytes = allocation_bytes;
                }
            }
        }
        } catch (const std::exception& error) {
            // Out of video memory or staging: the original stays (the slot is marked failed).
            std::cerr << "[texture-pack] upload failed: " << error.what() << '\n';
            replacement = {};
        }
        const std::filesystem::path file = packs_.file(slot);
        if (trace_texture_packs_enabled()) {
            std::cerr << "[texture-pack] upload " << pixels.width << 'x' << pixels.height << " levels=" << replacement.levels
                      << (replacement.texture && pixels.levels.empty() ? " staged" : "")
                      << " ms=" << std::chrono::duration<double, std::milli>(
                                       std::chrono::steady_clock::now() - upload_start).count()
                      << ' ' << file.filename().string() << '\n';
        }
        pixels = {};
        packs_.make_resident(slot, std::move(replacement));
        const auto waiting = pack_waiters_.find(slot);
        const TextureReplacement* resident = packs_.resident(slot);
        if (waiting == pack_waiters_.end() || resident == nullptr) {
            continue;
        }
        for (const Key& key : waiting->second) {
            const auto it = entries_.find(key);
            if (it == entries_.end() || it->second.pack_slot != slot || !it->second.pack_waiting) {
                continue;
            }
            Entry next = make_replacement_entry(*resident, slot, it->second.handle.width, it->second.handle.height,
                                                it->second.handle.generated_mips, it->second.handle.source_bytes);
            next.handle.rt_material = it->second.handle.rt_material;
            next.handle.pack = it->second.handle.pack;
            decoded_lru_release(key, it->second);
            retire(std::move(it->second));
            it->second = std::move(next);
            decoded_lru_track(key);
            ++pack_swapped_;
            if (trace_texture_packs_enabled()) {
                std::cerr << "[texture-pack] swap addr=" << std::hex << key.address << std::dec << ' '
                          << file.filename().string() << '\n';
            }
        }
        pack_waiters_.erase(waiting);
    }
    pack_uploads_.erase(pack_uploads_.begin(), pack_uploads_.begin() + static_cast<std::ptrdiff_t>(done));
    const auto apply_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - apply_start).count());
    pack_apply_ns_ += apply_ns;
    pack_apply_max_ns_ = std::max(pack_apply_max_ns_, apply_ns);
    if (++pack_stats_frame_ % 600u == 0u && (packs_.enabled() || packs_.dumping())) {
        const TexturePacks::Stats stats = packs_.stats();
        std::cerr << "[texture-pack] names=" << stats.names << " hashed=" << pack_hashed_
                  << " apply-ms=" << static_cast<double>(pack_apply_ns_) / 1.0e6
                  << " apply-max-ms=" << static_cast<double>(pack_apply_max_ns_) / 1.0e6
                  << " bound=" << pack_bound_ << " swapped=" << pack_swapped_ << " loads=" << stats.loads
                  << " failures=" << stats.load_failures << " uploads=" << stats.uploads
                  << " upload-mb=" << (stats.upload_bytes >> 20u) << " resident=" << stats.resident
                  << " resident-mb=" << (stats.resident_bytes >> 20u) << " evictions=" << stats.evictions
                  << " deferred=" << stats.deferred << " load-ms=" << stats.load_ms << " dumped=" << stats.dumped
                  << '\n';
    }
}

bool TextureCache::movie_handle(std::uint32_t srv_index) const noexcept {
    for (const auto& planes : movie_ring_) {
        for (const MoviePlane& plane : planes) {
            if (plane.texture && plane.srv_index == srv_index) return true;
        }
    }
    return false;
}

bool TextureCache::set_movie_override(const MovieFrameMark& mark, const MovieReplacementFrame* frame) {
    if (frame == nullptr || mark.frame < 0 || !frame->staging || frame->width == 0u || frame->height == 0u) {
        const bool was = movie_active_;
        movie_active_ = false;
        movie_frame_ = -1;
        movie_staging_seen_ = nullptr;
        return was;
    }
    const std::array<std::uint64_t, 3> addresses{mark.y, mark.u, mark.v};
    const bool new_picture = frame->frame != movie_frame_ || frame->staging.get() != movie_staging_seen_;
    const bool same_planes = movie_active_ && addresses == movie_addr_ && mark.width == movie_source_width_[0] &&
                             mark.height == movie_source_height_[0];
    if (same_planes && !new_picture) {
        return false;
    }
    if (!backend_.has_upload_list() || current_frame_slot_ >= movie_staging_.size()) {
        return false;
    }
    if (new_picture) {
        movie_ring_at_ = (movie_ring_at_ + 1u) % kMovieRing;
        for (unsigned p = 0; p < 3u; ++p) {
            const std::uint32_t w = p == 0u ? frame->width : (frame->width + 1u) / 2u;
            const std::uint32_t h = p == 0u ? frame->height : (frame->height + 1u) / 2u;
            MoviePlane& plane = movie_ring_[movie_ring_at_][p];
            if (!plane.texture || plane.width != w || plane.height != h) {
                if (plane.texture) {
                    // Another size: the old texture and its slot retire after the frames that may still read them.
                    Entry old{};
                    old.handle.resource = gpu::raw(plane.texture);
                    old.handle.srv_index = plane.srv_index;
                    old.texture = std::move(plane.texture);
                    retire(std::move(old));
                }
                std::uint64_t bytes = 0;
                plane.texture = backend_.create_texture(w, h, 1u, bytes, true);
                plane.width = w;
                plane.height = h;
                plane.uploaded = false;
                plane.srv_index = allocate_srv_index();
                backend_.write_view(plane.srv_index, gpu::raw(plane.texture), 1u);
            }
            const std::uint64_t level_offset = 0u;
            const std::uint32_t pitch = frame->pitches[p];
            backend_.record_upload(gpu::raw(plane.texture), plane.uploaded, *frame->staging, frame->offsets[p], w, h,
                                   1u, &level_offset, &pitch);
            plane.uploaded = true;
        }
        movie_staging_[current_frame_slot_].push_back(frame->staging);
        movie_frame_ = frame->frame;
        movie_staging_seen_ = frame->staging.get();
    }
    movie_addr_ = addresses;
    for (unsigned p = 0; p < 3u; ++p) {
        // GX's sizes: U and V are the Y size shifted down (THPGXYuv2RgbDraw).
        movie_source_width_[p] = static_cast<std::uint16_t>(p == 0u ? mark.width : mark.width >> 1u);
        movie_source_height_[p] = static_cast<std::uint16_t>(p == 0u ? mark.height : mark.height >> 1u);
        const MoviePlane& plane = movie_ring_[movie_ring_at_][p];
        TextureHandle handle{};
        handle.resource = gpu::raw(plane.texture);
        handle.srv_index = plane.srv_index;
        handle.width = movie_source_width_[p];
        handle.height = movie_source_height_[p];
        handle.mip_levels = 1u;
        handle.source_bytes = static_cast<std::uint32_t>(movie_source_width_[p]) * movie_source_height_[p];
        movie_handles_[p] = handle;
    }
    movie_active_ = true;
    return true;
}

TextureHandle TextureCache::get(
    const TexImage& image,
    const TexMode& mode,
    std::uint64_t palette_address,
    TlutFormat palette_format) {
    // Colour-indexed images reach the renderer cooked, their palettes applied (nebula_cooked_texture.h).
    (void)palette_address;
    (void)palette_format;
    if (movie_active_ && image.format == TexFormat::I8) [[unlikely]] {
        // A replacement movie's plane in place of the THP's (set_movie_override).
        for (unsigned p = 0; p < 3u; ++p) {
            if (image.address == movie_addr_[p] && image.width == movie_source_width_[p] &&
                image.height == movie_source_height_[p]) {
                return movie_handles_[p];
            }
        }
    }
    const bool trace_stalls = trace_gx_stalls_enabled();
    const auto get_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // The key includes the chain length and whether it is generated: the game hand-authors lower levels (fog, lava
    // and water tinting; Dolphin PRs #6118 and #6875), so authored mips must never alias generated chains.
    unsigned levels = 1;
    unsigned source_levels = 1;
    bool generated_mips = false;
    {
        const unsigned mip_mode =
            static_cast<unsigned>(mode.min_filter) & 0x3u;
        const unsigned full_chain =
            full_mip_chain_levels(image.width, image.height);
        if (mip_mode != 0u) {
            const unsigned wanted =
                1u + (static_cast<unsigned>(mode.max_lod_x16) + 15u) / 16u;
            levels = std::min(wanted, full_chain);
            source_levels = levels;
        } else if (should_generate_native_mips(image, mode)) {
            levels = full_chain;
            source_levels = 1;
            generated_mips = levels > 1u;
        }
    }
    // A texture record (cooked, or made by the game) holds its levels as texels: nothing is decoded.
    const NebulaTexture* const record = registered_record(image.address);
    if (record != nullptr && source_levels > record->levels) {
        source_levels = record->levels;
        levels = source_levels;
    }
    // Authored levels come from the image the game handed over: a sampler asking for more than it holds gets those.
    if (record == nullptr && source_levels > 1u) {
        const auto handed = registered_images_.find(image.address);
        if (handed != registered_images_.end()) {
            const unsigned wanted = source_levels;
            while (source_levels > 1u &&
                   texture_source_bytes(image.format, image.width, image.height, source_levels) > handed->second->size()) {
                --source_levels;
            }
            levels = source_levels;
            static const bool log_clamp = read_env_u64("GALAXY_LOG_GUEST_TEXTURES", 0u) != 0u;
            if (log_clamp && source_levels != wanted) {
                std::fprintf(stderr, "[texture] levels limited to the handed-over image: %llX %ux%u format %u %u of %u\n",
                             image.address, image.width, image.height, static_cast<unsigned>(image.format),
                             source_levels, wanted);
            }
        }
    }
    const auto config_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    Key key{};
    key.address  = image.address;
    key.width       = image.width;
    key.height      = image.height;
    key.format      = static_cast<std::uint8_t>(image.format);
    key.levels      = static_cast<std::uint8_t>(levels);
    key.generated_mips = generated_mips ? 1u : 0u;
    const std::uint32_t total_source_size = record != nullptr
        ? record->size
        : texture_source_bytes(image.format, image.width, image.height, source_levels);
    if (image.address + total_source_size < image.address) {
        throw std::runtime_error("[TextureCache] texture address overflow");
    }
    const auto key_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // EFB alias hit. Matching the full descriptor keeps a stale full-screen capture from being sampled by a later,
    // smaller texture at the same address.
    {
        const EfbAliasKey alias_key{
            image.address,
            image.width,
            image.height,
            static_cast<std::uint8_t>(image.format),
            {},
        };
        const auto alias_it = efb_aliases_.find(alias_key);
        if (alias_it != efb_aliases_.end()) {
            return alias_it->second.handle;
        }
    }
    const auto alias_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    {
        const auto it = entries_.find(key);
        if (it != entries_.end()) {
            decoded_lru_touch(key);
            return it->second.handle;
        }
    }
    const auto cache_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // Miss: decode. Authored levels are contiguous and block-aligned and resolve as one range; generated mips
    // read level 0 only.
    const std::uint32_t w = image.width;
    const std::uint32_t h = image.height;

    const std::byte* handed_over = registered_bytes(image.address, total_source_size);
    if (handed_over == nullptr && record == nullptr) {
        ++missing_images_;
        std::fprintf(stderr, "[texture] image not handed over: %llX %ux%u format %u (%u bytes)\n", static_cast<unsigned long long>(image.address),
                     image.width, image.height, static_cast<unsigned>(image.format), total_source_size);
    }
    const auto* src = reinterpret_cast<const std::uint8_t*>(
        handed_over != nullptr ? handed_over : missing_image_bytes(total_source_size));
    // The ray tracer's material table names textures by the texture-pack key of their base level.
    // A record carries the disc image's names (pack hash, palette hash, disc format); one the game made has none.
    const TexFormat source_format = record != nullptr ? static_cast<TexFormat>(record->disc_format) : image.format;
    const bool nameless = record != nullptr && record->disc_format == NEBULA_TEXTURE_MADE;
    std::uint16_t rt_material = 0u;
    std::uint64_t pack = 0u;
    if (rt_materials_loaded() && !nameless) {
        pack = record != nullptr ? record->pack_hash
                                 : texture_pack_hash(src, texture_source_bytes(image.format, image.width, image.height, 1u));
        rt_material = rt_material_index(pack);
        note_rt_texture(image.address, pack, image.width, image.height, static_cast<std::uint32_t>(source_format));
    }
    // Texture packs: the naming convention's hashes of the base level (and of the palette range it uses). A resident
    // replacement binds without a decode; else the original decodes and waits for it.
    std::uint32_t pack_slot = TexturePacks::kNone;
    TexturePackName pack_name{};
    const bool pack_lookup = !nameless && packs_.may_match(image.width, image.height, source_format);
    const bool pack_dump = !nameless && packs_.dumping();
    if (pack_lookup || pack_dump) {
        pack_name.width = image.width;
        pack_name.height = image.height;
        pack_name.format = static_cast<std::uint8_t>(source_format);
        pack_name.mipmapped = source_levels > 1u;
        if (record != nullptr) {
            pack_name.hash = record->pack_hash;
            pack_name.tlut = record->pack_palette_hash;
        } else {
            pack_name.hash = pack != 0u ? pack
                                        : texture_pack_hash(src, texture_source_bytes(image.format, image.width,
                                                                                         image.height, 1u));
        }
        ++pack_hashed_;
        if (pack_lookup) {
            pack_slot = packs_.find(image.width, image.height, image.format, pack_name.hash, pack_name.tlut,
                                    pack_name.mipmapped);
            if (const TextureReplacement* replacement = packs_.resident(pack_slot)) {
                Entry entry = make_replacement_entry(*replacement, pack_slot, image.width, image.height,
                                                     generated_mips, total_source_size);
                entry.handle.rt_material = rt_material;
                entry.handle.pack = pack;
                const TextureHandle handle = entry.handle;
                if (entries_.empty()) decoded_envelope_.clear();
                decoded_envelope_.include(image.address, total_source_size);
                entries_.emplace(key, std::move(entry));
                decoded_lru_track(key);
                ++pack_bound_;
                if (trace_texture_packs_enabled()) {
                    std::cerr << "[texture-pack] bind addr=" << std::hex << image.address << std::dec << ' '
                              << packs_.file(pack_slot).filename().string() << '\n';
                }
                return handle;
            }
        }
    }
    const auto resolve_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    const auto miss_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (trace_stalls) {
        const std::uint64_t pre_miss_us =
            elapsed_us(get_start, miss_start);
        if (pre_miss_us >= trace_gx_stall_threshold_us()) {
            std::cerr << "[gx-texture-premiss] addr=0x" << std::hex
                      << image.address << std::dec
                      << " size=" << total_source_size
                      << " config-us="
                      << elapsed_us(get_start, config_end)
                      << " key-us=" << elapsed_us(config_end, key_end)
                      << " alias-us=" << elapsed_us(key_end, alias_end)
                      << " cache-us=" << elapsed_us(alias_end, cache_end)
                      << " resolve-us=" << elapsed_us(cache_end, resolve_end)
                      << " total-us=" << pre_miss_us
                      << '\n';
        }
    }
    // Only the pixel fill moves off-thread. Texture-pack dumps, which read the decoded pixels here, and unknown
    // formats, which must throw here, stay synchronous.
    const bool async_decode = async_decode_allowed_ &&
        (record != nullptr || texture_decode_format_supported(image.format)) && !pack_dump;
    if (level_pixels_scratch_.size() < levels) {
        level_pixels_scratch_.resize(levels);
    }
    static const bool log_console_images = read_env_u64("GALAXY_LOG_GUEST_TEXTURES", 0u) != 0u;
    if (log_console_images && record == nullptr) {
        // What still reaches the renderer in a console format (the cooker and the game's records cover the rest).
        std::fprintf(stderr, "[texture] console image: %llX %ux%u format %u\n", static_cast<unsigned long long>(image.address),
                     image.width, image.height, static_cast<unsigned>(image.format));
    }
    auto& level_pixels = level_pixels_scratch_;
    if (!async_decode) {
        if (record != nullptr) {
            expand_record_levels(record, source_levels, levels, generated_mips, level_pixels);
        } else {
            decode_texture_levels(
                image.format, src, w, h, source_levels, levels, generated_mips, level_pixels);
        }
    }
    const auto decode_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // One staging buffer holds every level at a 512-aligned placed footprint (256-aligned row pitch), copied into a
    // DEFAULT-heap Texture2D on the frame's command list. The shader samples Texture2D: a buffer SRV in that slot
    // is undefined behaviour and hangs the GPU.
    if (!backend_.has_upload_list()) {
        throw std::runtime_error(
            "[TextureCache] get: no upload command list set");
    }

    // Per-level placed footprints.
    if (level_offsets_scratch_.size() < levels) {
        level_offsets_scratch_.resize(levels);
    }
    if (level_pitches_scratch_.size() < levels) {
        level_pitches_scratch_.resize(levels);
    }
    auto& level_offset = level_offsets_scratch_;
    auto& level_pitch = level_pitches_scratch_;
    std::uint64_t staging_size = 0;
    for (unsigned l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, w >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, h >> l);
        staging_size =
            (staging_size + TextureBackend::kPlacementAlignment - 1u) &
            ~static_cast<std::uint64_t>(TextureBackend::kPlacementAlignment - 1u);
        level_offset[l] = staging_size;
        level_pitch[l] =
            (lw * 4u + TextureBackend::kPitchAlignment - 1u) &
            ~(TextureBackend::kPitchAlignment - 1u);
        staging_size += static_cast<std::uint64_t>(level_pitch[l]) * lh;
    }

    const UploadAllocation staging = allocate_upload_bytes(
        staging_size,
        TextureBackend::kPlacementAlignment);
    if (staging.cpu_base == nullptr) {
        throw std::runtime_error(
            "[TextureCache] upload arena is not mapped");
    }
    auto* const dst_base = staging.cpu_base + staging.offset;
    if (async_decode) {
        if (!decode_pool_) {
            const unsigned hardware = std::thread::hardware_concurrency();
            // First-use decodes come in bursts at scene loads, while the
            // render thread waits for them; idle workers cost nothing.
            const unsigned workers = std::clamp(hardware / 2u, 2u, 8u);
            decode_pool_ = std::make_unique<TextureDecodePool>(workers, [] {
                galaxy::host::name_current_thread(L"Nebula Texture Decode");
            });
        }
        std::vector<std::uint64_t> offsets(level_offset.begin(), level_offset.begin() + levels);
        std::vector<std::uint32_t> pitches(level_pitch.begin(), level_pitch.begin() + levels);
        const TexFormat format = image.format;
        // The source bytes stay unchanged until finish_pending_decodes() (caller contract).
        decode_pool_->submit(
            [format, source_ptr = src, record, w, h, source_levels, levels,
             generated_mips, dst_base, offsets = std::move(offsets),
             pitches = std::move(pitches)]() {
                thread_local std::vector<std::vector<RGBA8>> scratch;
                if (scratch.size() < levels) {
                    scratch.resize(levels);
                }
                if (record != nullptr) {
                    expand_record_levels(record, source_levels, levels, generated_mips, scratch);
                } else {
                    decode_texture_levels(
                        format, source_ptr, w, h, source_levels, levels,
                        generated_mips, scratch);
                }
                write_staging_levels(
                    dst_base, w, h, levels, offsets.data(), pitches.data(),
                    scratch);
            });
        async_decode_pending_ = true;
        ++async_decode_jobs_;
    } else {
        write_staging_levels(
            dst_base, w, h, levels, level_offset.data(), level_pitch.data(),
            level_pixels);
    }
    const auto staging_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    gpu::TextureRef resource;
    std::uint64_t allocation_bytes = 0;
    const bool reused_resource = take_pooled_texture(w, h, levels, resource, allocation_bytes);
    if (!reused_resource) {
        resource = backend_.create_texture(w, h, levels, allocation_bytes);
    }
    const auto resource_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    backend_.record_upload(gpu::raw(resource), reused_resource, *staging.chunk, staging.offset, w, h, levels,
                           level_offset.data(), level_pitch.data());

    const std::uint32_t srv_idx = allocate_srv_index();
    backend_.write_view(srv_idx, gpu::raw(resource), levels);
    const auto upload_record_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    const auto entry_build_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    TextureHandle handle{};
    handle.resource      = gpu::raw(resource);
    handle.srv_index     = srv_idx;
    handle.width         = static_cast<std::uint16_t>(w);
    handle.height        = static_cast<std::uint16_t>(h);
    handle.mip_levels    = static_cast<std::uint8_t>(levels);
    handle.from_efb_copy = false;
    handle.generated_mips = generated_mips;
    handle.source_bytes = total_source_size;
    handle.rt_material = rt_material;
    handle.pack = pack;

    Entry entry{};
    entry.texture = std::move(resource);
    entry.allocation_bytes = allocation_bytes;
    entry.handle  = handle;
    const auto entry_build_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    const auto tex_dump_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (pack_dump) {
        // Synchronous decode in dump mode: level_pixels holds the base level.
        packs_.dump(pack_name, w, h, reinterpret_cast<const std::uint8_t*>(level_pixels[0].data()));
    }
    const auto tex_dump_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    const TextureHandle result = entry.handle;
    const auto emplace_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (entries_.empty()) decoded_envelope_.clear();
    decoded_envelope_.include(image.address, total_source_size);
    const auto decoded = entries_.emplace(key, std::move(entry)).first;
    decoded_lru_track(key);
    if (pack_slot != TexturePacks::kNone) {
        wait_for_replacement(key, decoded->second, pack_slot);
    }
    const auto emplace_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::uint64_t total_us = trace_stalls
        ? elapsed_us(miss_start, upload_record_end) : 0u;
    if (trace_stalls) {
        const std::uint64_t post_record_us =
            elapsed_us(upload_record_end, emplace_end);
        if (post_record_us >= trace_gx_stall_threshold_us()) {
            std::cerr << "[gx-texture-postrecord] addr=0x" << std::hex
                      << image.address << std::dec
                      << " entry-us="
                      << elapsed_us(entry_build_start, entry_build_end)
                      << " texdump-us="
                      << elapsed_us(tex_dump_start, tex_dump_end)
                      << " emplace-us="
                      << elapsed_us(emplace_start, emplace_end)
                      << " total-us=" << post_record_us
                      << '\n';
        }
    }
    if (trace_stalls &&
        total_us >= trace_gx_stall_threshold_us()) {
        std::cerr << "[gx-texture-miss] addr=0x" << std::hex
                  << image.address << std::dec
                  << " fmt=" << static_cast<unsigned>(image.format)
                  << " size=" << w << 'x' << h
                  << " levels=" << levels
                  << " native-mips=" << (generated_mips ? 1u : 0u)
                  << " guest-bytes=" << total_source_size
                  << " staging-bytes=" << staging_size
                  << " srv=" << srv_idx
                  << " async-decode=" << (async_decode ? 1u : 0u)
                  << " decode-us=" << elapsed_us(miss_start, decode_end)
                  << " staging-us=" << elapsed_us(decode_end, staging_end)
                  << " resource-us=" << elapsed_us(staging_end, resource_end)
                  << " record-us=" << elapsed_us(resource_end, upload_record_end)
                  << " total-us=" << total_us
                  << '\n';
    }
    return result;
}

} // namespace galaxy::render
