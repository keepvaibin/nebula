/* The game's memory: one block the root heap (JKRHeap::initArena) divides. Untouched pages cost address space and commit charge only,
   no physical memory. */
#include <stdint.h>
#include <stddef.h>

#include <nebula_game_api.h>

#include "nebula_module.h"


enum { kSize = 0x18100000u /* 385 MB */ };

static uint8_t* sMemory;

int nebula_memory_init(void) {
    const NebulaHostServices* host = nebula_host();
    sMemory = (uint8_t*)host->memory_block(host->user, kSize);
    return sMemory != NULL;
}

void nebula_memory_arena(void** pStart, void** pEnd) {
    *pStart = sMemory;
    *pEnd = sMemory + kSize;
}
