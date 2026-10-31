// The cook check (D-01, E-06): every texture the cooker wrote (nebula-import with NEBULA_COOK_DUMP=<file>) against
// the renderer's own decode of the disc's bytes, level by level, texel by texel. Prints a line per mismatch, then
// the totals; exit code 1 when any texel differs.
//   verify_cooked_textures <dump file>
#include "galaxy/render/texture_cache.h"
#include "nebula_cooked_texture.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <vector>

using namespace galaxy::render;

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: verify_cooked_textures <dump file>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<char> all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto* p = reinterpret_cast<const std::uint8_t*>(all.data());
    const auto* end = p + all.size();
    std::uint64_t images = 0, levels_checked = 0, bad_images = 0;
    std::map<unsigned, std::uint64_t> layouts;
    while (p + 32 <= end) {
        std::uint32_t h[8];
        std::memcpy(h, p, sizeof(h));
        p += 32;
        const auto format = static_cast<TexFormat>(h[0]);
        const std::uint32_t width = h[1], height = h[2], levels = h[3];
        p += h[5];  // the palette
        const std::uint8_t* image = p;
        p += h[6];
        const std::uint8_t* record = p;
        p += h[7];
        if (p > end) {
            std::fprintf(stderr, "the dump ends inside an image\n");
            return 2;
        }
        const NebulaTexture* t = nebula_texture_record(record);
        ++images;
        if (t == nullptr || t->width != width || t->height != height || t->levels != levels || t->size != h[7]) {
            std::printf("image %llu: bad record header\n", static_cast<unsigned long long>(images));
            ++bad_images;
            continue;
        }
        ++layouts[t->layout];
        const std::uint8_t* src = image;
        const std::uint8_t* texels = record + NEBULA_TEXTURE_HEADER;
        bool bad = false;
        for (std::uint32_t l = 0; l < levels && !bad; ++l) {
            const std::uint32_t lw = std::max(1u, width >> l), lh = std::max(1u, height >> l);
            std::vector<RGBA8> out(static_cast<std::size_t>(lw) * lh);
            switch (format) {
            case TexFormat::I4: case TexFormat::I8: case TexFormat::IA4: case TexFormat::IA8:
                detail::decode_intensity_tiles(format, src, lw, lh, out.data());
                break;
            case TexFormat::RGB565: case TexFormat::RGB5A3:
                detail::decode_color_tiles(format, src, lw, lh, out.data());
                break;
            case TexFormat::RGBA8:
                detail::decode_rgba8_tiles(src, lw, lh, out.data());
                break;
            case TexFormat::CMPR:
                detail::decode_cmpr_tiles(src, lw, lh, out.data());
                break;
            default:
                // Colour-indexed images (none on the disc): the cooker's palette arithmetic is its own tests'.
                std::printf("image %llu: format %u not checked here\n", static_cast<unsigned long long>(images), h[0]);
                bad = true;
                break;
            }
            if (bad) {
                break;
            }
            src += texture_source_bytes(format, lw, lh, 1u);
            std::vector<RGBA8> expanded(out.size());
            detail::expand_record_level(t->layout, texels, lw, lh, expanded.data());
            for (std::size_t i = 0; i < out.size(); ++i) {
                const RGBA8& cooked = expanded[i];
                const RGBA8& d = out[i];
                if (cooked.r != d.r || cooked.g != d.g || cooked.b != d.b || cooked.a != d.a) {
                    std::printf("image %llu (format %u %ux%u) level %u texel %zu: cooked %u,%u,%u,%u decoded %u,%u,%u,%u\n",
                                static_cast<unsigned long long>(images), h[0], width, height, l, i, cooked.r, cooked.g,
                                cooked.b, cooked.a, d.r, d.g, d.b, d.a);
                    bad = true;
                    break;
                }
            }
            texels += nebula_texture_level_bytes(lw, lh, 1u, t->layout);
            ++levels_checked;
        }
        bad_images += bad ? 1u : 0u;
    }
    std::printf("%llu images, %llu levels checked, %llu differ; layouts RGBA8 %llu, L8 %llu, LA8 %llu, BLOCK4 %llu\n",
                static_cast<unsigned long long>(images), static_cast<unsigned long long>(levels_checked),
                static_cast<unsigned long long>(bad_images), static_cast<unsigned long long>(layouts[0]),
                static_cast<unsigned long long>(layouts[1]), static_cast<unsigned long long>(layouts[2]),
                static_cast<unsigned long long>(layouts[3]));
    return bad_images == 0 && images != 0 ? 0 : 1;
}
