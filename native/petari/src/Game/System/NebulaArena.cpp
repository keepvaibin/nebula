#include "Game/System/NebulaArena.hpp"

#include <JSystem/JUtility/JUTNativeLog.hpp>
#include <new>
#include <stdlib.h>
#include <string.h>

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

#include "Game/System/GameAssets.hpp"

#ifdef NEBULA_ASAN_HEAPS
// AddressSanitizer test builds: what no block holds is poisoned, so overflows and uses after free inside an arena are reported.
#include <sanitizer/asan_interface.h>
#define POISON_BLOCK(p, n) __asan_poison_memory_region((p), (size_t)(n))
#define UNPOISON_BLOCK(p, n) __asan_unpoison_memory_region((p), (size_t)(n))
#else
#define POISON_BLOCK(p, n)
#define UNPOISON_BLOCK(p, n)
#endif

NebulaArena* NebulaArena::currentSlot() {
    const NebulaHostServices* const pHost = nebula_host();
    return static_cast< NebulaArena* >(pHost->thread_slots(pHost->user)[0]);
}

void NebulaArena::setCurrentSlot(NebulaArena* pArena) {
    const NebulaHostServices* const pHost = nebula_host();
    pHost->thread_slots(pHost->user)[0] = pArena;
}
NebulaArena* NebulaArena::sRootHeap;
NebulaArena* NebulaArena::sSystemHeap;

namespace {
    // Addresses each arena has to itself, not memory: only what it uses is committed.
    const u64 cSpan = 1ull << 32;
    const u64 cCommitStep = 1ull << 20;

    // Before each block: its class, and how far back its start lies.
    struct alignas(16) Header {
        u32 mClass;
        u32 mBack;
    };

    // Sizes up to 1 KB in steps of 16 (64 classes), then four to each doubling.
    u32 classOf(u32 bytes, u32* pRounded) {
        if (bytes <= 1024) {
            const u32 index = (bytes + 15) / 16;
            *pRounded = index * 16;
            return index - 1;
        }

        u32 shift = 10;
        while ((bytes - 1) >> (shift + 1)) {
            shift++;
        }
        const u32 step = 1u << (shift - 2);
        const u32 quarter = ((bytes - 1) >> (shift - 2)) - 3;
        *pRounded = (quarter + 4) * step;
        return 64 + (shift - 10) * 4 + quarter;
    }

#ifdef NEBULA_ASAN_HEAPS
    u32 sizeOfClass(u32 index) {
        if (index < 64) {
            return (index + 1) * 16;
        }

        const u32 shift = 10 + (index - 64) / 4;
        return ((index - 64) % 4 + 4) * (1u << (shift - 2));
    }
#endif

    // Every arena, for findFromRoot: a flat list by address is all it takes, as none lies in another.
    GameMutex sListMutex;
    NebulaArena** sArenas;
    s32 sArenaCount;
    s32 sArenaMax;

    void addToList(NebulaArena* pArena) {
        sListMutex.lock();
        if (sArenaCount == sArenaMax) {
            sArenaMax = sArenaMax != 0 ? sArenaMax * 2 : 64;
            sArenas = static_cast< NebulaArena** >(realloc(sArenas, sizeof(NebulaArena*) * sArenaMax));
        }
        sArenas[sArenaCount++] = pArena;
        sListMutex.unlock();
    }

    void removeFromList(NebulaArena* pArena) {
        sListMutex.lock();
        for (s32 i = 0; i < sArenaCount; i++) {
            if (sArenas[i] == pArena) {
                sArenas[i] = sArenas[--sArenaCount];
                break;
            }
        }
        sListMutex.unlock();
    }
}  // namespace

NebulaArena::NebulaArena() : JKRDisposer() {
    const NebulaHostServices* const host = nebula_host();
    mMutex.init();
    mStart = static_cast< u8* >(host->memory_reserve(host->user, cSpan));
    if (mStart == nullptr) {
        NEBULA_PANIC("no addresses left for an arena");
    }
    mEnd = mStart + cSpan;
    mNext = mStart;
    mCommitted = mStart;
    memset(mFree, 0, sizeof(mFree));
    mParent = nullptr;
    mFirstChild = nullptr;
    mNextSibling = nullptr;
}

NebulaArena::~NebulaArena() {
    const NebulaHostServices* const host = nebula_host();
    UNPOISON_BLOCK(mStart, mCommitted - mStart);
    host->memory_release(host->user, mStart, cSpan);
}

