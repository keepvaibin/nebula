// Offline differential probe, using actual user-owned THP frames and the full
// statically translated retail decoder. No runtime PPC decoding or compilation.
// Timing excludes game scheduling, audio and presentation; it is kernel evidence.
#include "galaxy/native_api.h"
#include "galaxy/native_thp_video.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace {
galaxy::ModuleLookupFn lookup{};
galaxy::NativeServicesV1 services{};
std::atomic<std::uint32_t> pending{0};
std::vector<std::uint8_t> read(const char* path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open input");
    return {std::istreambuf_iterator<char>(file), {}};
}
std::uint32_t be32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset + 4 > data.size()) throw std::runtime_error("Input bounds");
    return (std::uint32_t(data[offset]) << 24) | (std::uint32_t(data[offset+1]) << 16) |
        (std::uint32_t(data[offset+2]) << 8) | data[offset+3];
}
void call(void*, std::uint32_t address, galaxy::PpcContext* ctx, galaxy::GuestMemoryV1* memory) {
    auto fn = lookup(address); if (!fn) throw std::runtime_error("Unknown AOT callee");
    ctx->pc = address; fn(ctx, memory, &services);
}
void checkpoint(void*, std::uint32_t, galaxy::PpcContext*, galaxy::GuestMemoryV1*) {}
}

