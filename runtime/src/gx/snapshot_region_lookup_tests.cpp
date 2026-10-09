// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/gx/snapshot_region_lookup.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <limits>
#include <vector>

using namespace galaxy;
namespace {
__declspec(noinline) const std::byte* original(
    const GuestMemoryV1& memory, std::uint32_t address, std::uint32_t size) {
    if (size == 0u) return nullptr;
    if (const auto* fast = resolve_guest_fast(const_cast<GuestMemoryV1*>(&memory),address,size)) return fast;
    const auto end = static_cast<std::uint64_t>(address)+size;
    for (std::uint32_t i=0;i<memory.region_count;++i) {
        const auto& r=memory.regions[i];
        if (r.host_base && address>=r.guest_base && end<=static_cast<std::uint64_t>(r.guest_base)+r.size)
            return r.host_base+(address-r.guest_base);
    }
    return nullptr;
}
__declspec(noinline) const std::byte* candidate(
    const GuestMemoryV1& memory, std::uint32_t address, std::uint32_t size, bool certified) {
    if (size == 0u) return nullptr;
    if (const auto* fast=resolve_guest_fast(const_cast<GuestMemoryV1*>(&memory),address,size)) return fast;
    if (certified) return gx::detail::resolve_sorted_snapshot_region(
        {memory.regions,memory.region_count},address,size);
    return original(memory,address,size);
}
std::uint32_t next(std::uint32_t& state) {
    state^=state<<13;state^=state>>17;state^=state<<5;return state;
}
}
int main() {
    std::vector<std::byte> backing(65536u);
    std::vector<GuestMemoryRegionV1> regions;
    for(std::uint32_t i=0;i<1024u;++i)
        regions.push_back({0x80000000u+i*64u,32u,backing.data()+i*64u});
    GuestMemoryV1 memory{};
    memory.regions=regions.data();memory.region_count=static_cast<std::uint32_t>(regions.size());
    if(!gx::detail::snapshot_regions_are_disjoint_sorted(regions)) return 1;
    unsigned checks=0;
    std::uint32_t random=0x81358abcu;
    for(unsigned i=0;i<1000000u;++i) {
        const auto address=i%17u==0u?next(random):0x80000000u+(next(random)%65537u);
        const auto size=i%31u==0u?std::numeric_limits<std::uint32_t>::max():next(random)%80u;
        if(original(memory,address,size)!=candidate(memory,address,size,true)) return 2;
        ++checks;
    }
    // First-match overlap/order, malformed geometry and alias boundaries must
    // never receive the certificate. Pointer equality checks actual ownership.
    for(unsigned mode=0;mode<5u;++mode) {
        auto bad=regions;
        if(mode==0u)std::swap(bad[0],bad[1]);
        if(mode==1u)bad[1].guest_base=bad[0].guest_base+16u;
        if(mode==2u)bad[1].size=0u;
        if(mode==3u)bad[1].host_base=nullptr;
        if(mode==4u){bad.back().guest_base=0xfffffff0u;bad.back().size=32u;}
        if(gx::detail::snapshot_regions_are_disjoint_sorted(bad))return 3;
        GuestMemoryV1 view{};view.regions=bad.data();view.region_count=static_cast<std::uint32_t>(bad.size());
        for(std::uint32_t offset=0;offset<256u;++offset)for(std::uint32_t size=0;size<64u;++size){
            const auto address=0x80000000u+offset;
            if(original(view,address,size)!=candidate(view,address,size,false))return 4;
            ++checks;
        }
    }
    // Fast table remains first even when its bytes differ from region storage.
    std::vector<std::byte> fast(65536u);
    memory.fast_regions[8]={static_cast<std::uint32_t>(fast.size()),fast.data()};
    if(candidate(memory,0x80000040u,16u,true)!=fast.data()+64u)return 5;
    memory.fast_regions[8]={};
    std::array<GuestMemoryRegionV1,2> adjacent{{{0u,32u,backing.data()},
                                             {32u,32u,backing.data()+64u}}};
    if(!gx::detail::snapshot_regions_are_disjoint_sorted(adjacent) ||
       gx::detail::resolve_sorted_snapshot_region(adjacent,31u,2u)!=nullptr ||
       gx::detail::resolve_sorted_snapshot_region(adjacent,0u,32u)!=backing.data() ||
       gx::detail::resolve_sorted_snapshot_region(adjacent,32u,32u)!=backing.data()+64u ||
       gx::detail::resolve_sorted_snapshot_region(adjacent,64u,1u)!=nullptr) return 6;
    std::array<GuestMemoryRegionV1,1> edge{{{0xffffff00u,256u,backing.data()}}};
    if(!gx::detail::snapshot_regions_are_disjoint_sorted(edge) ||
       gx::detail::resolve_sorted_snapshot_region(edge,0xffffffffu,1u)!=backing.data()+255u ||
       gx::detail::resolve_sorted_snapshot_region(edge,0xffffffffu,2u)!=nullptr) return 7;
    printf("snapshot lookup differential checks=%u PASS\n",checks);
    // Separate synthetic cost, not gameplay/FPS: same1024fragment view, fixed
    // addresses in every rotated block; observable checksum prevents elision.
    std::array<std::uint32_t,4096> queries{};
    for(auto& q:queries)q=0x80000000u+(next(random)%1024u)*64u;
    for(unsigned block=0;block<6u;++block)for(unsigned pass=0;pass<2u;++pass){
        const bool binary=(pass^(block&1u))!=0;
        std::uintptr_t sum=0u;
        const auto begin=std::chrono::steady_clock::now();
        for(unsigned i=0;i<500000u;++i){
            const auto* p=binary?candidate(memory,queries[i%queries.size()],16u,true)
                                :original(memory,queries[i%queries.size()],16u);
            sum+=reinterpret_cast<std::uintptr_t>(p);
        }
        const auto ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-begin).count()/500000.0;
        printf("lookup-cost block=%u binary=%u ns/call=%.4f checksum=%llu\n",block,binary,ns,static_cast<unsigned long long>(sum));
    }
    return 0;
}
