#include "Game/Render/RenderTexture.hpp"
#include "Game/System/NebulaArena.hpp"

#include <new>
#include <stdlib.h>
#include <string.h>

#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

#include "Game/System/GameAssets.hpp"
#include "Game/System/LumaContentFx.hpp"
#include "Game/System/LumaContentModel.hpp"
#include "Game/System/LumaDrawInst.hpp"
#include "Game/System/LumaAnim.hpp"
#include "Game/System/LumaUi.hpp"
#include "Game/System/LumaCore.hpp"

#ifdef NEBULA_ASAN_HEAPS
// AddressSanitizer test builds: what no block holds is poisoned, so overflows and uses after free inside an arena are reported.
#include <sanitizer/asan_interface.h>
#define POISON_BLOCK(p, n) __asan_poison_memory_region((p), (size_t)(n))
#define UNPOISON_BLOCK(p, n) __asan_unpoison_memory_region((p), (size_t)(n))
#else
#define POISON_BLOCK(p, n)
#define UNPOISON_BLOCK(p, n)
#endif

NebulaArena* NebulaArena::sCurrent;
NebulaArena* NebulaArena::sRoot;

namespace {
    // Addresses each arena has to itself, not memory: only what it uses is committed.
    const u64 cSpan = 1ull << 32;
    const u64 cCommitStep = 1ull << 20;
    // The largest block: a size past it is a bad count (a corrupt file's length, a wrapped sum), and cut to 32 bits or
    // rounded to a class it would become a small block that the caller then writes past.
    const u64 cMaxBlock = 0x7FFFFF00ull;

    u32 blockSize(u64 size) {
        if (size > cMaxBlock) {
            NEBULA_PANIC("an allocation of %llu bytes", static_cast< unsigned long long >(size));
        }
        return static_cast< u32 >(size);
    }

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
    NebulaArena** sArenas;
    s32 sArenaCount;
    s32 sArenaMax;

    void addToList(NebulaArena* pArena) {
        if (sArenaCount == sArenaMax) {
            sArenaMax = sArenaMax != 0 ? sArenaMax * 2 : 64;
            sArenas = static_cast< NebulaArena** >(realloc(sArenas, sizeof(NebulaArena*) * sArenaMax));
        }
        sArenas[sArenaCount++] = pArena;
    }

    void removeFromList(NebulaArena* pArena) {
        for (s32 i = 0; i < sArenaCount; i++) {
            if (sArenas[i] == pArena) {
                sArenas[i] = sArenas[--sArenaCount];
                break;
            }
        }
    }
}  // namespace

ArenaOwned::ArenaOwned() : mArena(NebulaArena::findFromRoot(this)) {
    if (mArena != nullptr) {
        mArena->mOwned.append(this);
    }
}

ArenaOwned::~ArenaOwned() {
    if (mArena != nullptr) {
        mArena->mOwned.remove(this);
    }
}

NebulaArena::NebulaArena() {
    const NebulaHostServices* const host = nebula_host();
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
        sRoot = pArena;
        sCurrent = pArena;
        return pArena;
    }

    pArena->mParent = pParent;
    NebulaArena** ppLast = &pParent->mFirstChild;
    while (*ppLast != nullptr) {
        ppLast = &(*ppLast)->mNextSibling;
    }
    *ppLast = pArena;
    return pArena;
}

