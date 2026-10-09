#include "galaxy/native_api.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>
#if defined(GALAXY_YAZ0_BENCH)
bool native_yaz0_span_bench_call(galaxy::PpcContext*, galaxy::GuestMemoryV1*);
#endif
namespace {
struct Fault { std::uint32_t pc; };
struct Notices { unsigned count = 0u; std::uint32_t address = 0u, size = 0u; };
void notify(void* user, std::uint32_t address, std::uint32_t size) {
    auto& n = *static_cast<Notices*>(user); ++n.count; n.address = address; n.size = size;
}
void put32(std::vector<std::byte>& ram, std::size_t off, std::uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) ram[off+i] = std::byte((v >> (24u-8u*i)) & 255u);
}
std::vector<std::byte> stream(std::uint32_t count, bool repeat) {
    std::vector<std::byte> out(16);
    out[0]=std::byte{'Y'};out[1]=std::byte{'a'};out[2]=std::byte{'z'};out[3]=std::byte{'0'};
    put32(out,4,count);
    unsigned produced=0u;
    while(produced<count) {
        const auto code_at=out.size();out.push_back(std::byte{0});unsigned code=0u;
        for(unsigned bit=0;bit<8 && produced<count;++bit) {
            if(!repeat || produced==0u || count-produced<3u) {
                code |= 0x80u >> bit;
                out.push_back(std::byte(repeat ? 0xA5u : (produced*37u)&255u)); ++produced;
            } else {
                const auto n=std::min(273u,count-produced);
                out.push_back(std::byte(n<18u ? (n-2u)<<4u : 0u));out.push_back(std::byte{0});
                if(n>=18u)out.push_back(std::byte(n-18u));produced+=n;
            }
        }
        out[code_at]=std::byte(code);
    }
    return out;
}
std::uint64_t hash(const std::byte* p, std::size_t n) {
    std::uint64_t h=1469598103934665603ull;
    for(std::size_t i=0;i<n;++i)h=(h^static_cast<unsigned char>(p[i]))*1099511628211ull;
    return h;
}
bool run(bool fast, bool repeat, bool tail, bool partial, bool overlap, bool fault) {
    constexpr std::uint32_t base=0x80000000u, src=base+0x1000u;
    const auto dst=overlap ? src+18u : base+0x20000u;
    const auto count=fault ? 4u : overlap ? 8u : 4096u;
    auto input=stream(count,repeat);
    std::vector<std::byte> ram(0x40000u); std::copy(input.begin(),input.end(),ram.begin()+0x1000u);
    Notices notices{};
    galaxy::GuestMemoryV1 memory{};memory.user=&notices;memory.notify_write=notify;
    std::array<galaxy::GuestMemoryRegionV1,3> ranges{};
    ranges[0]={base,static_cast<std::uint32_t>(ram.size()),ram.data()};
    memory.regions=ranges.data();memory.region_count=1u;
    if(fast)memory.fast_regions[8]={static_cast<std::uint32_t>(ram.size()),ram.data()};
    if(fault) {
        ranges[0]={src,19u,ram.data()+0x1000u};
        ranges[1]={dst,4u,ram.data()+0x20000u};
        ranges[2]={base+0xD700u,256u,ram.data()+0xD700u};memory.region_count=3u;
        if(fast)memory.fast_regions[8].size=0x1013u;
    }
    galaxy::NativeServicesV1 services{};
    services.fatal=[](void*,std::uint32_t pc,const char*){throw Fault{pc};};
    galaxy::PpcContext context{};context.gpr[13]=base+0x10000u;
    context.gpr[3]=tail ? src+16u : src;context.gpr[4]=dst;
    context.gpr[30]=dst+count;context.gpr[31]=dst;
    if(partial) {put32(ram,0xD7A0u,1u);put32(ram,0xD798u,src+18u);}
    bool returned=false,caught=false;
    try {returned=tail ? galaxy::native_yaz0_decode_tail_803989BC(&context,&memory,&services,0x80001234u)
                      : galaxy::native_yaz0_decode_803989BC(&context,&memory,&services,0x80001234u);}
    catch(const Fault& f){caught=f.pc==0x80001234u;}
    bool ok=true;
    if(fault)ok=caught && !returned && notices.count==0u && ram[dst-base]==std::byte{0} && ram[dst-base+1u]==std::byte{37};
    else if(partial)ok=!returned && !caught && notices.count==1u && notices.size==1u && context.gpr[3]==src+18u && context.gpr[31]==dst+1u && context.gpr[7]==7u;
    else {
        ok=returned && !caught && notices.count==1u && notices.address==dst && notices.size==count;
        for(unsigned i=0;i<count;++i)ok &= ram[dst-base+i]==std::byte(repeat ? 0xA5u : overlap ? 0u : (i*37u)&255u);
    }
    std::printf("case fast=%u repeat=%u tail=%u partial=%u overlap=%u fault=%u ok=%u hash=%llu r3=%08x r31=%08x bits=%u code=%u notice=%u/%08x/%u\n",
        fast,repeat,tail,partial,overlap,fault,ok,static_cast<unsigned long long>(hash(ram.data(),ram.size())),context.gpr[3],context.gpr[31],context.gpr[7],context.gpr[8],notices.count,notices.address,notices.size);
    return ok;
}
bool wrap() {
    constexpr std::uint32_t src=0xFFFFFFE0u,dst=0x80020000u;
    const auto input=stream(20u,false);
    std::array<std::byte,32> high{};std::array<std::byte,32> low{};std::array<std::byte,32> dest{};std::array<std::byte,256> sda{};
    for(std::size_t i=0;i<input.size();++i)(i<32u ? high[i] : low[i-32u])=input[i];
    std::array<galaxy::GuestMemoryRegionV1,4> ranges{{{src,32u,high.data()},{0u,32u,low.data()},{dst,32u,dest.data()},{0x8000D700u,256u,sda.data()}}};
    Notices n{};galaxy::GuestMemoryV1 m{};m.regions=ranges.data();m.region_count=4;m.user=&n;m.notify_write=notify;
    galaxy::PpcContext c{};c.gpr[3]=src;c.gpr[4]=dst;c.gpr[13]=0x80010000u;
    bool ok=galaxy::native_yaz0_decode_803989BC(&c,&m,nullptr,0u) && n.count==1u && n.size==20u;
    for(unsigned i=0;i<20;++i)ok &= dest[i]==std::byte((i*37u)&255u);
    std::printf("wrap ok=%u hash=%llu\n",ok,static_cast<unsigned long long>(hash(dest.data(),dest.size())));return ok;
}
#if defined(GALAXY_YAZ0_BENCH)
void bench(bool repeat) {
    const auto input=stream(65536u,repeat);std::vector<std::byte> ram(0x40000u);
    std::copy(input.begin(),input.end(),ram.begin()+0x1000u);
    galaxy::GuestMemoryV1 m{};m.fast_regions[8]={static_cast<std::uint32_t>(ram.size()),ram.data()};
    std::uint64_t checksum=0;
    for(unsigned sample=0;sample<5;++sample) {
        const auto start=std::chrono::steady_clock::now();
        for(unsigned i=0;i<128;++i) {
            galaxy::PpcContext c{};c.gpr[3]=0x80001000u;c.gpr[4]=0x80020000u;c.gpr[13]=0x80040000u;
            const bool done=native_yaz0_span_bench_call(&c,&m);
            if (!done || c.gpr[3] != 1u) {
                std::fputs("benchmark decoder did not complete\n", stderr); std::abort();
            }
            checksum+=done+c.gpr[3]+static_cast<unsigned char>(ram[0x20000u+(i&65535u)]);
        }
        const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/128.0;
        std::printf("bench repeat=%u sample=%u us=%.6f checksum=%llu\n",repeat,sample,us,static_cast<unsigned long long>(checksum));
    }
}
#endif
}
int main(int argc,char**) {
#if defined(GALAXY_YAZ0_BENCH)
    if(argc>1){bench(false);bench(true);return 0;}
#else
    (void)argc;
#endif
    bool ok=true;
    for(bool fast:{false,true})for(bool repeat:{false,true})for(bool tail:{false,true})ok &= run(fast,repeat,tail,false,false,false);
    for(bool fast:{false,true}){ok &= run(fast,false,true,true,false,false);ok &= run(fast,false,false,false,true,false);ok &= run(fast,false,false,false,false,true);}
    ok &= wrap();return ok ? 0 : 1;
}