int main(int argc, char** argv) try {
    if (argc != 4 && argc != 5) return 2; // module, DOL, THP, optional frame count
    const auto module = LoadLibraryA(argv[1]); if (!module) return 3;
    lookup = reinterpret_cast<galaxy::ModuleLookupFn>(GetProcAddress(module, "galaxy_lookup_function"));
    const auto init = reinterpret_cast<galaxy::ModuleInitFn>(GetProcAddress(module, "galaxy_module_init"));
    if (!lookup || !init) return 4;
    services.call_guest = &call;
    services.call_guest_resolved = +[](void*, std::uint32_t addr, galaxy::NativeGameFunction fn, galaxy::PpcContext* c, galaxy::GuestMemoryV1* m) {
        c->pc = addr; fn(c, m, &services);
    };
    services.call_guest_cached = +[](void*, std::uint32_t addr, std::uint32_t*, galaxy::NativeGameFunction*, galaxy::PpcContext* c, galaxy::GuestMemoryV1* m) { call(nullptr, addr, c, m); };
    services.branch_checkpoint = &checkpoint;
    services.pending_event_mask = &pending;
    services.time_base_ticks = +[](void*) -> std::uint64_t { return 0; };
    services.decrementer_written = +[](void*, std::uint32_t, std::uint32_t, std::uint64_t, std::uint32_t, galaxy::PpcContext*, galaxy::GuestMemoryV1*) {};
    services.fatal = +[](void*, std::uint32_t pc, const char* text) { std::cerr << std::hex << pc << ": " << text << '\n'; throw std::runtime_error(text); };
    if (!init(&services)) return 5;
    std::vector<std::byte> ram(24u*1024u*1024u);
    std::vector<std::byte> locked(0x4000u);
    galaxy::GuestMemoryRegionV1 regions[]{ {0x80000000u, static_cast<std::uint32_t>(ram.size()), ram.data()}, {0xC0000000u, static_cast<std::uint32_t>(ram.size()), ram.data()}, {0xE0000000u,static_cast<std::uint32_t>(locked.size()),locked.data()}, {0u,static_cast<std::uint32_t>(ram.size()),ram.data()} };
    galaxy::GuestMemoryV1 memory{}; memory.region_count=4; memory.regions=regions;
    memory.fast_regions[8]={static_cast<std::uint32_t>(ram.size()),ram.data()};
    memory.fast_regions[12]=memory.fast_regions[8];
    memory.fast_regions[0]=memory.fast_regions[8];
    memory.fast_regions[14]={static_cast<std::uint32_t>(locked.size()),locked.data()};
    const auto dol=read(argv[2]);
    for (std::size_t section=0; section<18; ++section) {
        const auto size=be32(dol,0x90+section*4); if (!size) continue;
        const auto address=be32(dol,0x48+section*4); const auto offset=be32(dol,section*4);
        if (address<0x80000000u || size>ram.size() || address-0x80000000u>ram.size()-size || offset>dol.size() || size>dol.size()-offset) return 6;
        std::memcpy(ram.data()+address-0x80000000u,dol.data()+offset,size);
    }
    const auto movie=read(argv[3]);
    if (be32(movie,0)!=0x54485000u || be32(movie,32)+28u>movie.size()) return 7;
    const auto comp=be32(movie,32);
    if (be32(movie,comp)!=2u || movie[comp+4]!=0 || movie[comp+5]!=1) return 8;
    const auto width=be32(movie,comp+20),height=be32(movie,comp+24);
    // The probe reserves separate 1-MiB planes; reject overlapping layouts.
    if (width==0 || height==0 || (width&1u) || (height&1u) || width>1024 || height>1024) return 9;
    const auto plane_size=[](auto w,auto h){ return ((w+7u)/8u)*((h+3u)/4u)*32u; };
    std::vector<std::uint8_t> y(plane_size(width,height)),u(plane_size(width/2,height/2)),v(u.size());
    std::size_t frame=be32(movie,40);
    std::uint32_t frame_size=be32(movie,24);
    const unsigned count=argc==5?static_cast<unsigned>(std::stoul(argv[4])):4u;
    if (count==0 || count>be32(movie,20)) return 13;
    for (unsigned n=0;n<count;++n) {
        const auto component_size=be32(movie,frame+8);
        const auto next=be32(movie,frame);
        if (frame+16>movie.size() || component_size>movie.size()-frame-16 || component_size>1024*1024) return 10;
        galaxy::PpcContext c{}; c.gpr[1]=0x81700000u; c.gpr[2]=0x806AB280u; c.gpr[13]=0x806A4CA0u;
        c.gpr[3]=0x81000000u; c.gpr[4]=0x81200000u; c.gpr[5]=0x81300000u; c.gpr[6]=0x81400000u; c.gpr[7]=0x81500000u;
        c.msr=0x2000u; c.hid2=0xB0000000u; c.pc=0x804514ECu;
        c.gqr[2]=0x00040004u; c.gqr[3]=0x00050005u; c.gqr[4]=0x00060006u; c.gqr[5]=0x00070007u;
        // Exact private buffer table established by THPInit (0x80454820).
        const std::uint32_t table0[]{0xE0000000u,0xE0002000u,0xE0002800u};
        const std::uint32_t table1[]{0xE0000000u,0xE0002A00u,0xE0003480u};
        for (unsigned i=0;i<3;++i) {
            galaxy::guest_store_u32(&memory,0x80624378u+i*4u,table0[i],&services,c.pc);
            galaxy::guest_store_u32(&memory,0x806244A0u+i*4u,table1[i],&services,c.pc);
        }
        galaxy::guest_store_u32(&memory,c.gpr[13]-0x2380u,1u,&services,c.pc);
        std::memcpy(ram.data()+0x1000000u,movie.data()+frame+16,component_size);
        auto start=std::chrono::steady_clock::now();
        const auto native_result=galaxy::thp::decode_video({movie.data()+frame+16,component_size},static_cast<std::uint16_t>(width),static_cast<std::uint16_t>(height),y,u,v);
        const auto native_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        start=std::chrono::steady_clock::now();
        call(nullptr,0x804514ECu,&c,&memory);
        const auto aot_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::uint64_t different=0,error_sum=0; unsigned max_error=0;
        const std::vector<std::uint8_t>* planes[]{&y,&u,&v};
        for (unsigned p=0;p<3;++p) {
            const auto w=p==0?width:width/2, h=p==0?height:height/2;
            const auto* actual=reinterpret_cast<const std::uint8_t*>(ram.data()+0x1200000u+p*0x100000u);
            for (std::uint32_t row=0;row<h;++row) for (std::uint32_t x=0;x<w;++x) {
                const auto offset=((row/4)*((w+7)/8)+x/8)*32+(row&3)*8+(x&7);
                const auto error=static_cast<unsigned>(std::abs(int(actual[offset])-int((*planes[p])[offset])));
                different+=error!=0?1:0; error_sum+=error; max_error=std::max(max_error,error);
            }
        }
        std::cout << "frame="<<n<<" size="<<component_size<<" native-status="<<native_result<<" aot-status="<<c.gpr[3]<<" different="<<different<<" max-error="<<max_error<<" error-sum="<<error_sum<<" native-ms="<<native_ms<<" aot-ms="<<aot_ms<<'\n';
        if (native_result!=0 || c.gpr[3]!=0 || different!=0) return 11;
        frame+=frame_size; frame_size=next;
    }
    FreeLibrary(module); return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 12; }
