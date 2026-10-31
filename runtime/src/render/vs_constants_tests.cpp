// SPDX-License-Identifier: GPL-3.0-only
// Checks VsConstantsCpuCache against a full fill_vs_constants over random GX state changes, and the octahedral
// sky-up encoding.
#include "galaxy/render/vs_constants.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
using namespace galaxy::render;
int main() {
    DrawMatrices state;
    NativeDrawState draw;
    VsConstantsCpuCache cache;
    bool ok = true;
    std::uint32_t seed = 0x43505539u;
    const auto next = [&] {seed ^= seed << 13u;seed ^= seed >> 17u;seed ^= seed << 5u;return seed;};
    unsigned scale = 1u;
    std::array<std::uint32_t,12> words{};
    std::array<float,12> values{};
    for (unsigned i = 0; i < 8192u; ++i) {
        bool frame = false;
        bool typed = false;  // a typed field changed: the backend's dirty flags then name vertex constants
        const unsigned kind=i%12u;
        for (auto& word:words)word=0x3F800000u | (next()&0x007FFFFFu);
        for (unsigned w=0;w<12u;++w)values[w]=std::bit_cast<float>(words[w]);
        switch(kind) {
        case 0: state.set_material_texture(next()%61u,values.data(),3u);break;
        case 1: state.set_material_texture(64u+next()%61u,values.data(),3u);break;
        case 2: {
            NativeLight light{};
            std::memcpy(light.position,values.data(),sizeof(light.position));
            light.color[0]=static_cast<std::uint8_t>(words[11]);
            state.set_light(next()%8u,light);
            break;
        }
        case 3: {
            const std::uint32_t a=next()&0x3FFFFFFFu;
            draw.position_matrix=static_cast<std::uint8_t>(a&0x3Fu);
            for(unsigned t=0;t<4u;++t)draw.texture_matrix[t]=static_cast<std::uint8_t>((a>>(6u+6u*t))&0x3Fu);
            typed=true;
            break;
        }
        case 4: {
            const std::uint32_t b=next()&0x00FFFFFFu;
            for(unsigned t=0;t<4u;++t)draw.texture_matrix[4u+t]=static_cast<std::uint8_t>((b>>(6u*t))&0x3Fu);
            typed=true;
            break;
        }
        case 5: draw.channel[0].lighting_enable=!draw.channel[0].lighting_enable;draw.gen.num_color_chans=1u;typed=true;break;
        case 6: for(unsigned t=0;t<6u;++t)draw.projection[t]=std::bit_cast<float>(words[t]);typed=true;break;
        case 7: words[0]=next();
            for(unsigned c=0;c<4u;++c)draw.ambient_color[0][c]=static_cast<std::uint8_t>(words[0]>>(24u-8u*c));
            typed=true;
            break;
        case 8: draw.post_texgen[next()%8u]=decode_xf_post_texgen(next()&0x3Fu);typed=true;break;
        case 9: {
            const std::uint32_t w=next()&0x3FFFFFu;
            draw.line_point={static_cast<std::uint8_t>(w),static_cast<std::uint8_t>(w>>8u),
                             static_cast<std::uint8_t>((w>>16u)&7u),static_cast<std::uint8_t>((w>>19u)&7u)};
            typed=true;
            break;
        }
        case 10: scale=1u+next()%16u;break;
        case 11: frame=true;break;
        }
        const std::uint32_t dirty=(state.consume_any_change()?kStateDirtyMatrices:0u)|(typed?kStateDirtyVertexConstants:0u);
        VertexConstants reference{};fill_vs_constants(state,draw,scale,reference);
        const auto& got=cache.update(state,draw,dirty,scale,frame);
        ok &= std::memcmp(&got,&reference,sizeof(got))==0;
        if(kind==5u)ok &= ((got.flags&2u)!=0u)==draw.channel[0].lighting_enable;
        if(!ok){std::printf("FAILED VS image at %u kind %u dirty %x\n",i,kind,dirty);return 1;}
        if((i%64u)==0u) {cache={};}
    }
    std::printf("PASS 8192 complete VS images: matrices, lights, indices, lighting, projection, colors, post texgen, raster, scale, frame/reset\n");
    // The sky up's octahedral word, decoded as the enhanced pixel shader's enh_up does: within 0.0002 rad
    // over the sphere, the poles and the octahedron's folds included; the cache carries it into every image.
    {
        double worst = 0.0;
        const auto check = [&](float x, float y, float z) {
            const float length = std::sqrt(x * x + y * y + z * z);
            const float v[3] = {x / length, y / length, z / length};
            const std::uint32_t word = encode_octahedral(v);
            const float fx = static_cast<float>(static_cast<std::int16_t>(word & 0xFFFFu)) / 32767.0f;
            const float fy = static_cast<float>(static_cast<std::int16_t>(word >> 16u)) / 32767.0f;
            float n[3] = {fx, fy, 1.0f - std::fabs(fx) - std::fabs(fy)};
            const float t = std::max(-n[2], 0.0f);
            n[0] += n[0] >= 0.0f ? -t : t;
            n[1] += n[1] >= 0.0f ? -t : t;
            // In doubles: the angle between two floats within an ulp of each other is not resolvable in float.
            const double m = std::sqrt(double(n[0]) * n[0] + double(n[1]) * n[1] + double(n[2]) * n[2]);
            const double d = (double(n[0]) * v[0] + double(n[1]) * v[1] + double(n[2]) * v[2]) / m;
            const double cx = double(n[1]) * v[2] - double(n[2]) * v[1];
            const double cy = double(n[2]) * v[0] - double(n[0]) * v[2];
            const double cz = double(n[0]) * v[1] - double(n[1]) * v[0];
            worst = std::max(worst, std::atan2(std::sqrt(cx * cx + cy * cy + cz * cz) / m, d));
        };
        for (unsigned i = 0; i < 4096u; ++i) {
            const float x = static_cast<float>(next() % 2001u) / 1000.0f - 1.0f;
            const float y = static_cast<float>(next() % 2001u) / 1000.0f - 1.0f;
            const float z = static_cast<float>(next() % 2001u) / 1000.0f - 1.0f;
            if (x * x + y * y + z * z > 1e-6f) check(x, y, z);
        }
        for (const float s : {1.0f, -1.0f}) {
            check(s, 0.0f, 0.0f);
            check(0.0f, s, 0.0f);
            check(0.0f, 0.0f, s);
            check(s, s, -1.0f);
            check(s, -s, -0.001f);
        }
        cache = {};
        cache.sky_up = 0x12345678u;
        cache.sky_up_known = 1u;
        const auto& got = cache.update(state, draw, ~0u, scale, true);
        if (worst > 2e-4 || got.sky_up != 0x12345678u || got.sky_up_known != 1u) {
            std::printf("FAILED sky up: worst %.6f rad, word %08X known %u\n", worst, got.sky_up, got.sky_up_known);
            return 1;
        }
        std::printf("PASS sky up: octahedral words within %.6f rad, carried into the image\n", worst);
    }
    return 0;
}
