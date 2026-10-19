/* The game's memory (docs/NATIVE_PORT.md, "Memory and the GPU's addresses"): one host reservation in the console's
   physical layout, MEM1 at physical 0 and MEM2 at 0x10000000, each larger than the console's to allow for 8-byte
   pointers. OSCachedToPhysical/OSPhysicalToCached map arena pointers to those physical addresses; the game module's
   own image (static display lists and arrays the game gives GX) is a second window, in the unused span between MEM1
   and MEM2, so every address the GPU sees stays below 512 MB (a texture's base is 24 bits of address >> 5). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

#if defined(_WIN32)
#include <windows.h>
extern IMAGE_DOS_HEADER __ImageBase;
#else
#include <dlfcn.h>
#include <sys/mman.h>
#endif

enum {
    kMem1Physical = 0x00000000u,
    kMem1Size = 0x04000000u, /* 64 MB (the console's 24 MB plus room for 64-bit objects) */
    kMem2Physical = 0x10000000u,
    kMem2Size = 0x10000000u, /* 256 MB (the console's 64 MB plus room) */
    kReserveSize = 0x20000000u,
    kImageWindow = 0x08000000u, /* the module image, between MEM1's end and MEM2 */
    kImageWindowSize = 0x08000000u,
    kStackPoolSize = 0x02000000u, /* 32 MB at the top of MEM2: game threads' native stacks */
};

static uint8_t* sArena;
static uintptr_t sImageBase;
static uintptr_t sImageSize;

uint8_t* nebula_arena_base(void) {
    return sArena;
}

void nebula_image_range(uintptr_t* base, uintptr_t* size) {
    *base = sImageBase;
    *size = sImageSize;
}

void nebula_memory_regions(NebulaMemoryRegion* out, int* count) {
    out[0].physical = kMem1Physical;
    out[0].size = kMem1Size;
    out[0].host = sArena + kMem1Physical;
    out[1].physical = kMem2Physical;
    out[1].size = kMem2Size;
    out[1].host = sArena + kMem2Physical;
    out[2].physical = kImageWindow;
    out[2].size = (uint32_t)sImageSize;
    out[2].host = (uint8_t*)sImageBase;
    *count = 3;
}

static void find_image(void) {
#if defined(_WIN32)
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)&__ImageBase + __ImageBase.e_lfanew);
    sImageBase = (uintptr_t)&__ImageBase;
    sImageSize = nt->OptionalHeader.SizeOfImage;
#else
    Dl_info info;
    if (dladdr((void*)&find_image, &info) != 0) {
        sImageBase = (uintptr_t)info.dli_fbase;
    }
    /* The window's whole span; it only needs to cover the image. */
    sImageSize = kImageWindowSize;
#endif
    if (sImageSize > kImageWindowSize) {
        nebula_fatal("the module image (%llu bytes) is larger than its physical window", (unsigned long long)sImageSize);
    }
}

/* The arena is aligned to its own size, so an arena pointer's low 29 bits are its physical address: what the game
   derives from an address's low bits (hash slots, alignment tests) has the console's value. The reservation is address
   space only. */
