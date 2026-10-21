// texture_cache.cpp: GX texture decode and D3D12 resource upload.
//
// Address-cache invalidation is explicit. An optional exact-content cache reuses identical
// decoded static textures. An unmapped guest range or unknown format is a fatal error.
// There is one non-shader-visible SRV heap; the renderer copies slots into the frame's
// shader-visible DescriptorRing before each draw.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "galaxy/gx/texture_cache.h"
#include "galaxy/gx/rt_materials.h"
#include "galaxy/gx/dependency_event_capture.h"

#include "galaxy/gx/render_config.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/windows_power.h"

#include <algorithm>
#include <array>
#include <bit>
#include "galaxy/simd128.h"
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>


namespace galaxy::gx {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

namespace {

static constexpr std::uint64_t kUploadArenaChunkBytes = 16ull << 20;
static constexpr std::uint64_t kContentCacheBudgetBytes = 64ull << 20;
static constexpr std::size_t kContentCacheMaxEntries = 4096u;

// Bound retained device allocations, including decoded RGBA8 mip levels and
// allocation alignment. Guest compressed bytes can be much smaller and are
// retained only as a separate invalidation/telemetry metric. This bound covers
// the live decoded map; frame-retired and content-cache owners are separate.
static constexpr std::uint64_t kDecodedResidentBudgetDefaultBytes = 64ull << 20;

std::uint64_t decoded_resident_budget_bytes() {
    static const std::uint64_t budget = [] {
        char value[32]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_GX_DECODED_TEXTURE_BUDGET_MB") != 0 ||
            length <= 1u || length > sizeof(value)) {
            return kDecodedResidentBudgetDefaultBytes;
        }
        for (std::size_t i = 0; i + 1u < length; ++i) {
            if (value[i] < '0' || value[i] > '9') {
                return kDecodedResidentBudgetDefaultBytes;
            }
        }
        char* end = nullptr;
        errno = 0;
        const unsigned long long mb = std::strtoull(value, &end, 10);
        if (errno == ERANGE || end == value || *end != '\0' ||
            mb > (std::numeric_limits<std::uint64_t>::max() >> 20)) {
            return kDecodedResidentBudgetDefaultBytes;
        }
        // 0 disables the bound.
        if (mb == 0ul) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return static_cast<std::uint64_t>(mb) << 20;
    }();
    return budget;
}

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

    auto it = std::lower_bound(
        ranges.begin(),
        ranges.end(),
        target_begin,
        [](const TextureCache::GuestRange& range,
           std::uint64_t value) noexcept {
            return static_cast<std::uint64_t>(range.guest_addr) +
                       static_cast<std::uint64_t>(range.size) <=
                   value;
        });
    for (; it != ranges.end() && it->guest_addr < target_end; ++it) {
        if (it->size != 0u) return &*it;
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


// ---------------------------------------------------------------------------
// Guest memory access. An unmapped range throws std::runtime_error, which the caller
// reports as GxFatalError. The texture path has no NativeServicesV1* to report through.
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

// --- Tile kernels (galaxy/simd128.h: SSE on x86, NEON on AArch64) --------
// Each kernel decodes one complete (unclipped) tile with exactly the scalar
// arithmetic; clipped edge tiles keep the scalar loops. RGBA8 is stored as
// bytes r,g,b,a, i.e. little-endian u32 r | g<<8 | b<<16 | a<<24.
using simd::v128;
[[nodiscard]] static inline v128 load_tile16(const std::uint8_t* p) noexcept {
    return simd::load(p);
}
static inline void store_pixels4(RGBA8* p, v128 v) noexcept {
    simd::store(p, v);
}
// Bit replication of a narrow channel held in u16 lanes.
[[nodiscard]] static inline v128 expand5(v128 x) noexcept {
    return simd::or_(simd::shl16<3>(x), simd::shr16<2>(x));
}
[[nodiscard]] static inline v128 expand6(v128 x) noexcept {
    return simd::or_(simd::shl16<2>(x), simd::shr16<4>(x));
}
[[nodiscard]] static inline v128 expand4(v128 x) noexcept {
    return simd::or_(simd::shl16<4>(x), x);
}
// 8 RGB565 words (native order) -> two rows of 4 RGBA8 pixels.
static inline void rgb565x8_to_rgba(v128 v, RGBA8* row0, RGBA8* row1) noexcept {
    const v128 m5 = simd::splat16(0x1F);
    const v128 m6 = simd::splat16(0x3F);
    const v128 r = expand5(simd::shr16<11>(v));
    const v128 g = expand6(simd::and_(simd::shr16<5>(v), m6));
    const v128 b = expand5(simd::and_(v, m5));
    const v128 rg = simd::or_(r, simd::shl16<8>(g));
    const v128 ba = simd::or_(b, simd::splat16(0xFF00));
    store_pixels4(row0, simd::interleave_low16(rg, ba));
    store_pixels4(row1, simd::interleave_high16(rg, ba));
}
// 8 RGB5A3 words (native order) -> two rows of 4 RGBA8 pixels.
static inline void rgb5a3x8_to_rgba(v128 v, RGBA8* row0, RGBA8* row1) noexcept {
    const v128 m3 = simd::splat16(0x07);
    const v128 m4 = simd::splat16(0x0F);
    const v128 m5 = simd::splat16(0x1F);
    const v128 opaque = simd::sign_mask16(v);  // bit 15
    const v128 r555 = expand5(simd::and_(simd::shr16<10>(v), m5));
    const v128 g555 = expand5(simd::and_(simd::shr16<5>(v), m5));
    const v128 b555 = expand5(simd::and_(v, m5));
    const v128 a3 = simd::and_(simd::shr16<12>(v), m3);
    const v128 r4 = expand4(simd::and_(simd::shr16<8>(v), m4));
    const v128 g4 = expand4(simd::and_(simd::shr16<4>(v), m4));
    const v128 b4 = expand4(simd::and_(v, m4));
    const v128 a8 = simd::or_(
        simd::or_(simd::shl16<5>(a3), simd::shl16<2>(a3)),
        simd::shr16<1>(a3));
    const auto select = [opaque](v128 when_opaque, v128 otherwise) {
        return simd::or_(simd::and_(opaque, when_opaque),
                         simd::andnot(opaque, otherwise));
    };
    const v128 r = select(r555, r4);
    const v128 g = select(g555, g4);
    const v128 b = select(b555, b4);
    const v128 a = select(simd::splat16(0xFF), a8);
    const v128 rg = simd::or_(r, simd::shl16<8>(g));
    const v128 ba = simd::or_(b, simd::shl16<8>(a));
    store_pixels4(row0, simd::interleave_low16(rg, ba));
    store_pixels4(row1, simd::interleave_high16(rg, ba));
}

[[nodiscard]] static inline std::uint16_t read_be16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(p[0]) << 8) | p[1]);
}

