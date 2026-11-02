/* The game's memory: one block the root heap (JKRHeap::initArena) divides. Untouched pages cost address space and commit charge only,
   no physical memory. */
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

#include "nebula_module.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/mman.h>
#endif

enum { kSize = 0x18100000u /* 385 MB */ };

static uint8_t* sMemory;

int nebula_memory_init(void) {
#if defined(_WIN32)
    // A test's address for the block: far from the file views and across a 4 GB line, so an address squeezed through
    // 32 bits fails in every run, not in the one run in ten chance places it so.
#if defined(NEBULA_RELEASE)
    const char* at = NULL;
#else
    const char* at = getenv("GALAXY_TEST_MEMORY_AT");
#endif
    sMemory = (uint8_t*)VirtualAlloc(at != NULL ? (void*)(uintptr_t)strtoull(at, NULL, 16) : NULL, kSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
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
