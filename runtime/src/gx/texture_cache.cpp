// texture_cache.cpp — GX texture decode → D3D12 resource upload
// Part of the Nebula native runtime (M4).
//
// Design constraints (from architecture review):
//   * Address-cache invalidation is explicit; an optional, exact-content
//     resource cache can reuse identical decoded static textures.
//   * Hard-fail on unmapped guest range or unknown format — no stubs.
//   * UPLOAD-heap textures for M4 simplicity (no copy queue needed).
//   * One non-shader-visible SRV heap; renderer copies slots into the
//     frame's shader-visible DescriptorRing before each draw.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/dependency_event_capture.h"

#include "galaxy/gx/render_config.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace galaxy::gx {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

// Maximum textures the SRV heap can hold.  Bump when > 4096 live textures
// are needed; M4 does not expect that.
static constexpr UINT kSrvHeapCapacity = 4096u;
static constexpr UINT64 kUploadArenaChunkBytes = 16ull << 20;
static constexpr std::uint64_t kContentCacheBudgetBytes = 64ull << 20;
static constexpr std::size_t kContentCacheMaxEntries = 4096u;

bool decoded_content_cache_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_GX_CONTENT_TEXTURE_CACHE") == 0 &&
               length > 1u && value[0] != '0' &&
               value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

UINT64 align_up(UINT64 value, UINT64 alignment) {
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

bool trace_texture_dirty_detail_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value),
                   "GALAXY_TRACE_GX_TEXTURE_DIRTY_DETAIL") == 0 &&
            length > 1u && value[0] != '0';
    }();
    return enabled;
}

const TextureCache::GuestRange* first_overlapping_dirty_range(
    std::span<const TextureCache::GuestRange> ranges,
    std::uint64_t target_begin,
    std::uint64_t target_end) {
    if (ranges.empty() || target_begin >= target_end) {
        return nullptr;
    }

    const auto it = std::lower_bound(
        ranges.begin(),
        ranges.end(),
        target_begin,
        [](const TextureCache::GuestRange& range,
           std::uint64_t value) noexcept {
            return static_cast<std::uint64_t>(range.guest_addr) +
                       static_cast<std::uint64_t>(range.size) <=
                   value;
        });
    if (it == ranges.end()) {
        return nullptr;
    }
    if (static_cast<std::uint64_t>(it->guest_addr) < target_end) {
        return &*it;
    }
    return nullptr;
}

bool trace_texture_uploads_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_TEXTURE_UPLOADS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

std::uint64_t read_env_u64(const char* name, std::uint64_t fallback) {
    char value[32]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length == 0) {
        return fallback;
    }
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    return end == value ? fallback : parsed;
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

// Throw a std::runtime_error with a D3D12 HRESULT decorated message.
[[noreturn]] static void d3d_fail(HRESULT hr, const char* label) {
    std::ostringstream ss;
    ss << "[TextureCache] " << label
       << " HRESULT=0x" << std::hex << std::uppercase
       << std::bit_cast<std::uint32_t>(static_cast<std::int32_t>(hr));
    throw std::runtime_error(ss.str());
}

static void d3d_check(HRESULT hr, const char* label) {
    if (FAILED(hr)) { d3d_fail(hr, label); }
}

// ---------------------------------------------------------------------------
// Guest memory access — fatal on unmapped range (hard-fail policy).
// We don't have a NativeServicesV1* in the texture path, so we abort directly
// via a runtime_error; the caller's exception handler propagates it as a
// GxFatalError.
// ---------------------------------------------------------------------------

static const std::byte* resolve_guest_tex(
    GuestMemoryV1* memory,
    std::uint32_t address,
    std::uint32_t size) {
    if (std::byte* fast = resolve_guest_fast(memory, address, size);
        fast != nullptr) {
        return fast;
    }
    if (memory != nullptr && memory->regions != nullptr) {
        const std::uint64_t req_end =
            static_cast<std::uint64_t>(address) + size;
        for (std::uint32_t i = 0; i < memory->region_count; ++i) {
            const GuestMemoryRegionV1& r = memory->regions[i];
            if (r.host_base == nullptr) { continue; }
            const std::uint64_t reg_end =
                static_cast<std::uint64_t>(r.guest_base) + r.size;
            if (address >= r.guest_base && req_end <= reg_end) {
                return r.host_base + (address - r.guest_base);
            }
        }
    }
    std::ostringstream ss;
    ss << "[TextureCache] guest address 0x" << std::hex << std::uppercase
       << address << " (size " << std::dec << size << ") is not mapped";
    throw std::runtime_error(ss.str());
}

// ---------------------------------------------------------------------------
// Big-endian 16-bit read from an arbitrary byte pointer (TLUT / texture data).
// ---------------------------------------------------------------------------

[[nodiscard]] static inline std::uint16_t read_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

// ---------------------------------------------------------------------------
// TLUT entry decode helpers — each entry is 2 big-endian bytes.
// ---------------------------------------------------------------------------

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
    if ((static_cast<unsigned>(mode.min_filter) & 0x3u) != 0u ||
        mode.min_filter != TexMinFilter::Linear ||
        mode.mag_filter != TexMagFilter::Linear ||
        (image.width < 64u && image.height < 64u) ||
        !generated_mips_color_format(image.format)) {
        return false;
    }
    // The shared settings lock/copy is needed only when this option can affect
    // the resource. Authored mips, nearest filtering, small and intensity-only
    // textures have a complete decision from their guest descriptors.
    return get_render_config().enhanced_mipmaps;
}

}  // namespace

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
                    // For equal nonzero alpha, it cancels exactly from
                    // (alpha * sum + 2 * alpha) / (4 * alpha). Keep the
                    // original zero RGB for fully transparent input.
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

[[nodiscard]] static RGBA8 decode_tlut_entry(
    const std::uint8_t* entry,
    TlutFormat fmt) {
    const std::uint16_t v = read_be16(entry);
    switch (fmt) {
    case TlutFormat::IA8: {
        const std::uint8_t a = static_cast<std::uint8_t>(v >> 8);
        const std::uint8_t i = static_cast<std::uint8_t>(v & 0xFFu);
        return RGBA8{i, i, i, a};
    }
    case TlutFormat::RGB565: {
        const std::uint8_t r5 = static_cast<std::uint8_t>((v >> 11) & 0x1Fu);
        const std::uint8_t g6 = static_cast<std::uint8_t>((v >> 5)  & 0x3Fu);
        const std::uint8_t b5 = static_cast<std::uint8_t>( v        & 0x1Fu);
        return RGBA8{
            static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
            static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4)),
            static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2)),
            0xFFu
        };
    }
    case TlutFormat::RGB5A3: {
        if ((v & 0x8000u) != 0u) {
            // RGB555
            const std::uint8_t r5 = static_cast<std::uint8_t>((v >> 10) & 0x1Fu);
            const std::uint8_t g5 = static_cast<std::uint8_t>((v >>  5) & 0x1Fu);
            const std::uint8_t b5 = static_cast<std::uint8_t>( v        & 0x1Fu);
            return RGBA8{
                static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
                static_cast<std::uint8_t>((g5 << 3) | (g5 >> 2)),
                static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2)),
                0xFFu
            };
        } else {
            // RGBA3444
            const std::uint8_t a3 = static_cast<std::uint8_t>((v >> 12) & 0x7u);
            const std::uint8_t r4 = static_cast<std::uint8_t>((v >>  8) & 0xFu);
            const std::uint8_t g4 = static_cast<std::uint8_t>((v >>  4) & 0xFu);
            const std::uint8_t b4 = static_cast<std::uint8_t>( v        & 0xFu);
            return RGBA8{
                static_cast<std::uint8_t>((r4 << 4) | r4),
                static_cast<std::uint8_t>((g4 << 4) | g4),
                static_cast<std::uint8_t>((b4 << 4) | b4),
                static_cast<std::uint8_t>((a3 << 5) | (a3 << 2) | (a3 >> 1))
            };
        }
    }
    default:
        throw std::runtime_error("[TextureCache] unknown TlutFormat");
    }
}

// ---------------------------------------------------------------------------
// Per-format decode functions.
// Each writes `width * height` RGBA8 pixels into `out` (row-major, no padding).
// `src` points into guest memory at the base of the texture.
// CI palettes live in the cache's absolute TMEM byte bank. The caller validates
// the full palette extent at tlut.tmem_offset * 512; each entry occupies 2 bytes.
// ---------------------------------------------------------------------------

// The block formats below use raster pixel order inside each tile.

