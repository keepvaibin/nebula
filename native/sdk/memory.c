/* The game's memory: one block the root heap (JKRHeap::initArena) divides. Untouched pages cost address space and commit charge only,
   no physical memory. */
#include <stdint.h>
#include <stddef.h>

#include "nebula_sdk.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

enum { kSize = 0x20000000u /* 512 MB */ };

static uint8_t* sMemory;

int nebula_memory_init(void) {
#if defined(_WIN32)
    sMemory = (uint8_t*)VirtualAlloc(NULL, kSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    return sMemory != NULL;
#else
    void* p = mmap(NULL, kSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    sMemory = p != MAP_FAILED ? (uint8_t*)p : NULL;
    return sMemory != NULL;
#endif
}

void nebula_memory_arena(void** pStart, void** pEnd) {
    *pStart = sMemory;
    *pEnd = sMemory + kSize;
}