int nebula_memory_init(void) {
#if defined(_WIN32)
    uint8_t* base = (uint8_t*)VirtualAlloc(NULL, 2 * (SIZE_T)kReserveSize, MEM_RESERVE, PAGE_NOACCESS);
    if (base == NULL) {
        return 0;
    }
    sArena = (uint8_t*)(((uintptr_t)base + kReserveSize - 1) & ~(uintptr_t)(kReserveSize - 1));
    if (VirtualAlloc(sArena + kMem1Physical, kMem1Size, MEM_COMMIT, PAGE_READWRITE) == NULL ||
        VirtualAlloc(sArena + kMem2Physical, kMem2Size, MEM_COMMIT, PAGE_READWRITE) == NULL) {
        return 0;
    }
#else
    const size_t span = 2 * (size_t)kReserveSize;
    uint8_t* base = (uint8_t*)mmap(NULL, span, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (base == (uint8_t*)MAP_FAILED) {
        return 0;
    }
    sArena = (uint8_t*)(((uintptr_t)base + kReserveSize - 1) & ~(uintptr_t)(kReserveSize - 1));
    if (sArena > base) {
        munmap(base, (size_t)(sArena - base));
    }
    if (base + span > sArena + kReserveSize) {
        munmap(sArena + kReserveSize, (size_t)(base + span - (sArena + kReserveSize)));
    }
    if (mprotect(sArena + kMem1Physical, kMem1Size, PROT_READ | PROT_WRITE) != 0 ||
        mprotect(sArena + kMem2Physical, kMem2Size, PROT_READ | PROT_WRITE) != 0) {
        return 0;
    }
#endif
    find_image();
    return 1;
}

uint32_t __NebulaToPhysical(const void* addr) {
    const uintptr_t a = (uintptr_t)addr;
    const uintptr_t arena = (uintptr_t)sArena;
    if (a - arena < kReserveSize) {
        return (uint32_t)(a - arena);
    }
    if (a - sImageBase < sImageSize) {
        return kImageWindow + (uint32_t)(a - sImageBase);
    }
    if (addr == NULL) {
        return 0;
    }
    nebula_fatal("OSCachedToPhysical: %p is outside the game's arena and image", addr);
    return 0;
}

uint32_t __NebulaObjectId(const void* object) {
    const uintptr_t a = (uintptr_t)object;
    if (a - (uintptr_t)sArena < kReserveSize) {
        return (uint32_t)(a - (uintptr_t)sArena);
    }
    if (a - sImageBase < sImageSize) {
        return kImageWindow + (uint32_t)(a - sImageBase);
    }
    /* Outside the game's memory (none of its objects are): the address folded, never 0 unless NULL. */
    return (uint32_t)(a ^ (a >> 32)) | (a != 0 ? 0x80000000u : 0u);
}

void* __NebulaFromPhysical(uint32_t paddr) {
    if (paddr - kImageWindow < sImageSize) {
        return (void*)(sImageBase + (paddr - kImageWindow));
    }
    if (paddr < kReserveSize) {
        return sArena + paddr;
    }
    nebula_fatal("OSPhysicalToCached: physical address 0x%08X has no mapping", paddr);
    return NULL;
}

/* The arena the OS hands out (OSGetMEM1ArenaLo etc.): MEM1 past low memory, the boot stack and the console's
   reserved first part; MEM2 below the stack pool. */
void nebula_memory_arenas(void** mem1Lo, void** mem1Hi, void** mem2Lo, void** mem2Hi) {
    *mem1Lo = sArena + 0x00100000u;
    *mem1Hi = sArena + kMem1Physical + kMem1Size;
    *mem2Lo = sArena + kMem2Physical;
    *mem2Hi = sArena + kMem2Physical + kMem2Size - kStackPoolSize;
}

uint8_t* nebula_stack_pool(size_t* size) {
    *size = kStackPoolSize;
    return sArena + kMem2Physical + kMem2Size - kStackPoolSize;
}

void nebula_memory_guard(void* p, size_t size) {
#if defined(_WIN32)
    DWORD old;
    if (!VirtualProtect(p, size, PAGE_NOACCESS, &old)) {
        nebula_fatal("could not guard %p (+%zu)", p, size);
    }
#else
    if (mprotect(p, size, PROT_NONE) != 0) {
        nebula_fatal("could not guard %p (+%zu)", p, size);
    }
#endif
}

/* Data cache maintenance. Natively no cache needs flushing, but the game flushes every range it writes for the GPU
   (display lists, vertex and texture data) before the GPU reads it, as the console requires: each flush tells the
   renderer to drop what it holds of that memory. Invalidation has nothing to do; DCZeroRange zeroes whole 32-byte
   blocks as the console's dcbz does. */
void nebula_gpu_coherent(const void* addr, uint32_t nBytes) {
    const NebulaHostServices* host = nebula_host();
    if (nBytes == 0 || host == NULL || host->gpu_coherent == NULL) {
        return;
    }
    const uintptr_t a = (uintptr_t)addr;
    if (a - (uintptr_t)sArena < kReserveSize) {
        host->gpu_coherent(host->user, (uint32_t)(a - (uintptr_t)sArena), nBytes);
    } else if (a - sImageBase < sImageSize) {
        host->gpu_coherent(host->user, kImageWindow + (uint32_t)(a - sImageBase), nBytes);
    }
}
void DCFlushRange(void* addr, u32 nBytes) {
    nebula_gpu_coherent(addr, nBytes);
}
void DCStoreRange(void* addr, u32 nBytes) {
    nebula_gpu_coherent(addr, nBytes);
}
void DCStoreRangeNoSync(void* addr, u32 nBytes) {
    nebula_gpu_coherent(addr, nBytes);
}
void DCInvalidateRange(void* addr, u32 nBytes) {
    (void)addr;
    (void)nBytes;
}
void DCZeroRange(void* addr, u32 nBytes) {
    const uintptr_t start = (uintptr_t)addr & ~(uintptr_t)31;
    const uintptr_t end = ((uintptr_t)addr + nBytes + 31) & ~(uintptr_t)31;
    if (nBytes != 0) {
        memset((void*)start, 0, end - start);
        nebula_gpu_coherent((const void*)start, (uint32_t)(end - start));
    }
}
void LCEnable(void) {
}
void LCDisable(void) {
}
void PPCSync(void) {
}
