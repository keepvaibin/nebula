#pragma once

#include <revolution/types.h>

// The game's auxiliary memory (the console's ARAM): sound data and archives JKRAram keeps out of the main heaps. It is
// a part of the game's memory the heap watcher sets aside, addressed by offsets from its start; the first cReserved
// bytes are not handed out (the console's OS kept them). The mixer reads wave data at the same offsets.
namespace NebulaAuxMemory {
    const u32 cReserved = 0x4000;

    void init(void* pBase, u32 size);
    u8* base();
    u32 size();
    // `amount` bytes from the next free offset; returns that offset.
    u32 alloc(u32 amount);
    // A transfer between main memory and the auxiliary memory at `offset`.
    void write(u32 offset, const void* pSource, u32 length);
    void read(void* pDestination, u32 offset, u32 length);
}  // namespace NebulaAuxMemory
