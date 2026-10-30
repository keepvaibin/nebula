/* The game's memory (docs/NATIVE_PORT.md, "Memory and the GPU's addresses"): one host reservation in the console's
   layout, MEM1 at offset 0 and MEM2 at 0x10000000, each larger than the console's to allow for 8-byte pointers. The
   game module's own image is a window in the unused span between MEM1 and MEM2 for object names
   (__NebulaObjectId). */
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

/* The game rewrote memory an image may have been handed over from (NebulaRender::memoryRewritten, DMA copies). */
void nebula_gpu_coherent(const void* addr, uint32_t nBytes) {
    const NebulaHostServices* host = nebula_host();
    if (nBytes == 0 || host == NULL || host->gpu_coherent == NULL) {
        return;
    }
    host->gpu_coherent(host->user, addr, nBytes);
}
