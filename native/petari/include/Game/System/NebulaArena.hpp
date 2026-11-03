#pragma once

// The game's memory: arenas. Each has its own stretch of addresses from the host and takes memory as it grows; destroying one
// (a scene's, say) gives all of it back at once, runs what is registered with it and destroys the arenas made under it. There
// is no size to give and nothing to run out of but the machine's memory. A pointer's arena is found by its address.

#include "Game/System/GameThreads.hpp"
#include "JSystem/JKernel/JKRDisposer.hpp"
#include "JSystem/JSupport/JSUList.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include <stddef.h>

class NebulaArena : public JKRDisposer {
public:
    // A new arena under `pParent`, which destroys it when it goes.
    static NebulaArena* create(NebulaArena* pParent);
    static void destroy(NebulaArena* pArena);

    void* alloc(u32 size, int alignment);
    void free(void* pData);
    // Everything in it goes; the arena stays.
    void freeAll();

    NebulaArena* becomeSystemHeap();
    NebulaArena* becomeCurrentHeap();

    void dispose();

    void lock() const {
        mMutex.lock();
    }

    void unlock() const {
        mMutex.unlock();
    }

    void* getStartAddr() const {
        return mStart;
    }

    void* getEndAddr() const {
        return mEnd;
    }

    // Null means the thread's current arena.
    static void* alloc(u32 size, int alignment, NebulaArena* pArena);
    static void free(void* pData, NebulaArena* pArena);
    // The arena `pData` lies in; null outside them all (a file's view, a static).
    static NebulaArena* findFromRoot(void* pData);

    static NebulaArena* getRootHeap() {
        return sRootHeap;
    }

    static NebulaArena* getSystemHeap() {
        return sSystemHeap;
    }

    static NebulaArena* getCurrentHeap() {
        return currentSlot();
    }

    static void setSystemHeap(NebulaArena* pArena) {
        sSystemHeap = pArena;
    }

    static void setCurrentHeap(NebulaArena* pArena) {
        setCurrentSlot(pArena);
    }

    // Each thread's own; a new thread and an async function start with their starter's.
    // This thread's current arena: the host keeps it (slot 0).
    static NebulaArena* currentSlot();
    static void setCurrentSlot(NebulaArena* pArena);
    static NebulaArena* sRootHeap;
    static NebulaArena* sSystemHeap;

    JSUList< JKRDisposer > mDisposerList;

private:
    enum { cClasses = 160 };

    NebulaArena();
    ~NebulaArena();

    void* takeBlock(u32 bytes);
    void commitTo(u8* pEnd);

    mutable GameMutex mMutex;
    u8* mStart;
    u8* mEnd;
    u8* mNext;       // the first byte never handed out
    u8* mCommitted;  // the first byte not in memory
    void* mFree[cClasses];
    NebulaArena* mParent;
    NebulaArena* mFirstChild;
    NebulaArena* mNextSibling;
};

void* operator new(size_t, int);
void* operator new(size_t, NebulaArena*);
void* operator new(size_t, NebulaArena*, int);
void* operator new[](size_t, int);
void* operator new[](size_t, NebulaArena*, int);
