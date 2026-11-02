/* The game's memory (docs/NATIVE_PORT.md, "Memory and the GPU's addresses"): one host reservation in the console's
   layout, MEM1 at offset 0 and MEM2 at 0x10000000, each larger than the console's to allow for 8-byte pointers.
   */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nebula_game_api.h>

#include "nebula_sdk.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

enum {
    kMem1Physical = 0x00000000u,
    kMem1Size = 0x04000000u, /* 64 MB (the console's 24 MB plus room for 64-bit objects) */
    kMem2Physical = 0x10000000u,
    kMem2Size = 0x10000000u, /* 256 MB (the console's 64 MB plus room) */
    kReserveSize = 0x20000000u,
};

static uint8_t* sArena;

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
    return 1;
}

/* The arena the OS hands out (OSGetMEM1ArenaLo etc.): MEM1 past the console's reserved first part; MEM2. */
void nebula_memory_arenas(void** mem1Lo, void** mem1Hi, void** mem2Lo, void** mem2Hi) {
    *mem1Lo = sArena + 0x00100000u;
    *mem1Hi = sArena + kMem1Physical + kMem1Size;
    *mem2Lo = sArena + kMem2Physical;
    *mem2Hi = sArena + kMem2Physical + kMem2Size;
}
