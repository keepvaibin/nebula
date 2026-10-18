/* The game's memory (docs/NATIVE_PORT.md, "Memory and the GPU's addresses"): one host reservation in the console's
   physical layout, MEM1 at physical 0 and MEM2 at 0x10000000, each larger than the console's to allow for 8-byte
   pointers. OSCachedToPhysical/OSPhysicalToCached map arena pointers to those physical addresses; the game module's
   own image (static display lists and arrays the game gives GX) is a second window from kImageWindow up. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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
    kImageWindow = 0x20000000u, /* the module image, up to the 32-bit limit */
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
    /* The whole first gigabyte past the load address at most; the window only needs to cover the image. */
    sImageSize = 0x0FFFFFFFu;
#endif
    if (sImageSize > 0xFFFFFFFFu - kImageWindow) {
        sImageSize = 0xFFFFFFFFu - kImageWindow;
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

void* __NebulaFromPhysical(uint32_t paddr) {
    if (paddr < kReserveSize) {
        return sArena + paddr;
    }
    if (paddr - kImageWindow < sImageSize) {
        return (void*)(sImageBase + (paddr - kImageWindow));
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

/* Data cache maintenance has nothing to do natively, except DCZeroRange, which zeroes whole 32-byte blocks as the
   console's dcbz does. */
void DCFlushRange(void* addr, u32 nBytes) {
    (void)addr;
    (void)nBytes;
}
void DCStoreRange(void* addr, u32 nBytes) {
    (void)addr;
    (void)nBytes;
}
void DCStoreRangeNoSync(void* addr, u32 nBytes) {
    (void)addr;
    (void)nBytes;
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
    }
}
void LCEnable(void) {
}
void LCDisable(void) {
}
void PPCSync(void) {
}