// ---------------------------------------------------------------------------
// TLUT entry decode helpers. Each entry is 2 big-endian bytes.
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
    // Only tiled textures get a generated mip chain. A clamped texture is a decal placed by a
    // texture matrix (eyes, HUD digits). Blended lower levels would wash out its small detail.
    if ((static_cast<unsigned>(mode.min_filter) & 0x3u) != 0u ||
        mode.min_filter != TexMinFilter::Linear ||
        mode.mag_filter != TexMagFilter::Linear ||
        mode.wrap_s == TexWrap::Clamp || mode.wrap_t == TexWrap::Clamp ||
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
            if constexpr (Format == TexFormat::I8 || Format == TexFormat::IA8) {
                if (rows == block_height && columns == block_width) {
                    RGBA8* dst = out + static_cast<std::size_t>(base_y) * width + base_x;
                    const std::size_t stride = width;
                    const v128 rows01 = load_tile16(src);
                    const v128 rows23 = load_tile16(src + 16u);
                    if constexpr (Format == TexFormat::I8) {
                        // 8 bytes per row; replicate each intensity 4 times.
                        const v128 q0 = simd::bytes(0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3);
                        const v128 q1 = simd::bytes(4, 4, 4, 4, 5, 5, 5, 5, 6, 6, 6, 6, 7, 7, 7, 7);
                        const v128 q2 = simd::bytes(8, 8, 8, 8, 9, 9, 9, 9, 10, 10, 10, 10, 11, 11, 11, 11);
                        const v128 q3 = simd::bytes(12, 12, 12, 12, 13, 13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15);
                        store_pixels4(dst, simd::shuffle_bytes(rows01, q0));
                        store_pixels4(dst + 4u, simd::shuffle_bytes(rows01, q1));
                        store_pixels4(dst + stride, simd::shuffle_bytes(rows01, q2));
                        store_pixels4(dst + stride + 4u, simd::shuffle_bytes(rows01, q3));
                        store_pixels4(dst + 2u * stride, simd::shuffle_bytes(rows23, q0));
                        store_pixels4(dst + 2u * stride + 4u, simd::shuffle_bytes(rows23, q1));
                        store_pixels4(dst + 3u * stride, simd::shuffle_bytes(rows23, q2));
                        store_pixels4(dst + 3u * stride + 4u, simd::shuffle_bytes(rows23, q3));
                    } else {
                        // (A, I) byte pairs -> I, I, I, A.
                        const v128 lo = simd::bytes(1, 1, 1, 0, 3, 3, 3, 2, 5, 5, 5, 4, 7, 7, 7, 6);
                        const v128 hi = simd::bytes(9, 9, 9, 8, 11, 11, 11, 10, 13, 13, 13, 12, 15, 15, 15, 14);
                        store_pixels4(dst, simd::shuffle_bytes(rows01, lo));
                        store_pixels4(dst + stride, simd::shuffle_bytes(rows01, hi));
                        store_pixels4(dst + 2u * stride, simd::shuffle_bytes(rows23, lo));
                        store_pixels4(dst + 3u * stride, simd::shuffle_bytes(rows23, hi));
                    }
                    src += 32u;
                    continue;
                }
            }
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

template <TlutFormat Format>
static void decode_color_tiles_impl(
    const std::uint8_t* src, std::uint32_t width, std::uint32_t height,
    RGBA8* out) {
    static_assert(Format == TlutFormat::RGB565 || Format == TlutFormat::RGB5A3);
    const std::uint32_t blocks_x = (width + 3u) / 4u;
    const std::uint32_t blocks_y = (height + 3u) / 4u;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        const std::uint32_t base_y = by * 4u;
        const std::uint32_t rows = std::min(4u, height - base_y);
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            const std::uint32_t base_x = bx * 4u;
            const std::uint32_t columns = std::min(4u, width - base_x);
            if (rows == 4u && columns == 4u) {
                RGBA8* dst = out + static_cast<std::size_t>(base_y) * width + base_x;
                const std::size_t stride = width;
                const v128 rows01 = simd::byteswap16(load_tile16(src));
                const v128 rows23 = simd::byteswap16(load_tile16(src + 16u));
                if constexpr (Format == TlutFormat::RGB565) {
                    rgb565x8_to_rgba(rows01, dst, dst + stride);
                    rgb565x8_to_rgba(rows23, dst + 2u * stride, dst + 3u * stride);
                } else {
                    rgb5a3x8_to_rgba(rows01, dst, dst + stride);
                    rgb5a3x8_to_rgba(rows23, dst + 2u * stride, dst + 3u * stride);
                }
                src += 32u;
                continue;
            }
            for (std::uint32_t py = 0; py < rows; ++py) {
                const std::uint8_t* row = src + py * 8u;
                RGBA8* dst = out + static_cast<std::size_t>(base_y + py) * width + base_x;
                for (std::uint32_t px = 0; px < columns; ++px) {
                    // Direct color and TLUT entries share the same GX word
                    // encoding. Format is fixed for this entire decode.
                    dst[px] = decode_tlut_entry(row + px * 2u, Format);
                }
            }
            src += 32u;
        }
    }
}

}  // namespace