template <TexFormat Format>
void decode_intensity_tiles_impl(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    RGBA8* out) {
    static_assert(Format == TexFormat::I4 || Format == TexFormat::I8 ||
                  Format == TexFormat::IA4 || Format == TexFormat::IA8);
    // All four formats use a 32-byte tile. Clip once per tile, then decode
    // contiguous valid rows without per-pixel destination edge tests.
    constexpr std::uint32_t block_width = Format == TexFormat::IA8 ? 4u : 8u;
    constexpr std::uint32_t block_height = Format == TexFormat::I4 ? 8u : 4u;
    constexpr std::uint32_t row_bytes = 32u / block_height;
    const std::uint32_t blocks_x = (width + block_width - 1u) / block_width;
    const std::uint32_t blocks_y = (height + block_height - 1u) / block_height;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        const std::uint32_t base_y = by * block_height;
        const std::uint32_t rows = std::min(block_height, height - base_y);
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            const std::uint32_t base_x = bx * block_width;
            const std::uint32_t columns = std::min(block_width, width - base_x);
            for (std::uint32_t py = 0; py < rows; ++py) {
                const std::uint8_t* row = src + py * row_bytes;
                RGBA8* dst = out + static_cast<std::size_t>(base_y + py) * width + base_x;
                if constexpr (Format == TexFormat::I4) {
                    // High nibble first; write both pixels without a half
                    // selector branch. I4/I8 replicate intensity into ALPHA.
                    std::uint32_t px = 0u;
                    for (; px + 1u < columns; px += 2u) {
                        const std::uint8_t byte = row[px / 2u];
                        const auto high = static_cast<std::uint8_t>((byte >> 4u) * 17u);
                        const auto low = static_cast<std::uint8_t>((byte & 15u) * 17u);
                        dst[px] = RGBA8{high, high, high, high};
                        dst[px + 1u] = RGBA8{low, low, low, low};
                    }
                    if (px < columns) {
                        const auto high = static_cast<std::uint8_t>((row[px / 2u] >> 4u) * 17u);
                        dst[px] = RGBA8{high, high, high, high};
                    }
                } else {
                    for (std::uint32_t px = 0; px < columns; ++px) {
                        if constexpr (Format == TexFormat::I8) {
                            const std::uint8_t i = row[px];
                            dst[px] = RGBA8{i, i, i, i};
                        } else if constexpr (Format == TexFormat::IA4) {
                            // A[7:4] I[3:0], both expanded by bit replication.
                            const std::uint8_t byte = row[px];
                            const auto i = static_cast<std::uint8_t>((byte & 15u) * 17u);
                            const auto a = static_cast<std::uint8_t>((byte >> 4u) * 17u);
                            dst[px] = RGBA8{i, i, i, a};
                        } else {
                            // IA8 stores A then I in guest byte order.
                            const std::uint8_t a = row[px * 2u];
                            const std::uint8_t i = row[px * 2u + 1u];
                            dst[px] = RGBA8{i, i, i, a};
                        }
                    }
                }
            }
            // Padding remains part of the validated guest footprint, even
            // though no destination pixel needs the clipped source texels.
            src += 32u;
        }
    }
}

static void decode_RGB565(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    RGBA8* out) {
    // Block: 4×4 pixels; two big-endian bytes per pixel.
    const std::uint32_t blocks_x = (width  + 3u) / 4u;
    const std::uint32_t blocks_y = (height + 3u) / 4u;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            for (std::uint32_t py = 0; py < 4u; ++py) {
                for (std::uint32_t px = 0; px < 4u; ++px) {
                    const std::uint16_t v = read_be16(src); src += 2;
                    const std::uint32_t ix = bx * 4u + px;
                    const std::uint32_t iy = by * 4u + py;
                    if (ix < width && iy < height) {
                        const std::uint8_t r5 =
                            static_cast<std::uint8_t>((v >> 11) & 0x1Fu);
                        const std::uint8_t g6 =
                            static_cast<std::uint8_t>((v >>  5) & 0x3Fu);
                        const std::uint8_t b5 =
                            static_cast<std::uint8_t>( v        & 0x1Fu);
                        out[iy * width + ix] = RGBA8{
                            static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
                            static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4)),
                            static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2)),
                            0xFFu
                        };
                    }
                }
            }
        }
    }
}

static void decode_RGB5A3(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    RGBA8* out) {
    // Block: 4×4 pixels; two big-endian bytes per pixel.
    const std::uint32_t blocks_x = (width  + 3u) / 4u;
    const std::uint32_t blocks_y = (height + 3u) / 4u;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            for (std::uint32_t py = 0; py < 4u; ++py) {
                for (std::uint32_t px = 0; px < 4u; ++px) {
                    const std::uint16_t v = read_be16(src); src += 2;
                    const std::uint32_t ix = bx * 4u + px;
                    const std::uint32_t iy = by * 4u + py;
                    if (ix < width && iy < height) {
                        RGBA8 pixel{};
                        if ((v & 0x8000u) != 0u) {
                            // RGB555
                            const std::uint8_t r5 =
                                static_cast<std::uint8_t>((v >> 10) & 0x1Fu);
                            const std::uint8_t g5 =
                                static_cast<std::uint8_t>((v >>  5) & 0x1Fu);
                            const std::uint8_t b5 =
                                static_cast<std::uint8_t>( v        & 0x1Fu);
                            pixel = RGBA8{
                                static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
                                static_cast<std::uint8_t>((g5 << 3) | (g5 >> 2)),
                                static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2)),
                                0xFFu
                            };
                        } else {
                            // RGBA3444
                            const std::uint8_t a3 =
                                static_cast<std::uint8_t>((v >> 12) & 0x7u);
                            const std::uint8_t r4 =
                                static_cast<std::uint8_t>((v >>  8) & 0xFu);
                            const std::uint8_t g4 =
                                static_cast<std::uint8_t>((v >>  4) & 0xFu);
                            const std::uint8_t b4 =
                                static_cast<std::uint8_t>( v        & 0xFu);
                            pixel = RGBA8{
                                static_cast<std::uint8_t>((r4 << 4) | r4),
                                static_cast<std::uint8_t>((g4 << 4) | g4),
                                static_cast<std::uint8_t>((b4 << 4) | b4),
                                static_cast<std::uint8_t>(
                                    (a3 << 5) | (a3 << 2) | (a3 >> 1))
                            };
                        }
                        out[iy * width + ix] = pixel;
                    }
                }
            }
        }
    }
}

}  // namespace

void detail::decode_intensity_tiles(
    TexFormat format, const std::uint8_t* src,
    std::uint32_t width, std::uint32_t height, RGBA8* out) {
    switch (format) {
    case TexFormat::I4: decode_intensity_tiles_impl<TexFormat::I4>(src, width, height, out); break;
    case TexFormat::I8: decode_intensity_tiles_impl<TexFormat::I8>(src, width, height, out); break;
    case TexFormat::IA4: decode_intensity_tiles_impl<TexFormat::IA4>(src, width, height, out); break;
    case TexFormat::IA8: decode_intensity_tiles_impl<TexFormat::IA8>(src, width, height, out); break;
    default: throw std::runtime_error("[TextureCache] unsupported intensity format");
    }
}

void detail::decode_rgba8_tiles(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    RGBA8* out) {
    // Block: 4×4 pixels stored as two 32-byte sub-tiles:
    //   tile 0 (bytes  0-31): 16× AR pairs (A then R, row-major within 4x4)
    //   tile 1 (bytes 32-63): 16× GB pairs (G then B)
    const std::uint32_t blocks_x = (width  + 3u) / 4u;
    const std::uint32_t blocks_y = (height + 3u) / 4u;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        const std::uint32_t base_y = by * 4u;
        const std::uint32_t rows = std::min(4u, height - base_y);
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            const std::uint32_t base_x = bx * 4u;
            const std::uint32_t columns = std::min(4u, width - base_x);
            const std::uint8_t* ar = src;
            const std::uint8_t* gb = src + 32u;
            for (std::uint32_t py = 0; py < rows; ++py) {
                RGBA8* dst = out + (base_y + py) * width + base_x;
                const std::uint8_t* ar_row = ar + py * 8u;
                const std::uint8_t* gb_row = gb + py * 8u;
                for (std::uint32_t px = 0; px < columns; ++px) {
                    dst[px] = {ar_row[px * 2u + 1u], gb_row[px * 2u],
                               gb_row[px * 2u + 1u], ar_row[px * 2u]};
                }
            }
            src += 64u;
        }
    }
}

namespace {

// C4/C8 indices use either raw entry conversion or an already decoded small
// palette. Keep the same tiled traversal for both paths.

template <typename PaletteLookup>
static void decode_C4(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    const PaletteLookup& lookup,
    RGBA8* out) {
    // Block: 8×8 pixels; 4 bits per index, high nibble first.
    const std::uint32_t blocks_x = (width  + 7u) / 8u;
    const std::uint32_t blocks_y = (height + 7u) / 8u;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            for (std::uint32_t py = 0; py < 8u; ++py) {
                for (std::uint32_t px = 0; px < 8u; px += 2u) {
                    const std::uint8_t byte = *src++;
                    for (std::uint32_t half = 0; half < 2u; ++half) {
                        const std::uint32_t ix = bx * 8u + px + half;
                        const std::uint32_t iy = by * 8u + py;
                        if (ix < width && iy < height) {
                            const std::uint32_t index =
                                (half == 0u)
                                    ? static_cast<std::uint32_t>((byte >> 4) & 0xFu)
                                    : static_cast<std::uint32_t>(byte & 0xFu);
                            out[iy * width + ix] = lookup(index);
                        }
                    }
                }
            }
        }
    }
}

