// SPDX-License-Identifier: GPL-3.0-only
// Texture packs' image codecs (texture_pack.h): PNG through stb_image and stb_image_write, DDS block formats through
// bcdec (third_party/stb, third_party/bcdec; MIT or public domain), all to RGBA8 levels.

#include "galaxy/gx/texture_pack.h"

#include "galaxy/gx/texture_cache.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#endif
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 16384
#include "../../../third_party/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../../../third_party/stb/stb_image_write.h"
#define BCDEC_IMPLEMENTATION
#include "../../../third_party/bcdec/bcdec.h"
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace galaxy::gx {
namespace {

bool read_file(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return false;
    const std::streamoff size = in.tellg();
    if (size <= 0 || size > (std::streamoff{1} << 31)) return false;
    bytes.resize(static_cast<std::size_t>(size));
    in.seekg(0);
    return static_cast<bool>(in.read(reinterpret_cast<char*>(bytes.data()), size));
}

// Levels below the given ones, each a 2x2 box of the one above (texture_cache.h's generator).
void generate_levels(TexturePackPixels& image) {
    const auto count = static_cast<std::uint32_t>(std::bit_width(std::max(image.width, image.height)));
    std::vector<RGBA8> above;
    for (std::uint32_t level = static_cast<std::uint32_t>(image.levels.size()); level < count; ++level) {
        const std::uint32_t aw = std::max(1u, image.width >> (level - 1u));
        const std::uint32_t ah = std::max(1u, image.height >> (level - 1u));
        const std::uint32_t lw = std::max(1u, image.width >> level);
        const std::uint32_t lh = std::max(1u, image.height >> level);
        above.resize(static_cast<std::size_t>(aw) * ah);
        std::memcpy(above.data(), image.levels[level - 1u].data(), above.size() * 4u);
        std::vector<RGBA8> next;
        detail::generate_rgba8_mip_level(above, aw, ah, next, lw, lh);
        std::vector<std::uint8_t> bytes(next.size() * 4u);
        std::memcpy(bytes.data(), next.data(), bytes.size());
        image.levels.push_back(std::move(bytes));
    }
}

// One DDS level as RGBA8.
bool decode_dds_level(DdsFormat format, const std::uint8_t* src, std::uint32_t width, std::uint32_t height,
                      std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(width) * height * 4u, 0);
    if (format == DdsFormat::RGBA8 || format == DdsFormat::BGRA8) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(width) * height; ++i) {
            const std::uint8_t* p = src + i * 4u;
            std::uint8_t* q = out.data() + i * 4u;
            q[0] = format == DdsFormat::BGRA8 ? p[2] : p[0];
            q[1] = p[1];
            q[2] = format == DdsFormat::BGRA8 ? p[0] : p[2];
            q[3] = p[3];
        }
        return true;
    }
    const std::uint32_t block_bytes =
        (format == DdsFormat::BC1 || format == DdsFormat::BC4) ? 8u : 16u;
    const std::uint32_t blocks_x = (width + 3u) / 4u;
    const std::uint32_t blocks_y = (height + 3u) / 4u;
    std::uint8_t block[4 * 4 * 4];
    std::uint8_t narrow[4 * 4 * 2];
    for (std::uint32_t by = 0; by < blocks_y; ++by) {
        for (std::uint32_t bx = 0; bx < blocks_x; ++bx) {
            const std::uint8_t* in = src + (static_cast<std::size_t>(by) * blocks_x + bx) * block_bytes;
            switch (format) {
            case DdsFormat::BC1: bcdec_bc1(in, block, 16); break;
            case DdsFormat::BC2: bcdec_bc2(in, block, 16); break;
            case DdsFormat::BC3: bcdec_bc3(in, block, 16); break;
            case DdsFormat::BC7: bcdec_bc7(in, block, 16); break;
            case DdsFormat::BC4:
                // One channel: grey, opaque.
                bcdec_bc4(in, narrow, 4);
                for (int i = 0; i < 16; ++i) {
                    block[i * 4] = block[i * 4 + 1] = block[i * 4 + 2] = narrow[i];
                    block[i * 4 + 3] = 255;
                }
                break;
            case DdsFormat::BC5:
                // Two channels: red and green, opaque.
                bcdec_bc5(in, narrow, 8);
                for (int i = 0; i < 16; ++i) {
                    block[i * 4] = narrow[i * 2];
                    block[i * 4 + 1] = narrow[i * 2 + 1];
                    block[i * 4 + 2] = 0;
                    block[i * 4 + 3] = 255;
                }
                break;
            default:
                return false;
            }
            for (std::uint32_t y = 0; y < 4u && by * 4u + y < height; ++y) {
                const std::uint32_t columns = std::min(4u, width - bx * 4u);
                std::memcpy(out.data() + ((static_cast<std::size_t>(by) * 4u + y) * width + bx * 4u) * 4u,
                            block + y * 16u, columns * 4u);
            }
        }
    }
    return true;
}

}  // namespace

bool load_texture_pack_image(const std::filesystem::path& file, bool dds, TexturePackPixels& out, std::string* error) {
    out = TexturePackPixels{};
    std::vector<std::uint8_t> bytes;
    if (!read_file(file, bytes)) {
        if (error != nullptr) *error = "cannot read the file";
        return false;
    }
    if (dds) {
        DdsLayout layout{};
        if (!parse_dds(bytes, layout)) {
            if (error != nullptr) *error = "not a supported DDS";
            return false;
        }
        out.width = layout.width;
        out.height = layout.height;
        for (std::uint32_t level = 0; level < layout.levels; ++level) {
            std::vector<std::uint8_t> pixels;
            if (!decode_dds_level(layout.format, bytes.data() + layout.level_offset[level],
                                  std::max(1u, layout.width >> level), std::max(1u, layout.height >> level), pixels)) {
                if (error != nullptr) *error = "cannot decode the DDS";
                return false;
            }
            out.levels.push_back(std::move(pixels));
        }
    } else {
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height,
                                                &channels, 4);
        if (pixels == nullptr || width <= 0 || height <= 0) {
            if (error != nullptr) *error = stbi_failure_reason() != nullptr ? stbi_failure_reason() : "not a PNG";
            stbi_image_free(pixels);
            return false;
        }
        out.width = static_cast<std::uint32_t>(width);
        out.height = static_cast<std::uint32_t>(height);
        out.levels.emplace_back(pixels, pixels + static_cast<std::size_t>(width) * height * 4u);
        stbi_image_free(pixels);
    }
    generate_levels(out);
    return true;
}

bool write_png(const std::filesystem::path& file, std::uint32_t width, std::uint32_t height, const std::uint8_t* rgba) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const auto sink = [](void* context, void* data, int size) {
        static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
    };
    const int written = stbi_write_png_to_func(sink, &out, static_cast<int>(width), static_cast<int>(height), 4, rgba,
                                               static_cast<int>(width * 4u));
    out.close();
    return written != 0 && static_cast<bool>(out);
}

}  // namespace galaxy::gx