void detail::decode_color_tiles(
    TexFormat format, const std::uint8_t* src,
    std::uint32_t width, std::uint32_t height, RGBA8* out) {
    switch (format) {
    case TexFormat::RGB565:
        decode_color_tiles_impl<TlutFormat::RGB565>(src, width, height, out);
        break;
    case TexFormat::RGB5A3:
        decode_color_tiles_impl<TlutFormat::RGB5A3>(src, width, height, out);
        break;
    default: throw std::runtime_error("[TextureCache] unsupported direct color format");
    }
}

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
    // Block: 4x4 pixels stored as two 32-byte sub-tiles:
    //   tile 0 (bytes 0-31): 16 AR pairs (A then R, row-major within 4x4)
    //   tile 1 (bytes 32-63): 16 GB pairs (G then B)
    const std::uint32_t blocks_x = (width  + 3u) / 4u;
    const std::uint32_t blocks_y = (height + 3u) / 4u;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        const std::uint32_t base_y = by * 4u;
        const std::uint32_t rows = std::min(4u, height - base_y);
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            const std::uint32_t base_x = bx * 4u;
            const std::uint32_t columns = std::min(4u, width - base_x);
            if (rows == 4u && columns == 4u) {
                // unpack(AR, GB) gives bytes A,R,G,B per pixel; rotate each
                // pixel right by one byte to R,G,B,A.
                const auto rotate = [](v128 x) {
                    return simd::or_(simd::shr32<8>(x), simd::shl32<24>(x));
                };
                const v128 ar01 = load_tile16(src);
                const v128 ar23 = load_tile16(src + 16u);
                const v128 gb01 = load_tile16(src + 32u);
                const v128 gb23 = load_tile16(src + 48u);
                RGBA8* dst = out + static_cast<std::size_t>(base_y) * width + base_x;
                const std::size_t stride = width;
                store_pixels4(dst, rotate(simd::interleave_low16(ar01, gb01)));
                store_pixels4(dst + stride, rotate(simd::interleave_high16(ar01, gb01)));
                store_pixels4(dst + 2u * stride, rotate(simd::interleave_low16(ar23, gb23)));
                store_pixels4(dst + 3u * stride, rotate(simd::interleave_high16(ar23, gb23)));
                src += 64u;
                continue;
            }
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
// palette. Clip once per tile for both paths, preserving 32-byte tile padding.
template <TexFormat Format, typename PaletteLookup>
static void decode_small_index_tiles_impl(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    const PaletteLookup& lookup,
    RGBA8* out) {
    static_assert(Format == TexFormat::C4 || Format == TexFormat::C8);
    constexpr std::uint32_t tile_rows = Format == TexFormat::C4 ? 8u : 4u;
    constexpr std::uint32_t row_bytes = 32u / tile_rows;
    const std::uint32_t blocks_x = (width  + 7u) / 8u;
    const std::uint32_t blocks_y = (height + tile_rows - 1u) / tile_rows;
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        const std::uint32_t base_y = by * tile_rows;
        const std::uint32_t rows = std::min(tile_rows, height - base_y);
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            const std::uint32_t base_x = bx * 8u;
            const std::uint32_t columns = std::min(8u, width - base_x);
            for (std::uint32_t py = 0; py < rows; ++py) {
                const std::uint8_t* row = src + py * row_bytes;
                RGBA8* dst = out + static_cast<std::size_t>(base_y + py) * width + base_x;
                if constexpr (Format == TexFormat::C4) {
                    std::uint32_t px = 0u;
                    for (; px + 1u < columns; px += 2u) {
                        const std::uint8_t byte = row[px / 2u];
                        dst[px] = lookup(byte >> 4u);
                        dst[px + 1u] = lookup(byte & 15u);
                    }
                    if (px < columns) {
                        dst[px] = lookup(row[px / 2u] >> 4u);
                    }
                } else {
                    for (std::uint32_t px = 0u; px < columns; ++px) {
                        dst[px] = lookup(row[px]);
                    }
                }
            }
            src += 32u;
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
            [&](const auto& lookup) {
                decode_small_index_tiles_impl<TexFormat::C4>(src, width, height, lookup, out);
            });
        return;
    case TexFormat::C8:
        decode_with_small_palette<256u>(palette, tlut_format, pixels,
            [&](const auto& lookup) {
                decode_small_index_tiles_impl<TexFormat::C8>(src, width, height, lookup, out);
            });
        return;
    default:
        throw std::runtime_error("[TextureCache] expected small indexed format");
    }
}

