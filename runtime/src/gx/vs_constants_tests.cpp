// SPDX-License-Identifier: GPL-3.0-only
// Checks VsConstantsCpuCache against a full fill_vs_constants over random GX state changes, and the octahedral
// sky-up encoding.
#include "galaxy/gx/vs_constants.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
using namespace galaxy::gx;
int main() {
    GxState state;
    VsConstantsCpuCache cache;
    bool ok = true;
    std::uint32_t seed = 0x43505539u;
    const auto next = [&] {seed ^= seed << 13u;seed ^= seed >> 17u;seed ^= seed << 5u;return seed;};
    unsigned scale = 1u;
    std::array<std::uint32_t,12> words{};
    for (unsigned i = 0; i < 8192u; ++i) {
        bool frame = false;
        const unsigned kind=i%12u;
        for (auto& word:words)word=0x3F800000u | (next()&0x007FFFFFu);
        switch(kind) {
        case 0: state.load_xf(static_cast<std::uint16_t>((next()%61u)*4u),words.data(),12u);break;
        case 1: state.load_xf(static_cast<std::uint16_t>(xf::kPostMatricesBase+(next()%61u)*4u),words.data(),12u);break;
        case 2: state.load_xf(static_cast<std::uint16_t>(xf::kLightsBase+next()%128u),words.data(),1u);break;
        case 3: state.load_cp(cp::kMatrixIndexA,next()&0x3FFFFFFFu);break;
        case 4: state.load_cp(cp::kMatrixIndexB,next()&0x00FFFFFFu);break;
        case 5: state.load_cp(cp::kVcdLo,state.cp(cp::kVcdLo)^1u);break;
        case 6: state.load_xf(xf::kProjectionBase,words.data(),6u);break;
        case 7: words[0]=next();state.load_xf(xf::kAmbientColorBase,words.data(),1u);break;
        case 8: words[0]=next()&0x3Fu;state.load_xf(static_cast<std::uint16_t>(xf::kPostTexGenBase+next()%8u),words.data(),1u);break;
        case 9: state.load_bp((std::uint32_t(bp::kSuLpSize)<<24u)|(next()&0xFFFFFFu));break;
        case 10: scale=1u+next()%16u;break;
        case 11: frame=true;break;
        }
        const auto dirty=state.consume_dirty();
        GxVsConstants reference{};fill_vs_constants(state,0u,scale,reference);
        const auto& got=cache.update(state,dirty,scale,frame);
        ok &= std::memcmp(&got,&reference,sizeof(got))==0;
        if(kind==5u)ok &= (dirty&GxState::kDirtyVsConstants)!=0u &&
            ((got.flags&1u)!=0u)==((state.cp(cp::kVcdLo)&1u)!=0u);
        if(!ok){std::printf("FAILED VS image at %u kind %u dirty %x\n",i,kind,dirty);return 1;}
        if((i%64u)==0u) {cache={};}
    }
    std::printf("PASS 8192 complete VS images: matrices, lights, indices, VCD, projection, colors, post texgen, raster, scale, frame/reset\n");
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
        const auto& got = cache.update(state, ~0u, scale, true);
        if (worst > 2e-4 || got.sky_up != 0x12345678u || got.sky_up_known != 1u) {
            std::printf("FAILED sky up: worst %.6f rad, word %08X known %u\n", worst, got.sky_up, got.sky_up_known);
            return 1;
        }
        std::printf("PASS sky up: octahedral words within %.6f rad, carried into the image\n", worst);
    }
    return 0;
}
