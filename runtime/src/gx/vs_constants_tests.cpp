// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/vs_constants.h"
#include <cstdio>
#include <chrono>
#include <cstdint>
#include <cstring>
using namespace galaxy::gx;
#if defined(GALAXY_VS_CONSTANTS_BENCH)
void baseline_vs(const GxState*, GxVsConstants*, unsigned);
void candidate_vs(const GxState*, VsConstantsCpuCache*, unsigned);
#endif
int main(int argc, char**) {
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
#if defined(GALAXY_VS_CONSTANTS_BENCH)
        GxVsConstants control{};baseline_vs(&state,&control,scale);
        ok &= std::memcmp(&got,&control,sizeof(got))==0;
#endif
        if(!ok){std::printf("FAILED VS image at %u kind %u dirty %x\n",i,kind,dirty);return 1;}
        // Matrix selectors at the clamped tail and post-row wrap boundaries.
        if((i%64u)==0u) {cache={};}
    }
    std::printf("PASS 8192 complete VS images: matrices, lights, indices, VCD, projection, colors, post texgen, raster, scale, frame/reset\n");
#if defined(GALAXY_VS_CONSTANTS_BENCH)
    if(argc>1) {
        std::uint64_t checksum=0u;
        GxVsConstants control{};cache.update(state,~0u,scale,true);
        for(unsigned order=0;order<2;++order)for(unsigned version=0;version<2;++version)for(unsigned sample=0;sample<5;++sample) {
            const auto selected=version^order;
            const auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<2000000u;++i) {
                if(selected==0u)baseline_vs(&state,&control,scale);else candidate_vs(&state,&cache,scale);
                const auto& value=selected==0u ? control : cache.constants;
                checksum+=std::bit_cast<std::uint32_t>(value.inline_pos_matrix[0][0])+value.flags;
            }
            const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/2000000.0;
            std::printf("version=%u sample=%u ns=%.6f checksum=%llu\n",selected,sample,ns,static_cast<unsigned long long>(checksum));
        }
    }
#else
    (void)argc;
#endif
    return 0;
}