void detail::decode_c14x2_tiles(
    const std::uint8_t* src,
    std::uint32_t width,
    std::uint32_t height,
    const std::uint8_t* palette,
    TlutFormat tlut_fmt,
    RGBA8* out) {
    // Block: 4x4 pixels; two big-endian bytes per index, bits 13:0 used.
    for (std::uint32_t y = 0; y < height; y += 4u) {
        const std::uint32_t rows = std::min(4u, height - y);
        for (std::uint32_t x = 0; x < width; x += 4u) {
            const std::uint32_t columns = std::min(4u, width - x);
            for (std::uint32_t py = 0; py < rows; ++py) {
                const std::uint8_t* row = src + py * 8u;
                RGBA8* dst = out + static_cast<std::size_t>(y + py) * width + x;
                for (std::uint32_t px = 0; px < columns; ++px) {
                    const std::uint32_t index =
                        static_cast<std::uint32_t>(read_be16(row + px * 2u)) & 0x3FFFu;
                    dst[px] = decode_tlut_entry(palette + index * 2u, tlut_fmt);
                }
            }
            src += 32u;
        }
    }
}

namespace {

// ---------------------------------------------------------------------------
// CMPR (S3TC / DXT1): 8x8 pixel blocks, each containing four 4x4 DXT1 sub-blocks.
// GX sub-block layout within the 8x8 block:
//   sub[0]=top-left, sub[1]=top-right, sub[2]=bottom-left, sub[3]=bottom-right.
// ---------------------------------------------------------------------------

// Decode one 4x4 DXT1 sub-block from 8 bytes into a 4x4 patch of `out`.
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
    // Outer loop: 8x8 GX macro-blocks.
    const std::uint32_t macro_x = (width  + 7u) / 8u;
    const std::uint32_t macro_y = (height + 7u) / 8u;
    // Sub-block layout within each 8x8 macro-block:
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
        // Each 8x8 macro-block = 4 DXT1 sub-blocks x 8 bytes = 32 bytes.
        return blocks(width, 8u) * blocks(height, 8u) * 32u;
    default:
        throw std::runtime_error("[TextureCache] tex_data_size: unknown TexFormat");
    }
}

[[nodiscard]] static bool texture_decode_format_supported(TexFormat format) {
    switch (format) {
    case TexFormat::I4:
    case TexFormat::I8:
    case TexFormat::IA4:
    case TexFormat::IA8:
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

// Decodes every guest-authored level (contiguous, block-aligned from `src`)
// and generates any enhancement levels from the decoded level above. Pure:
// reads only `src`/`palette`, writes only `level_pixels`. Runs on the render
// thread or a TextureDecodePool worker.
static void decode_texture_levels(
    TexFormat format,
    const std::uint8_t* src,
    std::uint32_t w,
    std::uint32_t h,
    unsigned guest_levels,
    unsigned levels,
    bool generated_mips,
    const std::uint8_t* palette,
    TlutFormat tlut_format,
    std::vector<std::vector<RGBA8>>& level_pixels) {
    for (unsigned l = 0; l < guest_levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, w >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, h >> l);
        level_pixels[l].resize(
            static_cast<std::size_t>(lw) * static_cast<std::size_t>(lh));
        RGBA8* out = level_pixels[l].data();

        switch (format) {
        case TexFormat::I4:
        case TexFormat::I8:
        case TexFormat::IA4:
        case TexFormat::IA8:
            detail::decode_intensity_tiles(format, src, lw, lh, out);
            break;
        case TexFormat::RGB565:
        case TexFormat::RGB5A3:
            detail::decode_color_tiles(format, src, lw, lh, out);
            break;
        case TexFormat::RGBA8:
            detail::decode_rgba8_tiles(src, lw, lh, out);
            break;
        case TexFormat::C4:
        case TexFormat::C8:
            detail::decode_small_index_tiles(format, src, lw, lh,
                palette, tlut_format, out);
            break;
        case TexFormat::C14X2:
            detail::decode_c14x2_tiles(src, lw, lh,
                palette, tlut_format, out);
            break;
        case TexFormat::CMPR:
            detail::decode_cmpr_tiles(src, lw, lh, out);
            break;
        default:
            throw std::runtime_error(
                "[TextureCache] get: unhandled TexFormat");
        }
        if (efb_true_color() && !detail::flatten_texel_checker(level_pixels[l], lw, lh)) {
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

std::size_t TextureCache::hash_content_key(const ContentKey& key) {
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
        return;
    }
    async_decode_pending_ = false;
    decode_pool_->wait_all();
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

    // RendererD3D12::begin_frame has already waited for this slot's fence,
    // so every resource retired during this slot's previous use is GPU-idle.
    // Keep its CPU descriptor alive too: handles selected earlier in a frame
    // can still be copied into a new table after another lookup evicts them.
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
    stats.budget_bytes = decoded_resident_budget_bytes();
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
    // Single removal path for the decoded map. It keeps the resident-byte total and the LRU
    // consistent with entries_. Every explicit removal goes through here: a total that drifts
    // high makes the eviction loop evict textures that are still within budget.
    decoded_resident_bytes_ -= std::min<std::uint64_t>(
        decoded_resident_bytes_, entry.allocation_bytes);
    decoded_guest_bytes_ -= std::min<std::uint64_t>(
        decoded_guest_bytes_, entry.handle.guest_byte_size);
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
    decoded_guest_bytes_ += entry->second.handle.guest_byte_size;
    decoded_lru_evict_to_budget();
}

void TextureCache::decoded_lru_evict_to_budget() {
    const std::uint64_t budget = decoded_resident_budget_bytes();
    if (decoded_resident_bytes_ <= budget) {
        return;
    }
    // Retire rather than destroy. retire() parks the resource in the current frame slot's list,
    // which is cleared only after that slot's fence has been waited on, so a submitted command
    // list that still references the texture stays valid. Its CPU SRV slot is reclaimed at the
    // same fence; live plus in-flight retired descriptors must fit kSrvHeapCapacity.
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
        // Uses the same helper as the explicit invalidation paths, so the byte total and the LRU
        // cannot drift apart. decoded_guest_envelope_ is left unchanged: it only widens, so an
        // evicted range keeps the envelope conservative. A later scan may test "may overlap" when it
        // cannot. That costs a fast path and never returns a stale texture. The envelope is rebuilt
        // whenever entries_ empties.
        decoded_lru_release(victim, it->second);
        // Retired, not destroyed: `retire()` parks the resource in the current
        // frame slot's list, which is cleared only after that slot's fence has
        // been waited on, so an already-submitted command list that still
        // references this texture stays valid. The SRV index also remains valid
        // until that slot's next begin_frame, when it returns to the free list.
        retire(std::move(it->second));
        entries_.erase(it);
    }
}

namespace {
std::uint64_t texture_pool_cap_bytes() {
    static const std::uint64_t cap = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_TEXTURE_POOL_MB") == 0 &&
            length > 0) {
            const unsigned long mb = std::strtoul(value, nullptr, 10);
            return static_cast<std::uint64_t>(mb) << 20;
        }
        return std::uint64_t{256} << 20;
    }();
    return cap;
}
}  // namespace