void NebulaArena::destroy(NebulaArena* pArena) {
    // Mods that keep objects forget those in it, before any of them or its memory goes.
    lumacore::released(pArena->mStart, pArena->mEnd);
    // The mods' effects on actors in it (content.fx): ended, never touched again.
    luma::content::fx::released(pArena->mStart, pArena->mEnd);
    // The mods' attached models made in it or on actors in it (content.model): ended, never touched again.
    luma::content::model::released(pArena->mStart, pArena->mEnd);
    // The mods' overrides of actors in it (draw.inst): ended, never touched again.
    luma::draw::inst::released(pArena->mStart, pArena->mEnd);
    // The mods' screens made in it (ui): ended, never touched again.
    luma::ui::released(pArena->mStart, pArena->mEnd);
    luma::anim::released(pArena->mStart, pArena->mEnd);

    // Its objects first: one that owns a child arena destroys it itself.
    pArena->destroyOwned();

    while (pArena->mFirstChild != nullptr) {
        destroy(pArena->mFirstChild);
    }

    // The textures and file views it holds go with it (its models and meshes went with its objects).
    RenderTexture::forget(reinterpret_cast< uintptr_t >(pArena->mStart), reinterpret_cast< uintptr_t >(pArena->mNext));
    GameAssets::unloadArena(pArena);

    removeFromList(pArena);
    if (pArena->mParent != nullptr) {
        NebulaArena** ppLink = &pArena->mParent->mFirstChild;
        while (*ppLink != pArena) {
            ppLink = &(*ppLink)->mNextSibling;
        }
        *ppLink = pArena->mNextSibling;
    }
    NebulaArena* const pNext = sRoot->mFirstChild;

    if (sCurrent == pArena) {
        sCurrent = pNext != nullptr ? pNext : sRoot;
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
    // At least 16-byte aligned, as a native allocator's memory is.
    u32 align = static_cast< u32 >(alignment);
    if (align < 16) {
        align = 16;
    }

    const u32 extra = align > 16 ? align : 0;
    const u32 bytes = blockSize(static_cast< u64 >(size) + sizeof(Header) + extra);
    u32 rounded;
    const u32 index = classOf(bytes, &rounded);

    u8* const pBlock = static_cast< u8* >(takeBlock(bytes));


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

    *reinterpret_cast< void** >(pBlock) = mFree[index];
    mFree[index] = pBlock;
#ifdef NEBULA_ASAN_HEAPS
    POISON_BLOCK(pBlock, sizeOfClass(index));
#endif
}

NebulaArena* NebulaArena::makeCurrent() {
    NebulaArena* const pPrevious = sCurrent;
    sCurrent = this;
    return pPrevious;
}

void NebulaArena::destroyOwned() {
    // Each one's destructor takes it out of the list.
    while (mOwned.getFirst() != mOwned.getEnd()) {
        mOwned.getFirst()->getObject()->~ArenaOwned();
    }
}

void* NebulaArena::alloc(u32 size, int alignment, NebulaArena* pArena) {
    if (pArena != nullptr) {
        return pArena->alloc(size, alignment);
    }

    return sCurrent != nullptr ? sCurrent->alloc(size, alignment) : nullptr;
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
    for (s32 i = 0; i < sArenaCount; i++) {
        NebulaArena* const pArena = sArenas[i];
        if (pData >= static_cast< void* >(pArena->mStart) && pData < static_cast< void* >(pArena->mEnd)) {
            return pArena;
        }
    }
    return nullptr;
}

void* operator new(size_t size) {
    return NebulaArena::alloc(blockSize(size), 4, nullptr);
}

void* operator new(size_t size, int alignment) {
    return NebulaArena::alloc(blockSize(size), alignment, nullptr);
}

void* operator new(size_t size, NebulaArena* pArena) {
    return NebulaArena::alloc(blockSize(size), 4, pArena);
}

void* operator new(size_t size, NebulaArena* pArena, int alignment) {
    return NebulaArena::alloc(blockSize(size), alignment, pArena);
}

void* operator new[](size_t size) {
    return NebulaArena::alloc(blockSize(size), 4, nullptr);
}

void* operator new[](size_t size, int alignment) {
    return NebulaArena::alloc(blockSize(size), alignment, nullptr);
}

void* operator new[](size_t size, NebulaArena* pArena, int alignment) {
    return NebulaArena::alloc(blockSize(size), alignment, pArena);
}

void operator delete(void* pData) {
    NebulaArena::free(pData, nullptr);
}

void operator delete[](void* pData) {
    NebulaArena::free(pData, nullptr);
}
