// Texture records the game handed over (cooked, made while running) as GPU textures, uploaded on first use.
// The SRV heap is not shader-visible; the renderer copies its slots into the frame's descriptor ring per draw.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "galaxy/portable.h"
#include "galaxy/render/texture_store.h"
#include "galaxy/run_options.h"
#include "galaxy/path_text.h"
#include "galaxy/loss_report.h"
#include "galaxy/render/rt_materials.h"
#include "nebula_cooked_texture.h"

#include "galaxy/render/render_config.h"
#include "galaxy/windows_power.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
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

std::uint64_t read_env_u64(const char* name, std::uint64_t fallback) {
    char value[32]{};
    std::size_t length = 0;
    if (::galaxy::env_read(&length, value, sizeof(value), name) != 0 ||
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

[[nodiscard]] static unsigned full_mip_chain_levels(
    std::uint32_t width,
    std::uint32_t height) {
    unsigned levels = 1;
    while (((width >> levels) | (height >> levels)) != 0u) {
        ++levels;
    }
    return levels;
}


[[nodiscard]] static bool should_generate_native_mips(
    const TextureInfo& image,
    const TextureSampler& mode) {
    // Only repeating textures get a generated mip chain: a clamped texture is a decal placed by a texture matrix
    // (eyes, HUD digits) whose small detail blended lower levels would wash out.
    if ((min_filter_code(mode) & 0x3u) != 0u ||
        min_filter_code(mode) != 4u ||
        !mode.mag_linear ||
        mode.wrap_s == TexWrap::Clamp || mode.wrap_t == TexWrap::Clamp ||
        (image.width < 64u && image.height < 64u) ||
        !image.colour) {
        return false;
    }
    // The settings (a lock and a copy) are read only once the sampling allows generated mips.
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
                    entries[2] = {block4_interpolated_channel(a.r, b.r), block4_interpolated_channel(a.g, b.g),
                                  block4_interpolated_channel(a.b, b.b), 0xFFu};
                    entries[3] = {block4_interpolated_channel(b.r, a.r), block4_interpolated_channel(b.g, a.g),
                                  block4_interpolated_channel(b.b, a.b), 0xFFu};
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
// its layout, through the grain-checker filters when it asks for them; then any generated levels, as for a decoded
// image. Pure.
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

// The format a record is sampled in without expanding it. Upload filters need texels; a block with a transparent colour
// is expanded, BC1 making that colour black where the record's blocks keep it for filtering.
static TexelFormat sampled_format(const NebulaTexture& record) {
    if ((record.flags & NEBULA_TEXTURE_FILTER_AT_UPLOAD) != 0u) {
        return TexelFormat::Rgba8;
    }
    switch (record.layout) {
    case NEBULA_TEXTURE_L8: return TexelFormat::R8;
    case NEBULA_TEXTURE_LA8: return TexelFormat::Rg8;
    case NEBULA_TEXTURE_BLOCK4:
        return (record.flags & NEBULA_TEXTURE_OPAQUE_BLOCKS) != 0u && TextureBackend::supports_bc1() ? TexelFormat::Bc1
                                                                                                    : TexelFormat::Rgba8;
    default: return TexelFormat::Rgba8;
    }
}

// A record's levels, as they are, into their placed footprints in mapped staging memory.
static void copy_record_levels(const NebulaTexture& record, TexelFormat format, std::uint8_t* dst_base, unsigned levels,
                               const std::uint64_t* offsets, const std::uint32_t* pitches) {
    const auto* src = reinterpret_cast<const std::uint8_t*>(&record) + NEBULA_TEXTURE_HEADER;
    for (unsigned l = 0; l < levels; ++l) {
        const std::uint32_t lw = std::max<std::uint32_t>(1u, record.width >> l);
        const std::uint32_t lh = std::max<std::uint32_t>(1u, record.height >> l);
        const std::uint32_t row = texel_row_bytes(format, lw);
        const std::uint32_t rows = texel_rows(format, lh);
        std::uint8_t* dst = dst_base + offsets[l];
        for (std::uint32_t y = 0; y < rows; ++y) {
            std::memcpy(dst + static_cast<std::size_t>(y) * pitches[l], src + static_cast<std::size_t>(y) * row, row);
        }
        src += static_cast<std::size_t>(rows) * row;
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
    (void)retire_key(address);
    auto& slot = registered_images_[address];
    if (slot != nullptr) {
        replaced_images_.push_back(std::move(slot));
    }
    slot = std::move(bytes);
}

void TextureCache::register_replacement(std::uint64_t address, std::shared_ptr<const std::vector<std::byte>> record) {
    built_in_replacements_.erase(address);
    // The original draws until the replacement's first use; entries that bound the old one hold it until their fence.
    (void)retire_key(address);
    const NebulaTexture* const source = registered_record(address);
    const NebulaTexture* const picture =
        record != nullptr && record->size() >= NEBULA_TEXTURE_HEADER ? nebula_texture_record(record->data()) : nullptr;
    if (source == nullptr || picture == nullptr || picture->size != record->size() ||
        picture->layout != NEBULA_TEXTURE_RGBA8 || picture->levels == 0u || picture->levels > NEBULA_TEXTURE_MAX_LEVELS ||
        NEBULA_TEXTURE_HEADER + nebula_texture_level_bytes(picture->width, picture->height, picture->levels,
                                                           picture->layout) > record->size()) {
        return;
    }
    BuiltInReplacement& built_in = built_in_replacements_[address];
    built_in = {};
    built_in.record = std::move(record);
    built_in.source_width = source->width;
}

bool TextureCache::upload_built_in(BuiltInReplacement& built_in) {
    if (built_in.replacement.texture) {
        return true;
    }
    if (!backend_.has_upload_list()) {
        return false;
    }
    const auto* const record = reinterpret_cast<const NebulaTexture*>(built_in.record->data());
    const std::uint32_t width = record->width;
    const std::uint32_t height = record->height;
    const std::uint32_t levels = record->levels;
    std::vector<std::uint64_t> offsets;
    std::vector<std::uint32_t> pitches;
    const std::uint64_t staging_size = texture_pack_staging_layout(width, height, levels, offsets, pitches);
    try {
        const UploadAllocation staging = allocate_upload_bytes(staging_size, TextureBackend::kPlacementAlignment);
        if (staging.cpu_base == nullptr) {
            return false;
        }
        std::uint8_t* const base = staging.cpu_base + staging.offset;
        const auto* source = reinterpret_cast<const std::uint8_t*>(record) + NEBULA_TEXTURE_HEADER;
        for (std::uint32_t l = 0; l < levels; ++l) {
            const std::uint32_t lw = std::max<std::uint32_t>(1u, width >> l);
            const std::uint32_t lh = std::max<std::uint32_t>(1u, height >> l);
            for (std::uint32_t row = 0; row < lh; ++row) {
                std::memcpy(base + offsets[l] + static_cast<std::uint64_t>(row) * pitches[l], source, lw * 4u);
                source += lw * 4u;
            }
        }
        std::uint64_t allocation_bytes = 0;
        gpu::TextureRef texture = backend_.create_texture(width, height, levels, allocation_bytes);
        if (!texture) {
            return false;
        }
        backend_.record_upload(gpu::raw(texture), false, *staging.chunk, staging.offset, width, height, levels,
                               offsets.data(), pitches.data());
        built_in.replacement.texture = std::move(texture);
        built_in.replacement.width = width;
        built_in.replacement.height = height;
        built_in.replacement.levels = levels;
        built_in.replacement.bytes = allocation_bytes;
        built_in.replacement.lod_shift_x16 = static_cast<std::uint8_t>(std::clamp(
            std::lround(16.0 * std::log2(static_cast<double>(width) / static_cast<double>(built_in.source_width))), 0l,
            128l));
    } catch (const std::exception& error) {
        // Out of video memory or staging: the original stays.
        std::cerr << "[texture] built-in replacement upload failed: " << error.what() << '\n';
        return false;
    }
    built_in.record.reset();
    return true;
}

std::uint64_t TextureCache::image_bytes() const noexcept {
    std::uint64_t bytes = 0u;
    for (const auto& [address, image] : registered_images_) {
        bytes += image->size();
    }
    return bytes;
}

std::uint64_t TextureCache::upload_arena_bytes() const noexcept {
    return upload_arena_chunks_ * kUploadArenaChunkBytes;
}

std::vector<std::uint64_t> TextureCache::forget_range(std::uint64_t start, std::uint64_t end) {
    std::vector<std::uint64_t> keys;
    for (auto it = registered_images_.lower_bound(start); it != registered_images_.end() && it->first < end; ++it) {
        keys.push_back(it->first);
    }
    for (const std::uint64_t key : keys) {
        (void)forget(key);
    }
    return keys;
}

std::size_t TextureCache::forget(std::uint64_t key) {
    if (const auto it = registered_images_.find(key); it != registered_images_.end()) {
        replaced_images_.push_back(std::move(it->second));
        registered_images_.erase(it);
    }
    return retire_key(key);
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
    if (handle.resource == nullptr || handle.from_capture ||
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
        decoded_lru_release(victim, it->second);
        retire(std::move(it->second));
        entries_.erase(it);
    }
}

// Called from begin_frame for a slot whose fence has been waited on: the
// texture is GPU-idle and still in the shader-resource state.
void TextureCache::pool_retired_texture(Entry& entry) {
    if (!entry.texture || entry.handle.from_capture || entry.allocation_bytes == 0u) {
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
    built_in_replacements_.clear();
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
    decoded_lru_.clear();
    decoded_lru_pos_.clear();
    decoded_srv_keys_ = {};
    decoded_resident_bytes_ = 0;
    decoded_source_bytes_ = 0;
    decoded_evicted_bytes_ = 0;
    decoded_evicted_entries_ = 0;
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
    ++upload_arena_chunks_;
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


void TextureCache::invalidate_all() {
    for (auto& [key, entry] : entries_) {
        (void)key;
        retire(std::move(entry));
    }
    entries_.clear();
    decoded_lru_.clear();
    decoded_lru_pos_.clear();
    decoded_srv_keys_ = {};
    decoded_resident_bytes_ = 0;
    decoded_source_bytes_ = 0;
    // Capture pictures are kept: they are the renderer's.
    // Retired SRV slots return to the free list after their frame-slot fence.
}

std::size_t TextureCache::retire_key(std::uint64_t key) {
    if (blank_images_.erase(key) != 0u) {
        galaxy::loss::filled(galaxy::loss::Kind::BlankTexture);
    }
    std::size_t evicted = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.address == key) {
            decoded_lru_release(it->first, it->second);
            retire(std::move(it->second));
            it = entries_.erase(it);
            ++evicted;
        } else {
            ++it;
        }
    }
    return evicted;
}

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
    entry.handle.from_capture = false;
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
                plane.texture = backend_.create_texture(w, h, 1u, bytes, TexelFormat::R8);
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
        // The movie's chroma planes are half the luma plane's size each way.
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

TextureInfo TextureCache::info_of(std::uint64_t texture) const {
    TextureInfo info{};
    info.key = texture;
    const NebulaTexture* const record = registered_record(texture);
    if (record == nullptr) {
        return info;
    }
    info.width = record->width;
    info.height = record->height;
    info.layout = record->layout;
    info.disc_format = record->disc_format;
    info.colour = record->disc_format < 16u ? disc_format_is_colour(record->disc_format)
                                             : record->layout == NEBULA_TEXTURE_RGBA8;
    return info;
}

TextureHandle TextureCache::get(std::uint64_t texture, const TextureSampler& mode) {
    const TextureInfo image = info_of(texture);
    if (movie_active_ && image.layout == NEBULA_TEXTURE_L8) [[unlikely]] {
        // A replacement movie's plane in place of the THP's (set_movie_override).
        for (unsigned p = 0; p < 3u; ++p) {
            if (image.key == movie_addr_[p] && image.width == movie_source_width_[p] &&
                image.height == movie_source_height_[p]) {
                return movie_handles_[p];
            }
        }
    }

    // The key includes the chain length and whether it is generated: the game hand-authors lower levels (fog, lava
    // and water tinting; ), so authored mips must never alias generated chains.
    unsigned levels = 1;
    unsigned source_levels = 1;
    bool generated_mips = false;
    {
        const unsigned mip_mode =
            min_filter_code(mode) & 0x3u;
        const unsigned full_chain =
            full_mip_chain_levels(image.width, image.height);
        if (mip_mode != 0u) {
            const unsigned wanted =
                1u + (static_cast<unsigned>(lod_x16(mode.max_lod)) + 15u) / 16u;
            levels = std::min(wanted, full_chain);
            source_levels = levels;
        } else if (should_generate_native_mips(image, mode)) {
            levels = full_chain;
            source_levels = 1;
            generated_mips = levels > 1u;
        }
    }
    // A texture record (cooked, or made by the game) holds its levels as texels: nothing is decoded.
    const NebulaTexture* const record = registered_record(image.key);
    if (record != nullptr && source_levels > record->levels) {
        source_levels = record->levels;
        levels = source_levels;
    }
    // Without a record (cooked, or made by the game) there are no texels: one blank level.
    if (record == nullptr) {
        source_levels = levels = 1u;
        generated_mips = false;
    }

    Key key{};
    key.address  = image.key;
    key.width       = image.width;
    key.height      = image.height;
    key.format      = image.layout;
    key.levels      = static_cast<std::uint8_t>(levels);
    key.generated_mips = generated_mips ? 1u : 0u;
    const std::uint32_t total_source_size = record != nullptr ? record->size : 1u;
    if (image.key + total_source_size < image.key) {
        throw std::runtime_error("[TextureCache] texture address overflow");
    }


    {
        const auto it = entries_.find(key);
        if (it != entries_.end()) {
            decoded_lru_touch(key);
            return it->second.handle;
        }
    }

    // Miss: decode. Authored levels are contiguous and block-aligned and resolve as one range; generated mips
    // read level 0 only.
    const std::uint32_t w = image.width;
    const std::uint32_t h = image.height;

    // A map holding nothing (its image went with its scene, drawing_state) draws blank and has lost nothing.
    if (record == nullptr && image.key != 0u) {
        ++missing_images_;
        if (blank_images_.insert(image.key).second) {
            galaxy::loss::note(galaxy::loss::Kind::BlankTexture);
        }
        std::fprintf(stderr, "[texture] no texture record: %llX %ux%u format %u (drawn blank)\n",
                     static_cast<unsigned long long>(image.key), image.width, image.height,
                     static_cast<unsigned>(image.disc_format));
    }
    // The ray tracer's material table names textures by the texture-pack key of their base level.
    // A record carries the disc image's names (pack hash, palette hash, disc format); one the game made has none.
    const TexturePackFormat source_format = static_cast<TexturePackFormat>(image.disc_format);
    const bool nameless = record != nullptr && record->disc_format == NEBULA_TEXTURE_MADE;
    std::uint16_t rt_material = 0u;
    std::uint64_t pack = 0u;
    if (rt_materials_loaded() && !nameless) {
        pack = record != nullptr ? record->pack_hash : 0u;
        rt_material = rt_material_index(pack);
        note_rt_texture(image.key, pack, image.width, image.height, static_cast<std::uint32_t>(source_format));
    }
    // Texture packs: the naming convention's hashes of the base level (and of the palette range it uses). A resident
    // replacement binds without a decode; else the original decodes and waits for it.
    std::uint32_t pack_slot = TexturePacks::kNone;
    TexturePackName pack_name{};
    const bool pack_lookup = record != nullptr && !nameless && packs_.may_match(image.width, image.height, source_format);
    const bool pack_dump = record != nullptr && !nameless && packs_.dumping();
    if (pack_lookup || pack_dump) {
        pack_name.width = image.width;
        pack_name.height = image.height;
        pack_name.format = static_cast<std::uint8_t>(source_format);
        pack_name.mipmapped = source_levels > 1u;
        pack_name.hash = record->pack_hash;
        pack_name.palette = record->pack_palette_hash;
        ++pack_hashed_;
        if (pack_lookup) {
            pack_slot = packs_.find(image.width, image.height, source_format, pack_name.hash, pack_name.palette,
                                    pack_name.mipmapped);
            if (const TextureReplacement* replacement = packs_.resident(pack_slot)) {
                Entry entry = make_replacement_entry(*replacement, pack_slot, image.width, image.height,
                                                     generated_mips, total_source_size);
                entry.handle.rt_material = rt_material;
                entry.handle.pack = pack;
                const TextureHandle handle = entry.handle;
                entries_.emplace(key, std::move(entry));
                decoded_lru_track(key);
                ++pack_bound_;
                return handle;
            }
        }
    }
    // The game's larger painting of this image, unless a pack names it. A dump wants the image itself.
    if (record != nullptr && !pack_dump && pack_slot == TexturePacks::kNone) {
        const auto built_in = built_in_replacements_.find(texture);
        if (built_in != built_in_replacements_.end() && upload_built_in(built_in->second)) {
            Entry entry = make_replacement_entry(built_in->second.replacement, TexturePacks::kNone, image.width,
                                                 image.height, generated_mips, total_source_size);
            entry.handle.rt_material = rt_material;
            entry.handle.pack = pack;
            const TextureHandle handle = entry.handle;
            entries_.emplace(key, std::move(entry));
            decoded_lru_track(key);
            return handle;
        }
    }

    // A record the GPU can sample as it is goes up as it is.
    const TexelFormat format = record != nullptr && !pack_dump && !generated_mips ? sampled_format(*record) : TexelFormat::Rgba8;
    // Only the pixel fill moves off-thread. Texture-pack dumps, which read the decoded pixels here, and unknown
    // formats, which must throw here, stay synchronous.
    const bool async_decode = async_decode_allowed_ && record != nullptr && !pack_dump && format == TexelFormat::Rgba8;
    if (level_pixels_scratch_.size() < levels) {
        level_pixels_scratch_.resize(levels);
    }
    auto& level_pixels = level_pixels_scratch_;
    if (!async_decode && format == TexelFormat::Rgba8) {
        if (record != nullptr) {
            expand_record_levels(record, source_levels, levels, generated_mips, level_pixels);
        } else {
            level_pixels[0].assign(static_cast<std::size_t>(w) * h, RGBA8{});
        }
    }

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
            (texel_row_bytes(format, lw) + TextureBackend::kPitchAlignment - 1u) &
            ~(TextureBackend::kPitchAlignment - 1u);
        staging_size += static_cast<std::uint64_t>(level_pitch[l]) * texel_rows(format, lh);
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
        // The record stays unchanged until finish_pending_decodes() (caller contract).
        decode_pool_->submit(
            [record, w, h, source_levels, levels, generated_mips, dst_base, offsets = std::move(offsets),
             pitches = std::move(pitches)]() {
                thread_local std::vector<std::vector<RGBA8>> scratch;
                if (scratch.size() < levels) {
                    scratch.resize(levels);
                }
                expand_record_levels(record, source_levels, levels, generated_mips, scratch);
                write_staging_levels(
                    dst_base, w, h, levels, offsets.data(), pitches.data(),
                    scratch);
            });
        async_decode_pending_ = true;
        ++async_decode_jobs_;
    } else if (format != TexelFormat::Rgba8) {
        copy_record_levels(*record, format, dst_base, levels, level_offset.data(), level_pitch.data());
    } else {
        write_staging_levels(
            dst_base, w, h, levels, level_offset.data(), level_pitch.data(),
            level_pixels);
    }

    gpu::TextureRef resource;
    std::uint64_t allocation_bytes = 0;
    const bool reused_resource = format == TexelFormat::Rgba8 && take_pooled_texture(w, h, levels, resource, allocation_bytes);
    if (!reused_resource) {
        resource = backend_.create_texture(w, h, levels, allocation_bytes, format);
    }

    backend_.record_upload(gpu::raw(resource), reused_resource, *staging.chunk, staging.offset, w, h, levels,
                           level_offset.data(), level_pitch.data());

    const std::uint32_t srv_idx = allocate_srv_index();
    backend_.write_view(srv_idx, gpu::raw(resource), levels);

    TextureHandle handle{};
    handle.resource      = gpu::raw(resource);
    handle.srv_index     = srv_idx;
    handle.width         = static_cast<std::uint16_t>(w);
    handle.height        = static_cast<std::uint16_t>(h);
    handle.mip_levels    = static_cast<std::uint8_t>(levels);
    handle.from_capture = false;
    handle.generated_mips = generated_mips;
    handle.source_bytes = total_source_size;
    handle.rt_material = rt_material;
    handle.pack = pack;

    Entry entry{};
    entry.texture = std::move(resource);
    entry.allocation_bytes = allocation_bytes;
    entry.handle  = handle;

    if (pack_dump) {
        // Synchronous decode in dump mode: level_pixels holds the base level.
        packs_.dump(pack_name, w, h, reinterpret_cast<const std::uint8_t*>(level_pixels[0].data()));
    }

    const TextureHandle result = entry.handle;
    const auto decoded = entries_.emplace(key, std::move(entry)).first;
    decoded_lru_track(key);
    if (pack_slot != TexturePacks::kNone) {
        wait_for_replacement(key, decoded->second, pack_slot);
    }
    return result;
}

} // namespace galaxy::render