// Called from begin_frame for a slot whose fence has been waited on: the
// texture is GPU-idle and still in the shader-resource state.
void TextureCache::pool_retired_texture(Entry& entry) {
    if (!entry.texture || entry.handle.from_efb_copy || entry.allocation_bytes == 0u) {
        return;
    }
    if (texture_pool_bytes_ + entry.allocation_bytes > texture_pool_cap_bytes()) {
        ++texture_pool_drops_;
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
        ++texture_pool_reuses_;
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
    if (decoded_content_cache_enabled()) {
        std::cerr << "[gx-content-cache] lookups=" << content_lookups_
                  << " exact-hits=" << content_hits_
                  << " decoded-misses=" << content_decoded_misses_
                  << " retained-bytes=" << content_retained_bytes_
                  << " evicted-entries=" << content_evicted_entries_
                  << " evicted-bytes=" << content_evicted_bytes_
                  << " live-entries=" << content_entries_.size()
                  << " live-bytes=" << content_bytes_
                  // Content-cache totals include allocation and exact CPU keys.
                  // Decoded-map bytes charge allocations separately per entry.
                  << " decoded-map-entries=" << entries_.size()
                  << " decoded-map-guest-bytes=" << decoded_entry_guest_bytes()
                  << " decoded-map-resident-kind=committed-allocation-per-entry"
                  << " decoded-map-resident-bytes=" << decoded_resident_bytes_
                  << " decoded-map-budget-bytes="
                  << decoded_resident_budget_bytes()
                  << " decoded-map-evicted-entries=" << decoded_evicted_entries_
                  << " decoded-map-evicted-bytes=" << decoded_evicted_bytes_
                  << " decoded-map-lru-entries=" << decoded_lru_.size()
                  << '\n';
    }
    entries_.clear();
    decoded_guest_envelope_.clear();
    decoded_lru_.clear();
    decoded_lru_pos_.clear();
    decoded_srv_keys_ = {};
    decoded_resident_bytes_ = 0;
    decoded_guest_bytes_ = 0;
    decoded_evicted_bytes_ = 0;
    decoded_evicted_entries_ = 0;
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
    alias_guest_envelope_.clear();
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

// ---------------------------------------------------------------------------
// load_tlut: BP 0x64 / 0x65
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
    // One transfer can touch several slots, and a C14X2 palette can span many slots even when
    // its first slot is outside the transfer. Compare each overlapping palette before replacing
    // the bank. A changed transfer can still leave this texture's palette unchanged (C4 uses only
    // the first 32 bytes of a 512-byte C8-slot reload).
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
            decoded_lru_release(it->first, it->second);
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
    gpu::TextureRef texture) {
    // Validate before evicting a still-owned alias or consuming an SRV slot.
    if (!backend_.ready() || !texture) {
        throw std::runtime_error("[TextureCache] invalid EFB alias resource");
    }
    const std::uint32_t guest_byte_size = efb_copy_guest_byte_size(params);
    if (static_cast<std::uint64_t>(guest_addr) + guest_byte_size > (1ull << 32u)) {
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
        guest_addr,
        alias_width,
        alias_height,
        efb_copy_alias_texture_format(params.target_format),
        {},
    };

    // Called on every copy. The renderer keys destination textures by address and format, so one
    // address can alternate resources. Exact same-resource aliases are remembered here, but the
    // function does not return before overlap eviction has retired stale decoded textures and
    // older EFB aliases.
    const auto current_alias_it = efb_aliases_.find(alias_key);
    const bool current_alias_same_resource =
        current_alias_it != efb_aliases_.end() &&
        gpu::raw(current_alias_it->second.texture) == gpu::raw(texture);
    if (debug_capture_frame_ != 0u) {
        std::cerr << "[cap-alias-copy] frame=" << debug_capture_frame_
                  << " addr=0x" << std::hex << guest_addr
                  << " resource=" << gpu::raw(texture) << std::dec
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
    for (auto it = decoded_guest_envelope_.may_overlap(guest_addr, guest_byte_size)
             ? entries_.begin() : entries_.end(); it != entries_.end(); ) {
        const std::uint64_t entry_begin = it->first.guest_addr;
        const std::uint64_t entry_end =
            entry_begin + it->second.handle.guest_byte_size;
        if (overlaps(new_begin, new_end, entry_begin, entry_end)) {
            decoded_lru_release(it->first, it->second);
            retire(std::move(it->second));
            it = entries_.erase(it);
            evicted_overlap = true;
        } else {
            ++it;
        }
    }
    for (auto it = alias_guest_envelope_.may_overlap(guest_addr, guest_byte_size)
             ? efb_aliases_.begin() : efb_aliases_.end(); it != efb_aliases_.end(); ) {
        const std::uint64_t alias_begin = it->first.guest_addr;
        const std::uint64_t alias_end =
            alias_begin + it->second.handle.guest_byte_size;
        const bool is_current_same_resource =
            it->first == alias_key &&
            gpu::raw(it->second.texture) == gpu::raw(texture);
        if (!is_current_same_resource &&
            overlaps(new_begin, new_end, alias_begin, alias_end)) {
            if (dependency_event_sink_ != nullptr) {
                dependency_event_sink_->alias_retire(
                    DependencyAliasKey{it->first.guest_addr,
                                       it->first.width, it->first.height,
                                       it->first.format},
                    reinterpret_cast<std::uintptr_t>(
                        gpu::raw(it->second.texture)));
            }
            retire(std::move(it->second));
            it = efb_aliases_.erase(it);
            evicted_overlap = true;
        } else {
            ++it;
        }
    }

    // Publish every new/extended alias footprint. Removal never shrinks the
    // filter; a fresh empty map can reset it before its first publication.
    if (efb_aliases_.empty()) alias_guest_envelope_.clear();
    alias_guest_envelope_.include(guest_addr, guest_byte_size);
    if (current_alias_same_resource) {
        const bool footprint_changed =
            current_alias_it->second.handle.guest_byte_size != guest_byte_size;
        current_alias_it->second.handle.guest_byte_size = guest_byte_size;
        if (dependency_event_sink_ != nullptr) {
            dependency_event_sink_->alias_copy(
                DependencyAliasKey{alias_key.guest_addr, alias_key.width,
                                   alias_key.height, alias_key.format},
                reinterpret_cast<std::uintptr_t>(gpu::raw(texture)));
        }
        return evicted_overlap || footprint_changed;
    }

    const std::uint32_t srv_idx = allocate_srv_index();

    // A 2D texture view: the resource passed in is a Texture2D (created by the renderer's EFB copy path).
    backend_.write_view(srv_idx, gpu::raw(texture), 1u);

    TextureHandle handle{};
    handle.resource      = gpu::raw(texture);
    handle.srv_index     = srv_idx;
    handle.width         = static_cast<std::uint16_t>(resource_width);
    handle.height        = static_cast<std::uint16_t>(resource_height);
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
                    gpu::raw(alias_it->second.texture)));
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
    decoded_guest_envelope_.clear();
    decoded_lru_.clear();
    decoded_lru_pos_.clear();
    decoded_srv_keys_ = {};
    decoded_resident_bytes_ = 0;
    decoded_guest_bytes_ = 0;
    // EFB aliases are kept: their content lives on the GPU.
    // Retired SRV slots return to the free list after their frame-slot fence.
}