template <typename PaletteLookup>
static void decode_C8(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    const PaletteLookup& lookup,
    RGBA8* out) {
    // Block: 8×4 pixels; one byte per index.
    const std::uint32_t blocks_x = (width  + 7u) / 8u;
    const std::uint32_t blocks_y = (height + 3u) / 4u;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            for (std::uint32_t py = 0; py < 4u; ++py) {
                for (std::uint32_t px = 0; px < 8u; ++px) {
                    const std::uint32_t index = *src++;
                    const std::uint32_t ix = bx * 8u + px;
                    const std::uint32_t iy = by * 4u + py;
                    if (ix < width && iy < height) {
                        out[iy * width + ix] = lookup(index);
                    }
                }
            }
        }
    }
}

template <std::size_t Entries, typename Decode>
static void decode_with_small_palette(
    const std::uint8_t* entries, TlutFormat format,
    std::uint64_t pixels, const Decode& decode) {
    const auto raw_lookup = [entries, format](std::uint32_t index) {
        return decode_tlut_entry(entries + index * 2u, format);
    };
    // Amortize table construction only when it removes at least three of
    // every four color conversions. Tiny textures retain the original path.
    if (pixels < Entries * 4u) {
        decode(raw_lookup);
        return;
    }
    std::array<RGBA8, Entries> palette;
    for (std::size_t index = 0; index < Entries; ++index) {
        palette[index] = raw_lookup(static_cast<std::uint32_t>(index));
    }
    decode([&palette](std::uint32_t index) { return palette[index]; });
}

}  // namespace

void detail::decode_small_index_tiles(
    TexFormat format, const std::uint8_t* src,
    std::uint32_t width, std::uint32_t height,
    const std::uint8_t* palette, TlutFormat tlut_format, RGBA8* out) {
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    switch (format) {
    case TexFormat::C4:
        decode_with_small_palette<16u>(palette, tlut_format, pixels,
            [&](const auto& lookup) { decode_C4(src, width, height, lookup, out); });
        return;
    case TexFormat::C8:
        decode_with_small_palette<256u>(palette, tlut_format, pixels,
            [&](const auto& lookup) { decode_C8(src, width, height, lookup, out); });
        return;
    default:
        throw std::runtime_error("[TextureCache] expected small indexed format");
    }
}

