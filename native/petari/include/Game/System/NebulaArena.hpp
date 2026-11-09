#pragma once

// The game's memory: arenas. Each has its own stretch of addresses from the host and takes memory as it grows; destroying one
// (a scene's, say) gives all of it back at once, destroys the objects it owns and the arenas made under it. There is no size
// to give and nothing to run out of but the machine's memory. A pointer's arena is found by its address.

#include "Game/System/NebulaList.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>
#include <stddef.h>

class NebulaArena;

// An object its arena destroys when the arena is destroyed or emptied, unless it is destroyed first. One made outside every
// arena (a static, a member of such) is nobody's.
class ArenaOwned : public NebulaListLink< ArenaOwned > {
public:
    ArenaOwned();
    virtual ~ArenaOwned();

private:
    NebulaArena* mArena;
};

class NebulaArena {
public:
    // Makes an arena current for its lifetime.
    class CurrentScope {
    public:
        explicit CurrentScope(NebulaArena* pArena) : mPrevious(pArena->makeCurrent()) {
        }

        ~CurrentScope() {
            mPrevious->makeCurrent();
        }

        CurrentScope(const CurrentScope&) = delete;
        CurrentScope& operator=(const CurrentScope&) = delete;

    private:
        NebulaArena* mPrevious;
    };

    // A new arena under `pParent`, which destroys it when it goes. The root (null parent) becomes the current arena.
    static NebulaArena* create(NebulaArena* pParent);
    static void destroy(NebulaArena* pArena);

    void* alloc(u32 size, int alignment);
    void free(void* pData);

    // Returns the arena that was current.
    NebulaArena* makeCurrent();

    void* getStartAddr() const {
        return mStart;
    }

    void* getEndAddr() const {
        return mEnd;
    }

    // Null means the current arena.
    static void* alloc(u32 size, int alignment, NebulaArena* pArena);
    static void free(void* pData, NebulaArena* pArena);
    // The arena `pData` lies in; null outside them all (a file's view, a static).
    static NebulaArena* findFromRoot(void* pData);

    // Lasts as long as the process (Arenas::create makes the game's arenas under it).
    static NebulaArena* root() {
        return sRoot;
    }

    // Where plain `new` allocates. Only the game thread allocates (the sound engine's thread takes nothing).
    static NebulaArena* current() {
        return sCurrent;
    }

private:
    friend class ArenaOwned;

    enum { cClasses = 160 };

    NebulaArena();
    ~NebulaArena();

    void destroyOwned();
    void* takeBlock(u32 bytes);
    void commitTo(u8* pEnd);

    static NebulaArena* sCurrent;
    static NebulaArena* sRoot;

    NebulaList< ArenaOwned > mOwned;
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