// ---------------------------------------------------------------------------
// get: primary texture fetch
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
    for (auto it = decoded_guest_envelope_.may_overlap(sorted_coalesced_ranges)
             ? entries_.begin() : entries_.end(); it != entries_.end(); ) {
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
            decoded_lru_release(it->first, it->second);
            retire(std::move(it->second));
            it = entries_.erase(it);
            ++evicted;
        } else {
            ++it;
        }
    }
    for (auto it = alias_guest_envelope_.may_overlap(sorted_coalesced_ranges)
             ? efb_aliases_.begin() : efb_aliases_.end(); it != efb_aliases_.end(); ) {
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
                          << " resource=" << gpu::raw(it->second.texture)
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
                        gpu::raw(it->second.texture)));
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

// ---------------------------------------------------------------------------
// Texture packs
// ---------------------------------------------------------------------------

namespace {

// GALAXY_TEXTURE_PACK_TRACE=1: each replacement bound or swapped in, with its guest address and file.
bool trace_texture_packs_enabled() {
    static const bool enabled = read_env_u64("GALAXY_TEXTURE_PACK_TRACE", 0u) != 0u;
    return enabled;
}

}  // namespace

TextureCache::Entry TextureCache::make_replacement_entry(const TextureReplacement& replacement, std::uint32_t slot,
                                                         std::uint16_t width, std::uint16_t height,
                                                         bool generated_mips, std::uint32_t guest_byte_size) {
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
    entry.handle.guest_byte_size = guest_byte_size;
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
    // Uploads within ~3 ms a frame (at least one): the rest wait for the next frames, so a pack's first textures
    // never stall a frame (a 2048x2048 level chain is 22 MB to stage).
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
                                                it->second.handle.generated_mips, it->second.handle.guest_byte_size);
            next.handle.rt_material = it->second.handle.rt_material;
            next.handle.pack = it->second.handle.pack;
            decoded_lru_release(key, it->second);
            retire(std::move(it->second));
            it->second = std::move(next);
            decoded_lru_track(key);
            ++pack_swapped_;
            if (trace_texture_packs_enabled()) {
                std::cerr << "[texture-pack] swap addr=" << std::hex << key.guest_addr << std::dec << ' '
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
    const std::array<std::uint32_t, 3> addresses{mark.y, mark.u, mark.v};
    const bool new_picture = frame->frame != movie_frame_ || frame->staging.get() != movie_staging_seen_;
    const bool same_planes = movie_active_ && addresses == movie_addr_ && mark.width == movie_guest_width_[0] &&
                             mark.height == movie_guest_height_[0];
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
                plane.texture = backend_.create_texture(w, h, 1u, bytes);
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
        movie_guest_width_[p] = static_cast<std::uint16_t>(p == 0u ? mark.width : mark.width >> 1u);
        movie_guest_height_[p] = static_cast<std::uint16_t>(p == 0u ? mark.height : mark.height >> 1u);
        const MoviePlane& plane = movie_ring_[movie_ring_at_][p];
        TextureHandle handle{};
        handle.resource = gpu::raw(plane.texture);
        handle.srv_index = plane.srv_index;
        handle.width = movie_guest_width_[p];
        handle.height = movie_guest_height_[p];
        handle.mip_levels = 1u;
        handle.guest_byte_size = static_cast<std::uint32_t>(movie_guest_width_[p]) * movie_guest_height_[p];
        movie_handles_[p] = handle;
    }
    movie_active_ = true;
    return true;
}