NebulaArena* NebulaArena::create(NebulaArena* pParent) {
    void* const pMemory = malloc(sizeof(NebulaArena));
    if (pMemory == nullptr) {
        NEBULA_PANIC("out of memory for an arena");
    }

    NebulaArena* const pArena = new (pMemory) NebulaArena();
    addToList(pArena);

    if (pParent == nullptr) {
        sRootHeap = pArena;
        sSystemHeap = pArena;
        setCurrentSlot(pArena);
        return pArena;
    }

    pArena->mParent = pParent;
    sListMutex.lock();
    NebulaArena** ppLast = &pParent->mFirstChild;
    while (*ppLast != nullptr) {
        ppLast = &(*ppLast)->mNextSibling;
    }
    *ppLast = pArena;
    sListMutex.unlock();

    if (sSystemHeap == sRootHeap) {
        sSystemHeap = pArena;
    }
    if (currentSlot() == sRootHeap) {
        setCurrentSlot(pArena);
    }

    return pArena;
}

void NebulaArena::destroy(NebulaArena* pArena) {
    // Its objects first: one that owns a child arena destroys it itself.
    pArena->dispose();

    while (pArena->mFirstChild != nullptr) {
        destroy(pArena->mFirstChild);
    }

    // What the renderer and the file views hold from this arena go with it.
    const NebulaHostServices* const host = nebula_host();
    host->shape_forget(host->user, reinterpret_cast< uintptr_t >(pArena->mStart), reinterpret_cast< uintptr_t >(pArena->mNext));
    GameAssets::unloadHeap(pArena);

    removeFromList(pArena);
    sListMutex.lock();
    if (pArena->mParent != nullptr) {
        NebulaArena** ppLink = &pArena->mParent->mFirstChild;
        while (*ppLink != pArena) {
            ppLink = &(*ppLink)->mNextSibling;
        }
        *ppLink = pArena->mNextSibling;
    }
    NebulaArena* const pNext = sRootHeap->mFirstChild;
    sListMutex.unlock();

    if (currentSlot() == pArena) {
        setCurrentSlot(pNext != nullptr ? pNext : sRootHeap);
    }
    if (sSystemHeap == pArena) {
        sSystemHeap = pNext != nullptr ? pNext : sRootHeap;
    }

    pArena->~NebulaArena();
    ::free(pArena);
}

void NebulaArena::commitTo(u8* pEnd) {
    if (pEnd > mEnd) {
        NEBULA_PANIC("an arena used all of its %llu bytes of addresses", static_cast< unsigned long long >(cSpan));
    }

    const u64 bytes = (static_cast< u64 >(pEnd - mCommitted) + cCommitStep - 1) & ~(cCommitStep - 1);
    const NebulaHostServices* const host = nebula_host();
    if (!host->memory_commit(host->user, mCommitted, bytes)) {
        NEBULA_PANIC("out of memory: %llu bytes more for an arena", static_cast< unsigned long long >(bytes));
    }
    POISON_BLOCK(mCommitted, bytes);
    mCommitted += bytes;
}

void* NebulaArena::takeBlock(u32 bytes) {
    u32 rounded;
    const u32 index = classOf(bytes, &rounded);

    if (mFree[index] != nullptr) {
        void* const pBlock = mFree[index];
        mFree[index] = *static_cast< void** >(pBlock);
        UNPOISON_BLOCK(pBlock, rounded);
        // As memory new from the host is: what a field left unset reads must not depend on what lay here before.
        memset(pBlock, 0, rounded);
        return pBlock;
    }

    u8* const pBlock = mNext;
    if (pBlock + rounded > mCommitted) {
        commitTo(pBlock + rounded);
    }
    mNext = pBlock + rounded;
    UNPOISON_BLOCK(pBlock, rounded);
    return pBlock;
}

void* NebulaArena::alloc(u32 size, int alignment) {
    // At least 16-byte aligned, as a native allocator's memory is. A negative alignment asked for a heap's tail; nothing here has one.
    u32 align = alignment < 0 ? static_cast< u32 >(-alignment) : static_cast< u32 >(alignment);
    if (align < 16) {
        align = 16;
    }

    const u32 extra = align > 16 ? align : 0;
    const u32 bytes = size + static_cast< u32 >(sizeof(Header)) + extra;
    u32 rounded;
    const u32 index = classOf(bytes, &rounded);

    lock();
    u8* const pBlock = static_cast< u8* >(takeBlock(bytes));
    unlock();

#if !defined(NEBULA_RELEASE)
    // Tests (GALAXY_TEST_ARENA_FILL=a5): every block starts as this byte, so what reads a field left unset shows in a picture.
    static u32 sFill = 0;
    static bool sFillRead = false;
    if (!sFillRead) {
        sFill = nebula_host()->option(nebula_host()->user, NEBULA_OPTION_TEST_ARENA_FILL);
        sFillRead = true;
    }
    if (sFill != 0) {
        memset(pBlock, static_cast< int >(sFill & 0xFF), rounded);
    }
#endif

    const uintptr_t data = (reinterpret_cast< uintptr_t >(pBlock) + sizeof(Header) + align - 1) & ~static_cast< uintptr_t >(align - 1);
    Header* const pHeader = reinterpret_cast< Header* >(data) - 1;
    pHeader->mClass = index;
    pHeader->mBack = static_cast< u32 >(reinterpret_cast< u8* >(pHeader) - pBlock);
    POISON_BLOCK(pBlock, data - reinterpret_cast< uintptr_t >(pBlock));
    POISON_BLOCK(reinterpret_cast< u8* >(data) + size, reinterpret_cast< uintptr_t >(pBlock) + rounded - data - size);
    return reinterpret_cast< void* >(data);
}