namespace {

static void decode_C14X2(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* tlut_bank,
    std::uint32_t tlut_byte_offset,
    TlutFormat tlut_fmt,
    RGBA8* out) {
    // Block: 4×4 pixels; two big-endian bytes per index, bits 13:0 used.
    const std::uint32_t blocks_x = (width  + 3u) / 4u;
    const std::uint32_t blocks_y = (height + 3u) / 4u;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            for (std::uint32_t py = 0; py < 4u; ++py) {
                for (std::uint32_t px = 0; px < 4u; ++px) {
                    const std::uint32_t index =
                        static_cast<std::uint32_t>(read_be16(src)) & 0x3FFFu;
                    src += 2;
                    const std::uint32_t ix = bx * 4u + px;
                    const std::uint32_t iy = by * 4u + py;
                    if (ix < width && iy < height) {
                        const std::uint32_t entry_offset =
                            tlut_byte_offset + index * 2u;
                        out[iy * width + ix] = decode_tlut_entry(
                            tlut_bank + entry_offset, tlut_fmt);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// CMPR (S3TC / DXT1): 8×8 pixel blocks, each containing four 4×4 DXT1 sub-blocks.
// GX sub-block layout within the 8×8 block:
//   sub[0]=top-left, sub[1]=top-right, sub[2]=bottom-left, sub[3]=bottom-right.
// ---------------------------------------------------------------------------

// Decode one 4×4 DXT1 sub-block from 8 bytes into a 4×4 patch of `out`.
// (ox, oy) is the top-left corner of the patch in the full image.
static void decode_dxt1_subblock(
    const std::uint8_t* block,
    std::uint32_t ox,
    std::uint32_t oy,
    std::uint32_t width,
    std::uint32_t height,
    RGBA8* out) {
    if (ox >= width || oy >= height) {
        return;
    }
    const std::uint32_t columns = std::min(4u, width - ox);
    const std::uint32_t rows = std::min(4u, height - oy);
    const std::uint16_t c0 = read_be16(block);
    const std::uint16_t c1 = read_be16(block + 2);

    // Expand color0 and color1 from RGB565.
    auto expand565 = [](std::uint16_t v) -> RGBA8 {
        const std::uint8_t r5 = static_cast<std::uint8_t>((v >> 11) & 0x1Fu);
        const std::uint8_t g6 = static_cast<std::uint8_t>((v >>  5) & 0x3Fu);
        const std::uint8_t b5 = static_cast<std::uint8_t>( v        & 0x1Fu);
        return RGBA8{
            static_cast<std::uint8_t>((r5 << 3) | (r5 >> 2)),
            static_cast<std::uint8_t>((g6 << 2) | (g6 >> 4)),
            static_cast<std::uint8_t>((b5 << 3) | (b5 >> 2)),
            0xFFu
        };
    };

    const RGBA8 col0 = expand565(c0);
    const RGBA8 col1 = expand565(c1);

    RGBA8 palette[4];
    palette[0] = col0;
    palette[1] = col1;
    if (c0 > c1) {
        // Four-color mode (no transparency).
        palette[2] = RGBA8{
            cmpr_interpolated_channel(col0.r, col1.r),
            cmpr_interpolated_channel(col0.g, col1.g),
            cmpr_interpolated_channel(col0.b, col1.b),
            0xFFu
        };
        palette[3] = RGBA8{
            cmpr_interpolated_channel(col1.r, col0.r),
            cmpr_interpolated_channel(col1.g, col0.g),
            cmpr_interpolated_channel(col1.b, col0.b),
            0xFFu
        };
    } else {
        // Three-color mode: palette[3] has averaged RGB with zero alpha.
        palette[2] = RGBA8{
            static_cast<std::uint8_t>((col0.r + col1.r) / 2u),
            static_cast<std::uint8_t>((col0.g + col1.g) / 2u),
            static_cast<std::uint8_t>((col0.b + col1.b) / 2u),
            0xFFu
        };
        palette[3] = RGBA8{
            static_cast<std::uint8_t>((col0.r + col1.r) / 2u),
            static_cast<std::uint8_t>((col0.g + col1.g) / 2u),
            static_cast<std::uint8_t>((col0.b + col1.b) / 2u),
            0u
        };
    }

    // Each source byte holds one row, leftmost selector in its top two bits.
    // Keep row offsets as integers: advancing a pointer after the last clipped
    // row could otherwise form an address beyond the output's one-past end.
    std::size_t offset = static_cast<std::size_t>(oy) * width + ox;
    if (columns == 4u) {
        for (std::uint32_t py = 0; py < rows; ++py, offset += width) {
            RGBA8* row = out + offset;
            const std::uint8_t selectors = block[4u + py];
            row[0] = palette[(selectors >> 6u) & 3u];
            row[1] = palette[(selectors >> 4u) & 3u];
            row[2] = palette[(selectors >> 2u) & 3u];
            row[3] = palette[selectors & 3u];
        }
    } else {
        for (std::uint32_t py = 0; py < rows; ++py, offset += width) {
            RGBA8* row = out + offset;
            const std::uint8_t selectors = block[4u + py];
            for (std::uint32_t px = 0; px < columns; ++px) {
                row[px] = palette[(selectors >> (6u - px * 2u)) & 3u];
            }
        }
    }
}

}  // namespace

void detail::decode_cmpr_tiles(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    RGBA8* out) {
    // Outer loop: 8×8 GX macro-blocks.
    const std::uint32_t macro_x = (width  + 7u) / 8u;
    const std::uint32_t macro_y = (height + 7u) / 8u;
    // Sub-block layout within each 8×8 macro-block:
    // sub0=(0,0), sub1=(4,0), sub2=(0,4), sub3=(4,4).
    static constexpr std::uint32_t kSubOffX[4] = {0u, 4u, 0u, 4u};
    static constexpr std::uint32_t kSubOffY[4] = {0u, 0u, 4u, 4u};

    for (std::uint32_t by = 0; by < macro_y; ++by) {
        for (std::uint32_t bx = 0; bx < macro_x; ++bx) {
            for (std::uint32_t sub = 0; sub < 4u; ++sub) {
                const std::uint32_t ox = bx * 8u + kSubOffX[sub];
                const std::uint32_t oy = by * 8u + kSubOffY[sub];
                decode_dxt1_subblock(src, ox, oy, width, height, out);
                src += 8u;
            }
        }
    }
}

namespace {

// ---------------------------------------------------------------------------
// Compute raw byte size of a mip0 level for each format.
// ---------------------------------------------------------------------------

[[nodiscard]] static std::uint32_t tex_data_size(
    TexFormat fmt,
    std::uint32_t width,
    std::uint32_t height) {
    // Round up to block dimensions first.
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
    case TexFormat::C4:
        return blocks(width, 8u) * blocks(height, 8u) * 32u;
    case TexFormat::C8:
        return blocks(width, 8u) * blocks(height, 4u) * 32u;
    case TexFormat::C14X2:
        return blocks(width, 4u) * blocks(height, 4u) * 32u;
    case TexFormat::CMPR:
        // Each 8×8 macro-block = 4 DXT1 sub-blocks × 8 bytes = 32 bytes.
        return blocks(width, 8u) * blocks(height, 8u) * 32u;
    default:
        throw std::runtime_error("[TextureCache] tex_data_size: unknown TexFormat");
    }
}

// ---------------------------------------------------------------------------
// Create a non-shader-visible SRV for a buffer that holds texture data in a
// linear (row-major) layout.  The renderer uses CopyDescriptors to stage
// these into the frame's shader-visible ring before each draw.
//
// For an UPLOAD buffer holding RGBA8 pixels we use a Texture2D SRV with
// D3D12_SRV_DIMENSION_TEXTURE2D is not applicable to a BUFFER resource —
// instead we describe it as a raw buffer SRV and let the HLSL sampler
// interpret it.  For M4 the renderer samples from the SRV directly using
// Load(); a proper Texture2D DEFAULT resource is deferred to M6.
//
// Descriptor heap index is the caller-assigned slot; we emit to that slot.
// ---------------------------------------------------------------------------

} // anonymous namespace

std::size_t TextureCache::ContentKeyHasher::operator()(
    const ContentKey& key) const {
    std::uint64_t hash = fnv1a64(key.source.data(), key.source.size());
    const auto mix = [&hash](std::uint64_t value) {
        hash ^= value + 0x9E3779B97F4A7C15ull +
            (hash << 6u) + (hash >> 2u);
    };
    mix(fnv1a64(key.palette.data(), key.palette.size()));
    mix(key.width);
    mix(key.height);
    mix(key.format);
    mix(key.levels);
    mix(key.generated_mips);
    mix(key.tlut_format);
    return static_cast<std::size_t>(hash);
}

// ---------------------------------------------------------------------------
// TextureCache public API
// ---------------------------------------------------------------------------

std::uint8_t efb_copy_alias_texture_format(std::uint8_t copy_format) {
    return efb_copy_alias_texture_format_impl(copy_format);
}

std::uint32_t texture_guest_byte_size(
    TexFormat format, std::uint32_t width, std::uint32_t height,
    unsigned guest_levels) {
    if (width == 0u || height == 0u || width > 1024u || height > 1024u ||
        guest_levels == 0u || guest_levels > full_mip_chain_levels(width, height)) {
        throw std::runtime_error("[TextureCache] invalid guest texture extent");
    }
    std::uint64_t total = 0u;
    for (unsigned level = 0; level < guest_levels; ++level) {
        total += tex_data_size(format, std::max(1u, width >> level),
                               std::max(1u, height >> level));
    }
    if (total > UINT32_MAX) {
        throw std::runtime_error("[TextureCache] guest texture footprint overflow");
    }
    return static_cast<std::uint32_t>(total);
}

std::uint32_t efb_copy_guest_byte_size(const EfbCopyParams& params) {
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
    const std::uint32_t row_bytes = texture_guest_byte_size(format, width, 1u);
    const std::uint32_t rows = (height + block_height - 1u) / block_height;
    const std::uint64_t span = row_bytes +
        static_cast<std::uint64_t>(rows - 1u) * params.dest_stride;
    if (span > UINT32_MAX) {
        throw std::runtime_error("[TextureCache] texture-copy footprint overflow");
    }
    return static_cast<std::uint32_t>(span);
}

bool TextureCache::initialize(ID3D12Device* device) {
    device_ = device;
    next_srv_index_ = 0u;
    free_srv_indices_.clear();

    D3D12_DESCRIPTOR_HEAP_DESC dhd{};
    dhd.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    // Permanent null SRV is outside the allocatable texture slots.
    dhd.NumDescriptors = kSrvHeapCapacity + 1u;
    dhd.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE; // CPU-visible only
    dhd.NodeMask       = 0;
    if (FAILED(device->CreateDescriptorHeap(&dhd, IID_PPV_ARGS(&srv_heap_)))) {
        return false;
    }
    srv_stride_ = device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    null_srv_ = srv_heap_->GetCPUDescriptorHandleForHeapStart();
    null_srv_.ptr += static_cast<SIZE_T>(kSrvHeapCapacity) * srv_stride_;
    D3D12_SHADER_RESOURCE_VIEW_DESC null_desc{};
    null_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    null_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    null_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    null_desc.Texture2D.MipLevels = 1u;
    device->CreateShaderResourceView(nullptr, &null_desc, null_srv_);
    return true;
}

void TextureCache::begin_frame(
    unsigned frame_slot,
    unsigned frames_in_flight) {
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

    // RendererD3D12::begin_frame has already waited for this slot's fence,
    // so every resource retired during this slot's previous use is GPU-idle.
    retired_entries_[frame_slot].clear();
    upload_arenas_[frame_slot].active_chunk = 0;
    upload_arenas_[frame_slot].cursor = 0;
    current_frame_slot_ = frame_slot;
}

bool TextureCache::preallocate_upload_arenas(unsigned frames_in_flight) {
    if (device_ == nullptr || frames_in_flight == 0u) {
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

void TextureCache::retire(Entry&& entry) {
    if (!entry.texture) {
        return;
    }
    if (retired_entries_.empty()) {
        throw std::runtime_error(
            "[TextureCache] resource retired before begin_frame");
    }
    // Both allocations must succeed before publishing this descriptor as free
    // or transferring the resource out of its still-live cache entry.
    auto& retired = retired_entries_[current_frame_slot_];
    retired.emplace_back();
    try {
        free_srv_indices_.push_back(entry.handle.srv_index);
    } catch (...) {
        retired.pop_back();
        throw;
    }
    retired.back() = std::move(entry);
}

void TextureCache::release_upload_arenas() {
    for (UploadArenaFrame& frame : upload_arenas_) {
        for (UploadArenaChunk& chunk : frame.chunks) {
            if (chunk.resource && chunk.cpu_base != nullptr) {
                chunk.resource->Unmap(0, nullptr);
                chunk.cpu_base = nullptr;
            }
        }
    }
}

void TextureCache::shutdown() {
    if (decoded_content_cache_enabled()) {
        std::cerr << "[gx-content-cache] lookups=" << content_lookups_
                  << " exact-hits=" << content_hits_
                  << " decoded-misses=" << content_decoded_misses_
                  << " retained-bytes=" << content_retained_bytes_
                  << " evicted-entries=" << content_evicted_entries_
                  << " evicted-bytes=" << content_evicted_bytes_
                  << " live-entries=" << content_entries_.size()
                  << " live-bytes=" << content_bytes_ << '\n';
    }
    entries_.clear();
    content_entries_.clear();
    content_lru_.clear();
    content_bytes_ = 0;
    content_lookups_ = 0;
    content_hits_ = 0;
    content_decoded_misses_ = 0;
    content_retained_bytes_ = 0;
    content_evicted_bytes_ = 0;
    content_evicted_entries_ = 0;
    efb_aliases_.clear();
    retired_entries_.clear();
    release_upload_arenas();
    upload_arenas_.clear();
    level_pixels_scratch_.clear();
    level_offsets_scratch_.clear();
    level_pitches_scratch_.clear();
    srv_heap_.Reset();
    srv_stride_ = 0u;
    null_srv_ = {};
    device_.Reset();
    next_srv_index_ = 0u;
    free_srv_indices_.clear();
}

void TextureCache::append_upload_arena_chunk(
    UploadArenaFrame& frame,
    UINT64 min_size) {
    const UINT64 chunk_size = align_up(
        std::max(kUploadArenaChunkBytes, min_size),
        D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    UploadArenaChunk chunk{};
    chunk.size = chunk_size;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width            = chunk_size;
    rd.Height           = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels        = 1;
    rd.Format           = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    d3d_check(device_->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(&chunk.resource)),
        "create texture upload arena");
    D3D12_RANGE no_read{0, 0};
    void* mapped = nullptr;
    d3d_check(
        chunk.resource->Map(0, &no_read, &mapped),
        "map texture upload arena");
    chunk.cpu_base = static_cast<std::uint8_t*>(mapped);
    try {
        frame.chunks.push_back(std::move(chunk));
    } catch (...) {
        chunk.resource->Unmap(0, nullptr);
        throw;
    }
}

TextureCache::UploadAllocation TextureCache::allocate_upload_bytes(
    UINT64 size,
    UINT64 alignment) {
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

        UploadArenaChunk& chunk = frame.chunks[frame.active_chunk];
        const UINT64 offset = align_up(frame.cursor, alignment);
        if (offset + size <= chunk.size) {
            frame.cursor = offset + size;
            return {chunk.resource.Get(), chunk.cpu_base, offset};
        }

        ++frame.active_chunk;
        frame.cursor = 0;
    }
}

ID3D12DescriptorHeap* TextureCache::srv_heap() const {
    return srv_heap_.Get();
}

std::uint32_t TextureCache::allocate_srv_index() {
    if (!free_srv_indices_.empty()) {
        const std::uint32_t index = free_srv_indices_.back();
        free_srv_indices_.pop_back();
        return index;
    }
    if (next_srv_index_ >= kSrvHeapCapacity) {
        throw std::runtime_error("[TextureCache] SRV heap exhausted");
    }
    return next_srv_index_++;
}

// ---------------------------------------------------------------------------
// load_tlut — BP 0x64 / 0x65
// ---------------------------------------------------------------------------

bool TextureCache::load_tlut(
    std::uint32_t src_reg,
    std::uint32_t dest_reg,
    GuestMemoryV1* memory) {
    const TlutTransfer decoded = decode_wii_tlut_transfer(src_reg, dest_reg);
    const std::uint32_t src_addr = decoded.source_address;
    const std::uint32_t dest_byte_offset = decoded.destination_offset;
    const std::uint32_t transfer = decoded.byte_count;
    if (transfer == 0u) {
        return false;
    }
    if (dest_byte_offset + transfer > kTlutBankBytes) {
        throw std::runtime_error(
            "[TextureCache] load_tlut: dest slot overflows TLUT bank");
    }

    const std::byte* src_ptr =
        resolve_guest_tex(memory, src_addr, transfer);
    if (dependency_event_sink_ != nullptr) {
        dependency_event_sink_->guest_read(
            DependencyReadSource::Tlut, src_addr,
            std::span<const std::byte>(src_ptr, transfer));
    }
    // An identical reload changes no decoded palette. Keep dependency reads
    // observable, but avoid retiring/redecoding every texture using this slot.
    if (std::memcmp(tlut_bank_ + dest_byte_offset, src_ptr, transfer) == 0) {
        return false;
    }
    // One transfer can touch several slots, and a C14X2 palette can span
    // many slots even when its first slot is outside the transfer. Compare
    // each overlapping palette BEFORE replacing the bank: a changed transfer
    // can still leave this texture's complete palette unchanged (e.g. C4
    // consumes only the first 32 bytes of a 512-byte C8-slot reload).
    const std::uint32_t transfer_end = dest_byte_offset + transfer;
    for (auto it = entries_.begin(); it != entries_.end(); ) {
        std::uint32_t palette_bytes = 0u;
        switch (static_cast<TexFormat>(it->first.format)) {
        case TexFormat::C4: palette_bytes = 16u * 2u; break;
        case TexFormat::C8: palette_bytes = 256u * 2u; break;
        case TexFormat::C14X2: palette_bytes = 16384u * 2u; break;
        default: break;
        }
        const std::uint32_t palette_start =
            static_cast<std::uint32_t>(it->first.tlut_offset) * 512u;
        const bool overlaps = palette_bytes != 0u &&
            palette_start < transfer_end &&
            dest_byte_offset < palette_start + palette_bytes;
        bool changed = false;
        if (overlaps) {
            const std::uint32_t begin = std::max(palette_start, dest_byte_offset);
            const std::uint32_t end = std::min(palette_start + palette_bytes, transfer_end);
            changed = std::memcmp(tlut_bank_ + begin,
                src_ptr + (begin - dest_byte_offset), end - begin) != 0;
        }
        if (changed) {
            retire(std::move(it->second));
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
    std::memcpy(tlut_bank_ + dest_byte_offset, src_ptr, transfer);
    return true;
}

// ---------------------------------------------------------------------------
// register_efb_copy
// ---------------------------------------------------------------------------

bool TextureCache::register_efb_copy(
    std::uint32_t guest_addr,
    const EfbCopyParams& params,
    ComPtr<ID3D12Resource> texture) {
    // Validate before evicting a still-owned alias or consuming an SRV slot.
    if (!device_ || !srv_heap_ || !texture) {
        throw std::runtime_error("[TextureCache] invalid EFB alias resource");
    }
    const std::uint32_t guest_byte_size = efb_copy_guest_byte_size(params);
    if (static_cast<std::uint64_t>(guest_addr) + guest_byte_size > (1ull << 32u)) {
        throw std::runtime_error("[TextureCache] EFB alias address overflow");
    }
    const D3D12_RESOURCE_DESC resource_desc = texture->GetDesc();
    if (resource_desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
        resource_desc.Format != DXGI_FORMAT_R8G8B8A8_UNORM ||
        resource_desc.DepthOrArraySize != 1u || resource_desc.SampleDesc.Count != 1u ||
        resource_desc.Width == 0u || resource_desc.Width > UINT16_MAX ||
        resource_desc.Height == 0u || resource_desc.Height > UINT16_MAX) {
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
        guest_addr,
        alias_width,
        alias_height,
        efb_copy_alias_texture_format(params.target_format),
        {},
    };

    // Called on EVERY copy (the renderer keys dest textures by address AND
    // format, so one address can alternate resources).  Remember exact
    // same-resource aliases here, but do not return until overlap eviction has
    // had a chance to retire stale decoded textures and older EFB aliases.
    const auto current_alias_it = efb_aliases_.find(alias_key);
    const bool current_alias_same_resource =
        current_alias_it != efb_aliases_.end() &&
        current_alias_it->second.texture.Get() == texture.Get();
    if (debug_capture_frame_ != 0u) {
        std::cerr << "[cap-alias-copy] frame=" << debug_capture_frame_
                  << " addr=0x" << std::hex << guest_addr
                  << " resource=" << texture.Get() << std::dec
                  << " bytes=" << guest_byte_size
                  << " wh=" << alias_width << 'x' << alias_height
                  << " format=" << static_cast<unsigned>(alias_key.format)
                  << " same-resource=" << current_alias_same_resource << '\n';
    }

    const auto overlaps = [](std::uint64_t a_begin, std::uint64_t a_end,
                             std::uint64_t b_begin, std::uint64_t b_end) {
        return a_begin < b_end && b_begin < a_end;
    };

    const std::uint64_t new_begin = guest_addr;
    const std::uint64_t new_end =
        new_begin + guest_byte_size;

    // Evict decoded entries and older EFB aliases whose guest byte ranges
    // overlap the newly captured copy. Galaxy reuses capture work buffers for
    // glass/blur effects; exact-address eviction leaves stale GPU aliases.
    bool evicted_overlap = false;
    for (auto it = entries_.begin(); it != entries_.end(); ) {
        const std::uint64_t entry_begin = it->first.guest_addr;
        const std::uint64_t entry_end =
            entry_begin + it->second.handle.guest_byte_size;
        if (overlaps(new_begin, new_end, entry_begin, entry_end)) {
            retire(std::move(it->second));
            it = entries_.erase(it);
            evicted_overlap = true;
        } else {
            ++it;
        }
    }
    for (auto it = efb_aliases_.begin(); it != efb_aliases_.end(); ) {
        const std::uint64_t alias_begin = it->first.guest_addr;
        const std::uint64_t alias_end =
            alias_begin + it->second.handle.guest_byte_size;
        const bool is_current_same_resource =
            it->first == alias_key &&
            it->second.texture.Get() == texture.Get();
        if (!is_current_same_resource &&
            overlaps(new_begin, new_end, alias_begin, alias_end)) {
            if (dependency_event_sink_ != nullptr) {
                dependency_event_sink_->alias_retire(
                    DependencyAliasKey{it->first.guest_addr,
                                       it->first.width, it->first.height,
                                       it->first.format},
                    reinterpret_cast<std::uintptr_t>(
                        it->second.texture.Get()));
            }
            retire(std::move(it->second));
            it = efb_aliases_.erase(it);
            evicted_overlap = true;
        } else {
            ++it;
        }
    }

    if (current_alias_same_resource) {
        const bool footprint_changed =
            current_alias_it->second.handle.guest_byte_size != guest_byte_size;
        current_alias_it->second.handle.guest_byte_size = guest_byte_size;
        if (dependency_event_sink_ != nullptr) {
            dependency_event_sink_->alias_copy(
                DependencyAliasKey{alias_key.guest_addr, alias_key.width,
                                   alias_key.height, alias_key.format},
                reinterpret_cast<std::uintptr_t>(texture.Get()));
        }
        return evicted_overlap || footprint_changed;
    }

    const UINT srv_idx = allocate_srv_index();

    // Describe as a 2D texture SRV — the resource passed in is a Texture2D
    // (created by the EFB copy path in RendererD3D12).
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format                        = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels           = 1;
    srv_desc.Texture2D.MostDetailedMip     = 0;
    srv_desc.Texture2D.PlaneSlice          = 0;
    srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;

    const UINT increment = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu =
        srv_heap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(srv_idx) * increment;

    device_->CreateShaderResourceView(texture.Get(), &srv_desc, cpu);

    D3D12_RESOURCE_DESC rd = texture->GetDesc();
    TextureHandle handle{};
    handle.resource      = texture.Get();
    handle.srv_index     = srv_idx;
    handle.width         = static_cast<std::uint16_t>(rd.Width);
    handle.height        = static_cast<std::uint16_t>(rd.Height);
    handle.mip_levels    = 1u;
    handle.from_efb_copy = true;
    handle.generated_mips = false;
    handle.guest_byte_size = guest_byte_size;

    Entry entry{};
    entry.texture = std::move(texture);
    entry.handle  = handle;

    auto alias_it = efb_aliases_.find(alias_key);
    if (alias_it != efb_aliases_.end()) {
        if (dependency_event_sink_ != nullptr) {
            dependency_event_sink_->alias_retire(
                DependencyAliasKey{alias_it->first.guest_addr,
                                   alias_it->first.width,
                                   alias_it->first.height,
                                   alias_it->first.format},
                reinterpret_cast<std::uintptr_t>(
                    alias_it->second.texture.Get()));
        }
        retire(std::move(alias_it->second));
        alias_it->second = std::move(entry);
    } else {
        efb_aliases_.emplace(alias_key, std::move(entry));
    }
    if (dependency_event_sink_ != nullptr) {
        dependency_event_sink_->alias_copy(
            DependencyAliasKey{alias_key.guest_addr, alias_key.width,
                               alias_key.height, alias_key.format},
            reinterpret_cast<std::uintptr_t>(handle.resource));
    }
    return true;
}

// ---------------------------------------------------------------------------
// invalidate_all
// ---------------------------------------------------------------------------

void TextureCache::invalidate_all() {
    for (auto& [key, entry] : entries_) {
        (void)key;
        retire(std::move(entry));
    }
    entries_.clear();
    // EFB aliases are NOT cleared here — their content lives on the GPU.
    // SRV slots for retired decoded textures are returned to the free list.
}

// ---------------------------------------------------------------------------
// get — primary texture fetch
// ---------------------------------------------------------------------------

std::size_t TextureCache::invalidate_guest_range(
    std::uint32_t guest_addr,
    std::uint32_t size) {
    const GuestRange range{guest_addr, size};
    return invalidate_guest_ranges(std::span<const GuestRange>(&range, 1u));
}

std::size_t TextureCache::invalidate_guest_ranges(
    std::span<const GuestRange> sorted_coalesced_ranges) {
    if (sorted_coalesced_ranges.empty() ||
        (entries_.empty() && efb_aliases_.empty())) {
        return 0u;
    }

    const bool trace_dirty_detail =
        debug_capture_frame_ != 0u || trace_texture_dirty_detail_enabled();


    std::size_t evicted = 0;
    for (auto it = entries_.begin(); it != entries_.end(); ) {
        const std::uint64_t tex_begin = it->first.guest_addr;
        const std::uint64_t tex_end = tex_begin + it->second.handle.guest_byte_size;
        const GuestRange* dirty = first_overlapping_dirty_range(
            sorted_coalesced_ranges,
            tex_begin,
            tex_end);
        if (dirty != nullptr) {
            const std::uint64_t dirty_begin = dirty->guest_addr;
            const std::uint64_t dirty_end = dirty_begin + dirty->size;
            if (trace_dirty_detail) {
                std::cerr << "[gx-texture-dirty-detail] kind=decoded"
                          << " dirty=0x" << std::hex << std::uppercase
                          << static_cast<std::uint32_t>(dirty_begin)
                          << "..0x" << static_cast<std::uint32_t>(dirty_end)
                          << " tex=0x" << static_cast<std::uint32_t>(tex_begin)
                          << "..0x" << static_cast<std::uint32_t>(tex_end)
                          << std::dec << std::nouppercase
                          << " size=" << dirty->size
                          << " texsize=" << (tex_end - tex_begin)
                          << " fmt=" << static_cast<unsigned>(it->first.format)
                          << " wh=" << it->first.width << 'x'
                          << it->first.height
                          << " levels=" << static_cast<unsigned>(it->first.levels)
                          << " genmips="
                          << static_cast<unsigned>(it->first.generated_mips)
                          << '\n';
            }
            retire(std::move(it->second));
            it = entries_.erase(it);
            ++evicted;
        } else {
            ++it;
        }
    }
    for (auto it = efb_aliases_.begin(); it != efb_aliases_.end(); ) {
        const std::uint64_t alias_begin = it->first.guest_addr;
        const std::uint64_t alias_end =
            alias_begin + it->second.handle.guest_byte_size;
        const GuestRange* dirty = first_overlapping_dirty_range(
            sorted_coalesced_ranges,
            alias_begin,
            alias_end);
        if (dirty != nullptr) {
            const std::uint64_t dirty_begin = dirty->guest_addr;
            const std::uint64_t dirty_end = dirty_begin + dirty->size;
            if (trace_dirty_detail) {
                std::cerr << "[gx-texture-dirty-detail] kind=efb-alias"
                          << " frame=" << debug_capture_frame_
                          << " resource=" << it->second.texture.Get()
                          << " dirty=0x" << std::hex << std::uppercase
                          << static_cast<std::uint32_t>(dirty_begin)
                          << "..0x" << static_cast<std::uint32_t>(dirty_end)
                          << " alias=0x" << static_cast<std::uint32_t>(alias_begin)
                          << "..0x" << static_cast<std::uint32_t>(alias_end)
                          << std::dec << std::nouppercase
                          << " size=" << dirty->size
                          << " aliassize=" << (alias_end - alias_begin)
                          << " fmt=" << static_cast<unsigned>(it->first.format)
                          << " wh=" << it->first.width << 'x'
                          << it->first.height
                          << '\n';
            }
            if (dependency_event_sink_ != nullptr) {
                dependency_event_sink_->alias_retire(
                    DependencyAliasKey{it->first.guest_addr,
                                       it->first.width, it->first.height,
                                       it->first.format},
                    reinterpret_cast<std::uintptr_t>(
                        it->second.texture.Get()));
            }
            retire(std::move(it->second));
            it = efb_aliases_.erase(it);
            ++evicted;
        } else {
            ++it;
        }
    }
    return evicted;
}

TextureHandle TextureCache::get(
    const TexImage& image,
    const TexMode& mode,
    const TlutRef& tlut,
    GuestMemoryV1* memory) {
    const bool trace_stalls = trace_gx_stalls_enabled();
    const auto get_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    if (debug_invalidate_every_frame) {
        invalidate_all();
    }
    const auto invalidate_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // 2. Build cache key.  The mip chain length and source are part of the key:
    // SMG hand-authors lower mip levels (fog/lava/water tinting — Dolphin PRs
    // #6118/#6875), so game-authored mips must never alias the generated native
    // mip chains used for quality filtering.
    unsigned levels = 1;
    unsigned guest_levels = 1;
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
            guest_levels = levels;
        } else if (should_generate_native_mips(image, mode)) {
            levels = full_chain;
            guest_levels = 1;
            generated_mips = levels > 1u;
        }
    }
    const auto config_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    Key key{};
    key.guest_addr  = image.guest_addr;
    key.width       = image.width;
    key.height      = image.height;
    key.format      = static_cast<std::uint8_t>(image.format);
    // Only indexed formats consume a TLUT. These BP registers may retain a
    // previous texture's palette, so keying direct formats on them created
    // duplicate decodes/resources for identical guest images.
    if (image.format == TexFormat::C4 || image.format == TexFormat::C8 ||
        image.format == TexFormat::C14X2) {
        key.tlut_format = static_cast<std::uint8_t>(tlut.format);
        key.tlut_offset = tlut.tmem_offset;
    }
    key.levels      = static_cast<std::uint8_t>(levels);
    key.generated_mips = generated_mips ? 1u : 0u;
    const std::uint32_t total_guest_size =
        texture_guest_byte_size(image.format, image.width, image.height, guest_levels);
    if (static_cast<std::uint64_t>(image.guest_addr) + total_guest_size > (1ull << 32u)) {
        throw std::runtime_error("[TextureCache] guest texture address overflow");
    }
    const auto key_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // 3. EFB alias hit.  The full descriptor match prevents a stale
    // full-screen capture from being sampled by a later small texture that
    // reuses the same heap address.
    {
        const EfbAliasKey alias_key{
            image.guest_addr,
            image.width,
            image.height,
            static_cast<std::uint8_t>(image.format),
            {},
        };
        const auto alias_it = efb_aliases_.find(alias_key);
        if (alias_it != efb_aliases_.end()) {
            if (dependency_event_sink_ != nullptr) {
                dependency_event_sink_->alias_bind(
                    DependencyAliasKey{alias_key.guest_addr,
                                       alias_key.width, alias_key.height,
                                       alias_key.format},
                    reinterpret_cast<std::uintptr_t>(
                        alias_it->second.handle.resource));
            }
            return alias_it->second.handle;
        }
    }
    const auto alias_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // 4. Cache hit.
    {
        const auto it = entries_.find(key);
        if (it != entries_.end()) {
            return it->second.handle;
        }
    }
    const auto cache_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // 5. Decode all guest mip levels.  Guest layout: authored levels are
    // contiguous and block-aligned; the whole authored chain is resolved as one
    // guest range (hard-fail unchanged). Native enhanced mips read mip0 only
    // and generate lower levels from the decoded RGBA base.
    const std::uint32_t w = image.width;
    const std::uint32_t h = image.height;

    // TLUT byte offset in our flat bank: tmem_offset encodes the bank slot.
    // Hardware TLUT slots: slot N starts at TMEM offset N*256 entries×2 bytes =
    // N*512 bytes.  tlut.tmem_offset is in units of 256 entries.
    const std::uint32_t tlut_byte_offset =
        static_cast<std::uint32_t>(tlut.tmem_offset) * 512u;

    // Validate before either content-key reads or the unconditionally used
    // CI decoder. Invalid caller descriptors must never become host reads.
    std::uint32_t required_palette_bytes = 0u;
    switch (image.format) {
    case TexFormat::C4: required_palette_bytes = 32u; break;
    case TexFormat::C8: required_palette_bytes = 512u; break;
    case TexFormat::C14X2: required_palette_bytes = 32768u; break;
    default: break;
    }
    if (required_palette_bytes != 0u &&
        (tlut_byte_offset > kTlutBankBytes ||
         required_palette_bytes > kTlutBankBytes - tlut_byte_offset)) {
        throw std::runtime_error("[TextureCache] palette exceeds TMEM");
    }

    const std::byte* guest_bytes =
        resolve_guest_tex(memory, image.guest_addr, total_guest_size);
    if (dependency_event_sink_ != nullptr) {
        dependency_event_sink_->guest_read(
            DependencyReadSource::Texture, image.guest_addr,
            std::span<const std::byte>(guest_bytes, total_guest_size));
    }
    const auto* src = reinterpret_cast<const std::uint8_t*>(guest_bytes);
    std::optional<ContentKey> content_key;
    if (decoded_content_cache_enabled() &&
        !debug_invalidate_every_frame && total_guest_size != 0u &&
        total_guest_size <= kContentCacheBudgetBytes) {
        auto& content = content_key.emplace();
        content.width = image.width;
        content.height = image.height;
        content.format = static_cast<std::uint8_t>(image.format);
        content.levels = static_cast<std::uint8_t>(levels);
        content.generated_mips = generated_mips ? 1u : 0u;
        content.source.assign(src, src + total_guest_size);

        // Match Aurora's content-key rule: palette bytes are part of the
        // identity, so a TLUT reload cannot return stale decoded colors.
        std::uint32_t palette_bytes = 0u;
        switch (image.format) {
        case TexFormat::C4: palette_bytes = 16u * 2u; break;
        case TexFormat::C8: palette_bytes = 256u * 2u; break;
        case TexFormat::C14X2: palette_bytes = 16384u * 2u; break;
        default: break;
        }
        if (palette_bytes != 0u) {
            if (tlut_byte_offset + palette_bytes > kTlutBankBytes) {
                // Preserve the existing decoder's behavior for malformed
                // TLUT references; no cache entry may alias unknown bytes.
                content_key.reset();
            } else {
                content.tlut_format =
                    static_cast<std::uint8_t>(tlut.format);
                content.palette.assign(
                    tlut_bank_ + tlut_byte_offset,
                    tlut_bank_ + tlut_byte_offset + palette_bytes);
            }
        }
        if (content_key.has_value()) {
            // Decode from the exact bytes used to identify this resource.
            // The non-opt-in path retains its existing guest-memory access.
            src = content_key->source.data();
            ++content_lookups_;
            if (auto it = content_entries_.find(*content_key);
                it != content_entries_.end()) {
                ++content_hits_;
                content_lru_.splice(
                    content_lru_.begin(), content_lru_, it->second.lru);
                it->second.lru = content_lru_.begin();
                const UINT srv_idx = allocate_srv_index();
                D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
                srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Shader4ComponentMapping =
                    D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srv_desc.Texture2D.MipLevels = static_cast<UINT>(levels);
                const UINT increment =
                    device_->GetDescriptorHandleIncrementSize(
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                D3D12_CPU_DESCRIPTOR_HANDLE cpu =
                    srv_heap_->GetCPUDescriptorHandleForHeapStart();
                cpu.ptr += static_cast<SIZE_T>(srv_idx) * increment;
                device_->CreateShaderResourceView(
                    it->second.texture.Get(), &srv_desc, cpu);

                Entry entry{};
                entry.texture = it->second.texture;
                entry.handle.resource = entry.texture.Get();
                entry.handle.srv_index = srv_idx;
                entry.handle.width = static_cast<std::uint16_t>(w);
                entry.handle.height = static_cast<std::uint16_t>(h);
                entry.handle.mip_levels = static_cast<std::uint8_t>(levels);
                entry.handle.from_efb_copy = false;
                entry.handle.generated_mips = generated_mips;
                entry.handle.guest_byte_size = total_guest_size;
                const TextureHandle handle = entry.handle;
                entries_.emplace(key, std::move(entry));
                return handle;
            }
        }
    }
    const auto resolve_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    const auto miss_start = std::chrono::steady_clock::now();
    if (trace_stalls) {
        const std::uint64_t pre_miss_us =
            elapsed_us(get_start, miss_start);
        if (pre_miss_us >= trace_gx_stall_threshold_us()) {
            const bool fast_available =
                resolve_guest_fast(memory, image.guest_addr, total_guest_size) != nullptr;
            std::cerr << "[gx-texture-premiss] addr=0x" << std::hex
                      << image.guest_addr << std::dec
                      << " size=" << total_guest_size
                      << " regions=" << (memory ? memory->region_count : 0u)
                      << " fast=" << (fast_available ? 1u : 0u)
                      << " invalidate-us="
                      << elapsed_us(get_start, invalidate_end)
                      << " config-us="
                      << elapsed_us(invalidate_end, config_end)
                      << " key-us=" << elapsed_us(config_end, key_end)
                      << " alias-us=" << elapsed_us(key_end, alias_end)
                      << " cache-us=" << elapsed_us(alias_end, cache_end)
                      << " resolve-us=" << elapsed_us(cache_end, resolve_end)
                      << " total-us=" << pre_miss_us
                      << '\n';
        }
    }
    if (level_pixels_scratch_.size() < levels) {
        level_pixels_scratch_.resize(levels);
    }
    auto& level_pixels = level_pixels_scratch_;
    for (unsigned l = 0; l < guest_levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, w >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, h >> l);
        level_pixels[l].resize(
            static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh));
        RGBA8* out = level_pixels[l].data();

        switch (image.format) {
        case TexFormat::I4:
        case TexFormat::I8:
        case TexFormat::IA4:
        case TexFormat::IA8:
            detail::decode_intensity_tiles(image.format, src, lw, lh, out);
            break;
        case TexFormat::RGB565:
            decode_RGB565(src, lw, lh, out);
            break;
        case TexFormat::RGB5A3:
            decode_RGB5A3(src, lw, lh, out);
            break;
        case TexFormat::RGBA8:
            detail::decode_rgba8_tiles(src, lw, lh, out);
            break;
        case TexFormat::C4:
        case TexFormat::C8:
            detail::decode_small_index_tiles(image.format, src, lw, lh,
                tlut_bank_ + tlut_byte_offset, tlut.format, out);
            break;
        case TexFormat::C14X2:
            decode_C14X2(src, lw, lh,
                tlut_bank_, tlut_byte_offset, tlut.format, out);
            break;
        case TexFormat::CMPR:
            detail::decode_cmpr_tiles(src, lw, lh, out);
            break;
        default:
            throw std::runtime_error(
                "[TextureCache] get: unhandled TexFormat");
        }
        src += tex_data_size(image.format, lw, lh);
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
    const auto decode_end = std::chrono::steady_clock::now();

    // 5. Upload to D3D12: one staging buffer holding every level at a
    // 512-aligned placed footprint (row pitch 256-aligned) → DEFAULT-heap
    // Texture2D with MipLevels=N via the frame command list.  The shader
    // samples Texture2D, so the bound resource MUST be a real texture — a
    // buffer SRV in a Texture2D slot is undefined behavior (and hung the
    // GPU in boot87).
    if (upload_list_ == nullptr) {
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
    UINT64 staging_size = 0;
    for (unsigned l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, w >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, h >> l);
        staging_size =
            (staging_size + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u) &
            ~static_cast<UINT64>(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1u);
        level_offset[l] = staging_size;
        level_pitch[l] =
            (lw * 4u + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u) &
            ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1u);
        staging_size += static_cast<UINT64>(level_pitch[l]) * lh;
    }

    const UploadAllocation staging = allocate_upload_bytes(
        staging_size,
        D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    {
        if (staging.cpu_base == nullptr) {
            throw std::runtime_error(
                "[TextureCache] upload arena is not mapped");
        }
        auto* dst_base = staging.cpu_base + staging.offset;
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
    const auto staging_end = std::chrono::steady_clock::now();

    ComPtr<ID3D12Resource> resource;
    {
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension        = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width            = w;
        rd.Height           = h;
        rd.DepthOrArraySize = 1;
        rd.MipLevels        = static_cast<UINT16>(levels);
        rd.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        rd.SampleDesc.Count = 1;
        rd.Layout           = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        d3d_check(device_->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(&resource)),
            "create default texture");
    }
    const auto resource_end = std::chrono::steady_clock::now();

    for (unsigned l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, w >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, h >> l);

        D3D12_TEXTURE_COPY_LOCATION copy_dst{};
        copy_dst.pResource        = resource.Get();
        copy_dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        copy_dst.SubresourceIndex = l;

        D3D12_TEXTURE_COPY_LOCATION copy_src{};
        copy_src.pResource                          = staging.resource;
        copy_src.Type                               = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        copy_src.PlacedFootprint.Offset             = staging.offset + level_offset[l];
        copy_src.PlacedFootprint.Footprint.Format   = DXGI_FORMAT_R8G8B8A8_UNORM;
        copy_src.PlacedFootprint.Footprint.Width    = lw;
        copy_src.PlacedFootprint.Footprint.Height   = lh;
        copy_src.PlacedFootprint.Footprint.Depth    = 1;
        copy_src.PlacedFootprint.Footprint.RowPitch = level_pitch[l];

        upload_list_->CopyTextureRegion(&copy_dst, 0, 0, 0, &copy_src, nullptr);
    }

    {
        D3D12_RESOURCE_BARRIER bar{};
        bar.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        bar.Transition.pResource   = resource.Get();
        bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        bar.Transition.StateAfter  = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        upload_list_->ResourceBarrier(1, &bar);
    }

    const UINT srv_idx = allocate_srv_index();

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
    srv_desc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
    srv_desc.ViewDimension             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Texture2D.MipLevels       = static_cast<UINT>(levels);

    const UINT increment = device_->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu =
        srv_heap_->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(srv_idx) * increment;

    device_->CreateShaderResourceView(resource.Get(), &srv_desc, cpu);
    const auto upload_record_end = std::chrono::steady_clock::now();

    const auto entry_build_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    TextureHandle handle{};
    handle.resource      = resource.Get();
    handle.srv_index     = srv_idx;
    handle.width         = static_cast<std::uint16_t>(w);
    handle.height        = static_cast<std::uint16_t>(h);
    handle.mip_levels    = static_cast<std::uint8_t>(levels);
    handle.from_efb_copy = false;
    handle.generated_mips = generated_mips;
    handle.guest_byte_size = total_guest_size;

    Entry entry{};
    entry.texture = std::move(resource);
    entry.handle  = handle;
    const auto entry_build_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // Bring-up: log each new texture decode with a content fingerprint.
    const auto tex_log_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    static int s_tex_logs = 0;
    if (trace_texture_uploads_enabled() && s_tex_logs < 24) {
        ++s_tex_logs;
        std::uint32_t nonzero = 0;
        const std::vector<RGBA8>& base = level_pixels[0];
        const std::uint32_t total = std::min<std::uint32_t>(
            static_cast<std::uint32_t>(base.size()), 4096);
        for (std::uint32_t i = 0; i < total; ++i) {
            const auto* px = reinterpret_cast<const std::uint8_t*>(&base[i]);
            if (px[0] | px[1] | px[2] | px[3]) ++nonzero;
        }
        std::fprintf(stderr,
            "[tex] addr=%08X fmt=%u %ux%u mips=%u native=%u tlut=%u/%u srv=%u "
            "nonzero=%u/%u px0=%08X\n",
            image.guest_addr, static_cast<unsigned>(image.format), w, h,
            levels,
            generated_mips ? 1u : 0u,
            static_cast<unsigned>(tlut.format), tlut.tmem_offset, srv_idx,
            nonzero, total,
            *reinterpret_cast<const std::uint32_t*>(&base[0]));
    }
    const auto tex_log_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // Opt-in decoded-texture dump: GALAXY_GX_CAPTURE_TEXDIR=<dir> writes
    // every newly decoded mip0 as <dir>/tex_<addr>_f<fmt>.ppm (RGB) and
    // .pgm (alpha).  Zero cost unless the env var is set.
    const auto tex_dump_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    static const std::string tex_dump_dir = [] {
        char value[MAX_PATH]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value),
                     "GALAXY_GX_CAPTURE_TEXDIR") != 0 || length <= 1) {
            return std::string{};
        }
        return std::string(value);
    }();
    if (!tex_dump_dir.empty()) {
        const std::vector<RGBA8>& base = level_pixels[0];
        char path[MAX_PATH]{};
        std::snprintf(path, sizeof(path), "%s/tex_%08X_f%u.ppm",
                      tex_dump_dir.c_str(), image.guest_addr,
                      static_cast<unsigned>(image.format));
        FILE* f = nullptr;
        if (fopen_s(&f, path, "wb") == 0 && f != nullptr) {
            std::fprintf(f, "P6\n%u %u\n255\n", w, h);
            for (std::uint32_t i = 0; i < w * h; ++i) {
                const auto* px =
                    reinterpret_cast<const std::uint8_t*>(&base[i]);
                std::fwrite(px, 1, 3, f);
            }
            std::fclose(f);
        }
        std::snprintf(path, sizeof(path), "%s/tex_%08X_f%u_a.pgm",
                      tex_dump_dir.c_str(), image.guest_addr,
                      static_cast<unsigned>(image.format));
        if (fopen_s(&f, path, "wb") == 0 && f != nullptr) {
            std::fprintf(f, "P5\n%u %u\n255\n", w, h);
            for (std::uint32_t i = 0; i < w * h; ++i) {
                const auto* px =
                    reinterpret_cast<const std::uint8_t*>(&base[i]);
                std::fwrite(px + 3, 1, 1, f);
            }
            std::fclose(f);
        }
    }
    const auto tex_dump_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    const TextureHandle result = entry.handle;
    const auto emplace_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    entries_.emplace(key, std::move(entry));
    if (content_key.has_value()) {
        ++content_decoded_misses_;
        // The key lives in both the map and LRU list; count both copies.
        std::uint64_t retained_bytes = 2u *
            (content_key->source.size() + content_key->palette.size());
        for (unsigned l = 0; l < levels; ++l) {
            retained_bytes +=
                static_cast<std::uint64_t>(
                    std::max<std::uint32_t>(1u, w >> l)) *
                std::max<std::uint32_t>(1u, h >> l) * sizeof(RGBA8);
        }
        if (retained_bytes <= kContentCacheBudgetBytes) {
            content_lru_.push_front(*content_key);
            const auto [it, inserted] = content_entries_.emplace(
                std::move(*content_key),
                ContentEntry{
                    entries_.find(key)->second.texture,
                    retained_bytes,
                    content_lru_.begin()});
            if (inserted) {
                content_bytes_ += retained_bytes;
                content_retained_bytes_ += retained_bytes;
            } else {
                content_lru_.pop_front();
                content_lru_.splice(
                    content_lru_.begin(), content_lru_, it->second.lru);
                it->second.lru = content_lru_.begin();
            }
            while ((content_bytes_ > kContentCacheBudgetBytes ||
                    content_entries_.size() > kContentCacheMaxEntries) &&
                   !content_lru_.empty()) {
                const auto oldest = content_entries_.find(
                    content_lru_.back());
                if (oldest != content_entries_.end()) {
                    content_bytes_ -= oldest->second.bytes;
                    content_evicted_bytes_ += oldest->second.bytes;
                    ++content_evicted_entries_;
                    content_entries_.erase(oldest);
                }
                content_lru_.pop_back();
            }
        }
    }
    const auto emplace_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::uint64_t total_us = elapsed_us(miss_start, upload_record_end);
    if (trace_stalls) {
        const std::uint64_t post_record_us =
            elapsed_us(upload_record_end, emplace_end);
        if (post_record_us >= trace_gx_stall_threshold_us()) {
            std::cerr << "[gx-texture-postrecord] addr=0x" << std::hex
                      << image.guest_addr << std::dec
                      << " entry-us="
                      << elapsed_us(entry_build_start, entry_build_end)
                      << " texlog-us="
                      << elapsed_us(tex_log_start, tex_log_end)
                      << " texdump-us="
                      << elapsed_us(tex_dump_start, tex_dump_end)
                      << " emplace-us="
                      << elapsed_us(emplace_start, emplace_end)
                      << " total-us=" << post_record_us
                      << '\n';
        }
    }
    if (trace_gx_stalls_enabled() &&
        total_us >= trace_gx_stall_threshold_us()) {
        std::cerr << "[gx-texture-miss] addr=0x" << std::hex
                  << image.guest_addr << std::dec
                  << " fmt=" << static_cast<unsigned>(image.format)
                  << " size=" << w << 'x' << h
                  << " levels=" << levels
                  << " native-mips=" << (generated_mips ? 1u : 0u)
                  << " tlut=" << static_cast<unsigned>(tlut.format)
                  << '/' << tlut.tmem_offset
                  << " guest-bytes=" << total_guest_size
                  << " staging-bytes=" << staging_size
                  << " srv=" << srv_idx
                  << " decode-us=" << elapsed_us(miss_start, decode_end)
                  << " staging-us=" << elapsed_us(decode_end, staging_end)
                  << " resource-us=" << elapsed_us(staging_end, resource_end)
                  << " record-us=" << elapsed_us(resource_end, upload_record_end)
                  << " total-us=" << total_us
                  << '\n';
    }
    return result;
}

} // namespace galaxy::gx