TextureHandle TextureCache::get(
    const TexImage& image,
    const TexMode& mode,
    const TlutRef& tlut,
    GuestMemoryV1* memory) {
    if (movie_active_ && image.format == TexFormat::I8) [[unlikely]] {
        // A replacement movie's plane in place of the THP's (set_movie_override).
        for (unsigned p = 0; p < 3u; ++p) {
            if (image.guest_addr == movie_addr_[p] && image.width == movie_guest_width_[p] &&
                image.height == movie_guest_height_[p]) {
                return movie_handles_[p];
            }
        }
    }
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

    // 2. Build cache key. The mip chain length and source are part of the key. The game
    // hand-authors lower mip levels (fog, lava and water tinting; see Dolphin PRs #6118 and
    // #6875), so authored mips must never alias the generated mip chains used for filtering.
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
    // Only indexed formats use a TLUT. Keying direct formats on the BP palette registers, which
    // may hold a previous texture's palette, created duplicate decodes of identical guest images.
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
            decoded_lru_touch(key);
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

    // TLUT slot N starts at TMEM byte N*512 (256 entries of 2 bytes). tlut.tmem_offset is in
    // units of 256 entries.
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
    // The ray tracer's material table names textures by the texture-pack key of their base level.
    std::uint16_t rt_material = 0u;
    std::uint64_t pack = 0u;
    if (rt_materials_loaded()) {
        pack = texture_pack_hash(
            src, texture_guest_byte_size(image.format, image.width, image.height, 1u));
        rt_material = rt_material_index(pack);
        note_rt_texture(image.guest_addr, pack, image.width, image.height, static_cast<std::uint32_t>(image.format));
    }
    // Texture packs: the naming convention's hashes of the base level (and of the palette range it uses). A resident
    // replacement binds without a decode; else the original decodes and waits for it.
    std::uint32_t pack_slot = TexturePacks::kNone;
    TexturePackName pack_name{};
    const bool pack_lookup = packs_.may_match(image.width, image.height, image.format);
    const bool pack_dump = packs_.dumping();
    if (pack_lookup || pack_dump) {
        pack_name.width = image.width;
        pack_name.height = image.height;
        pack_name.format = static_cast<std::uint8_t>(image.format);
        pack_name.mipmapped = guest_levels > 1u;
        pack_name.hash = pack != 0u ? pack
                                    : texture_pack_hash(src, texture_guest_byte_size(image.format, image.width,
                                                                                     image.height, 1u));
        if (texture_format_palettized(image.format)) {
            pack_name.tlut = texture_pack_tlut_hash(image.format, src, image.width, image.height,
                                                    tlut_bank_ + tlut_byte_offset);
        }
        ++pack_hashed_;
        if (pack_lookup) {
            pack_slot = packs_.find(image.width, image.height, image.format, pack_name.hash, pack_name.tlut,
                                    pack_name.mipmapped);
            if (const TextureReplacement* replacement = packs_.resident(pack_slot)) {
                Entry entry = make_replacement_entry(*replacement, pack_slot, image.width, image.height,
                                                     generated_mips, total_guest_size);
                entry.handle.rt_material = rt_material;
                entry.handle.pack = pack;
                const TextureHandle handle = entry.handle;
                if (entries_.empty()) decoded_guest_envelope_.clear();
                decoded_guest_envelope_.include(image.guest_addr, total_guest_size);
                entries_.emplace(key, std::move(entry));
                decoded_lru_track(key);
                ++pack_bound_;
                if (trace_texture_packs_enabled()) {
                    std::cerr << "[texture-pack] bind addr=" << std::hex << image.guest_addr << std::dec << ' '
                              << packs_.file(pack_slot).filename().string() << '\n';
                }
                return handle;
            }
        }
    }
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

        // Matches Aurora's content-key rule: palette bytes are part of the identity, so a TLUT reload
        // cannot return stale colors.
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
            content_key->bucket_hash = hash_content_key(*content_key);
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
                const std::uint32_t srv_idx = allocate_srv_index();
                backend_.write_view(srv_idx, gpu::raw(it->second.texture), levels);

                Entry entry{};
                entry.texture = it->second.texture;
                entry.allocation_bytes = it->second.allocation_bytes;
                entry.handle.resource = gpu::raw(entry.texture);
                entry.handle.srv_index = srv_idx;
                entry.handle.width = static_cast<std::uint16_t>(w);
                entry.handle.height = static_cast<std::uint16_t>(h);
                entry.handle.mip_levels = static_cast<std::uint8_t>(levels);
                entry.handle.from_efb_copy = false;
                entry.handle.generated_mips = generated_mips;
                entry.handle.guest_byte_size = total_guest_size;
                entry.handle.rt_material = rt_material;
                entry.handle.pack = pack;
                const TextureHandle handle = entry.handle;
                if (entries_.empty()) decoded_guest_envelope_.clear();
                decoded_guest_envelope_.include(image.guest_addr, total_guest_size);
                const auto inserted = entries_.emplace(key, std::move(entry)).first;
                decoded_lru_track(key);
                if (pack_slot != TexturePacks::kNone) {
                    wait_for_replacement(key, inserted->second, pack_slot);
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
    // Opt-in decoded-texture dump: GALAXY_GX_CAPTURE_TEXDIR=<dir> writes
    // every newly decoded mip0 as <dir>/tex_<addr>_f<fmt>.ppm (RGB) and
    // .pgm (alpha).  Zero cost unless the env var is set.
    static const std::string tex_dump_dir = [] {
        char value[MAX_PATH]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value),
                     "GALAXY_GX_CAPTURE_TEXDIR") != 0 || length <= 1) {
            return std::string{};
        }
        return std::string(value);
    }();
    const bool palettized = image.format == TexFormat::C4 ||
        image.format == TexFormat::C8 || image.format == TexFormat::C14X2;
    // Only the pixel fill moves off-thread. Paths that inspect decoded pixels
    // on this thread, and malformed palette formats whose decode must fail at
    // the point of use, keep the synchronous decode.
    const bool async_decode = async_decode_allowed_ &&
        texture_decode_format_supported(image.format) &&
        (!palettized || static_cast<unsigned>(tlut.format) <= 2u) &&
        !trace_texture_uploads_enabled() && tex_dump_dir.empty() && !pack_dump;
    if (level_pixels_scratch_.size() < levels) {
        level_pixels_scratch_.resize(levels);
    }
    auto& level_pixels = level_pixels_scratch_;
    if (!async_decode) {
        decode_texture_levels(
            image.format, src, w, h, guest_levels, levels, generated_mips,
            tlut_bank_ + tlut_byte_offset, tlut.format, level_pixels);
    }
    const auto decode_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // 5. Upload to D3D12. One staging buffer holds every level at a 512-aligned placed footprint
    // (row pitch 256-aligned). It is copied into a DEFAULT-heap Texture2D with MipLevels=N through
    // the frame command list. The shader samples Texture2D, so the bound resource must be a real
    // texture. A buffer SRV in a Texture2D slot is undefined behavior and hangs the GPU.
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
        // The palette bank can be reloaded later in this frame; the job owns
        // the exact entries this texture was keyed with.
        std::vector<std::uint8_t> palette;
        if (palettized) {
            palette.assign(tlut_bank_ + tlut_byte_offset,
                           tlut_bank_ + tlut_byte_offset + required_palette_bytes);
        }
        const TexFormat format = image.format;
        const TlutFormat tlut_format = tlut.format;
        // Not `src`: with the content cache on it points into a key that may
        // be evicted before the job runs. The guest bytes are the same bytes
        // and stay immutable until finish_pending_decodes() (caller contract).
        const auto* const guest_src =
            reinterpret_cast<const std::uint8_t*>(guest_bytes);
        decode_pool_->submit(
            [format, tlut_format, guest_src, w, h, guest_levels, levels,
             generated_mips, dst_base, offsets = std::move(offsets),
             pitches = std::move(pitches), palette = std::move(palette)]() {
                thread_local std::vector<std::vector<RGBA8>> scratch;
                if (scratch.size() < levels) {
                    scratch.resize(levels);
                }
                decode_texture_levels(
                    format, guest_src, w, h, guest_levels, levels,
                    generated_mips, palette.empty() ? nullptr : palette.data(),
                    tlut_format, scratch);
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
    handle.guest_byte_size = total_guest_size;
    handle.rt_material = rt_material;
    handle.pack = pack;

    Entry entry{};
    entry.texture = std::move(resource);
    entry.allocation_bytes = allocation_bytes;
    entry.handle  = handle;
    const auto entry_build_end = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // Logs each new texture decode with a content fingerprint.
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

    // Decoded-texture dump (tex_dump_dir above); never async.
    const auto tex_dump_start = trace_stalls
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
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
    if (entries_.empty()) decoded_guest_envelope_.clear();
    decoded_guest_envelope_.include(image.guest_addr, total_guest_size);
    const auto decoded = entries_.emplace(key, std::move(entry)).first;
    decoded_lru_track(key);
    if (pack_slot != TexturePacks::kNone) {
        wait_for_replacement(key, decoded->second, pack_slot);
    }
    if (content_key.has_value()) {
        ++content_decoded_misses_;
        // The key lives in both the map and LRU list; count both copies.
        std::uint64_t retained_bytes = 2u *
            (content_key->source.size() + content_key->palette.size());
        retained_bytes += allocation_bytes;
        if (retained_bytes <= kContentCacheBudgetBytes) {
            content_lru_.push_front(*content_key);
            const auto [it, inserted] = content_entries_.emplace(
                std::move(*content_key),
                ContentEntry{
                    // A budget eviction may already have removed this entry.
                    // Its returned resource remains owned through the frame's
                    // retirement fence; retain it without another map lookup.
                    gpu::texture_ref(result.resource),
                    retained_bytes,
                    allocation_bytes,
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
    const std::uint64_t total_us = trace_stalls
        ? elapsed_us(miss_start, upload_record_end) : 0u;
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
    if (trace_stalls &&
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

} // namespace galaxy::gx
