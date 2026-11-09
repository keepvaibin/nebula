#pragma once

#include "Game/System/NebulaAtomic.hpp"
#include <nebula_types.h>

class NebulaArena;

// Memory the game thread makes for the audio thread (players, streams): handed over with a command, given back when the audio
// thread sets the record's released flag. Game thread only.
namespace SoundMemory {
    // The sound system's arena, for as long as it lives.
    void setArena(NebulaArena* pArena);
    // Where the sounds and their children are made: a sound outlives the scene that starts it.
    NebulaArena* arena();
    // `bytes` aligned to 64, zeroed.
    void* alloc(u32 bytes);
    // The memory goes back once `rReleased` is set (the audio thread's last write to it).
    void retire(void* pMemory, const NebulaAtomic< bool >& rReleased);
    // Memory the audio thread never had goes back at once.
    void free(void* pMemory);
    // Each frame: frees what the audio thread has let go.
    void collect();
}  // namespace SoundMemory
