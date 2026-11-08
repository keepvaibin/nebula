#pragma once

#include "Game/System/NebulaAtomic.hpp"
#include <nebula_types.h>

class NebulaArena;

// One request from the game thread to the audio thread: its kind and its values (SoundEngine reads them).
struct SoundCommand {
    static const u32 cPayloadBytes = 48;

    u16 mKind;
    u16 mArg;
    u32 mValue;
    // The player or stream it is for.
    void* mTarget;
    alignas(void*) u8 mBytes[cPayloadBytes];

    template < class T >
    T& payload() {
        static_assert(sizeof(T) <= cPayloadBytes, "a command's values fit its record");
        return *reinterpret_cast< T* >(mBytes);
    }
    template < class T >
    const T& payload() const {
        static_assert(sizeof(T) <= cPayloadBytes, "a command's values fit its record");
        return *reinterpret_cast< const T* >(mBytes);
    }
};
static_assert(sizeof(SoundCommand) == 64, "a command is one cache line");

// The game thread's commands to the audio thread, in order: one writer, one reader, neither waits. The writer adds a chunk when
// the last is full and frees each chunk the reader has left; the reader never allocates.
class SoundRing {
public:
    static const u32 cChunkCommands = 255;

    void setup(NebulaArena* pArena);

    // Game thread: a record to fill, then send() makes it the reader's and returns sent().
    SoundCommand& next();
    // The record with its kind and target set, mArg and mValue 0 (the payload is left as it was).
    SoundCommand& next(u16 kind, void* pTarget) {
        SoundCommand& rCommand = next();
        rCommand.mKind = kind;
        rCommand.mArg = 0;
        rCommand.mValue = 0;
        rCommand.mTarget = pTarget;
        return rCommand;
    }
    u64 send();
    // Commands sent.
    u64 sent() const {
        return mWritten;
    }

    // Audio thread: the oldest unread command, or null; done() when it has run.
    const SoundCommand* front();
    void done();
    // Commands run, for the game thread's mirrors.
    u64 applied() const {
        return mRead;
    }

private:
    struct Chunk {
        NebulaAtomic< Chunk* > mNext;
        alignas(64) SoundCommand mCommands[cChunkCommands];
    };
    static_assert(sizeof(Chunk) == 64 * (cChunkCommands + 1), "the link on its own line, each command on one line");

    Chunk* newChunk();
    void freeLeft();

    NebulaArena* mArena = nullptr;
    // Writer.
    Chunk* mTail = nullptr;
    u32 mTailIndex = 0;
    Chunk* mOldest = nullptr;
    u64 mOldestFirst = 0;
    u64 mPending = 0;
    NebulaAtomic< u64 > mWritten = 0;
    // Reader.
    Chunk* mHead = nullptr;
    u32 mHeadIndex = 0;
    NebulaAtomic< u64 > mRead = 0;
};
