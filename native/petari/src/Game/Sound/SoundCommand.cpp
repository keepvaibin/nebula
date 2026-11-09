#include "Game/Sound/SoundCommand.hpp"
#include "Game/System/NebulaArena.hpp"
#include <new>

void SoundRing::setup(NebulaArena* pArena) {
    mArena = pArena;
    Chunk* first = newChunk();
    mTail = first;
    mOldest = first;
    mHead = first;
}

SoundRing::Chunk* SoundRing::newChunk() {
    void* memory = NebulaArena::alloc(sizeof(Chunk), 64, mArena);
    if (memory == nullptr) {
        NEBULA_PANIC("no memory for the sound commands");
    }
    Chunk* chunk = new (memory) Chunk();
    chunk->mNext = nullptr;
    return chunk;
}

SoundCommand& SoundRing::next() {
    if (mTailIndex == cChunkCommands) {
        freeLeft();
        Chunk* chunk = newChunk();
        mTail->mNext = chunk;
        mTail = chunk;
        mTailIndex = 0;
    }
    return mTail->mCommands[mTailIndex];
}

u64 SoundRing::send() {
    mTailIndex++;
    mPending++;
    mWritten = mPending;
    return mPending;
}

// A chunk the reader has gone past: it read a command after the chunk's last, so it holds the next chunk.
void SoundRing::freeLeft() {
    const u64 read = mRead;
    while (mOldest != mTail && read > mOldestFirst + cChunkCommands) {
        Chunk* next = mOldest->mNext;
        mOldest->~Chunk();
        NebulaArena::free(mOldest, mArena);
        mOldest = next;
        mOldestFirst += cChunkCommands;
    }
}

const SoundCommand* SoundRing::front() {
    if (mRead == mWritten) {
        return nullptr;
    }
    if (mHeadIndex == cChunkCommands) {
        mHead = mHead->mNext;
        mHeadIndex = 0;
    }
    return &mHead->mCommands[mHeadIndex];
}

void SoundRing::done() {
    mHeadIndex++;
    mRead = mRead + 1;
}