void NebulaArena::free(void* pData) {
    if (pData == nullptr) {
        return;
    }

    Header* const pHeader = static_cast< Header* >(pData) - 1;
    const u32 index = pHeader->mClass;
    u8* const pBlock = reinterpret_cast< u8* >(pHeader) - pHeader->mBack;

    lock();
    *reinterpret_cast< void** >(pBlock) = mFree[index];
    mFree[index] = pBlock;
#ifdef NEBULA_ASAN_HEAPS
    POISON_BLOCK(pBlock, sizeOfClass(index));
#endif
    unlock();
}

void NebulaArena::freeAll() {
    dispose();

    while (mFirstChild != nullptr) {
        destroy(mFirstChild);
    }

    const NebulaHostServices* const host = nebula_host();
    host->shape_forget(host->user, reinterpret_cast< uintptr_t >(mStart), reinterpret_cast< uintptr_t >(mNext));
    GameAssets::unloadHeap(this);

    lock();
    UNPOISON_BLOCK(mStart, mCommitted - mStart);
    host->memory_decommit(host->user, mStart, mCommitted - mStart);
    mCommitted = mStart;
    mNext = mStart;
    memset(mFree, 0, sizeof(mFree));
    unlock();
}

NebulaArena* NebulaArena::becomeSystemHeap() {
    NebulaArena* const pPrevious = sSystemHeap;
    sSystemHeap = this;
    return pPrevious;
}

NebulaArena* NebulaArena::becomeCurrentHeap() {
    NebulaArena* const pPrevious = currentSlot();
    setCurrentSlot(this);
    return pPrevious;
}

void NebulaArena::dispose() {
    while (mDisposerList.getFirst() != mDisposerList.getEnd()) {
        JSUListIterator< JKRDisposer > iterator = mDisposerList.getFirst();
        iterator->~JKRDisposer();
    }
}

void* NebulaArena::alloc(u32 size, int alignment, NebulaArena* pArena) {
    if (pArena != nullptr) {
        return pArena->alloc(size, alignment);
    }

    return currentSlot() != nullptr ? currentSlot()->alloc(size, alignment) : nullptr;
}

void NebulaArena::free(void* pData, NebulaArena* pArena) {
    // A file's view lies in no arena: it stays until its arena goes, as a freed block's bytes did.
    NebulaArena* const pOwner = findFromRoot(pData);
    if (pOwner != nullptr) {
        pOwner->free(pData);
    }
    (void)pArena;
}

NebulaArena* NebulaArena::findFromRoot(void* pData) {
    NebulaArena* pFound = nullptr;
    sListMutex.lock();
    for (s32 i = 0; i < sArenaCount; i++) {
        NebulaArena* const pArena = sArenas[i];
        if (pData >= static_cast< void* >(pArena->mStart) && pData < static_cast< void* >(pArena->mEnd)) {
            pFound = pArena;
            break;
        }
    }
    sListMutex.unlock();
    return pFound;
}

void* operator new(size_t size) {
    return NebulaArena::alloc(static_cast< u32 >(size), 4, nullptr);
}

void* operator new(size_t size, int alignment) {
    return NebulaArena::alloc(static_cast< u32 >(size), alignment, nullptr);
}

void* operator new(size_t size, NebulaArena* pArena) {
    return NebulaArena::alloc(static_cast< u32 >(size), 4, pArena);
}

void* operator new(size_t size, NebulaArena* pArena, int alignment) {
    return NebulaArena::alloc(static_cast< u32 >(size), alignment, pArena);
}

void* operator new[](size_t size) {
    return NebulaArena::alloc(static_cast< u32 >(size), 4, nullptr);
}

void* operator new[](size_t size, int alignment) {
    return NebulaArena::alloc(static_cast< u32 >(size), alignment, nullptr);
}

void* operator new[](size_t size, NebulaArena* pArena, int alignment) {
    return NebulaArena::alloc(static_cast< u32 >(size), alignment, pArena);
}

void operator delete(void* pData) {
    NebulaArena::free(pData, nullptr);
}

void operator delete[](void* pData) {
    NebulaArena::free(pData, nullptr);
}
