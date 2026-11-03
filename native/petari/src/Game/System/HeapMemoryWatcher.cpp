#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/Screen/NebulaIcons.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/NebulaArena.hpp"


namespace {
    NebulaArena* createArena(NebulaArena* pHeap) {
        NebulaArena* heap = NebulaArena::create(pHeap);

        if (MR::isEqualCurrentHeap(heap)) {
            NebulaArena::sRootHeap->becomeCurrentHeap();
        }

        return heap;
    }

    void destroyHeapAndSetNULL(NebulaArena** pHeap) {
        if (*pHeap != nullptr) {
            NebulaArena::destroy(*pHeap);
            *pHeap = nullptr;
        }
    }
};  // namespace

void HeapMemoryWatcher::createFileCacheHeapOnGameHeap() {
    mFileCacheHeap = ::createArena(mGameHeap);
}

void HeapMemoryWatcher::createSceneHeapOnGameHeap() {
    mSceneHeap = ::createArena(mGameHeap);
}

void HeapMemoryWatcher::adjustStationedHeaps() {
}

void HeapMemoryWatcher::setCurrentHeapToStationedHeap() {
    MR::becomeCurrentHeap(mStationedHeap);
}

void HeapMemoryWatcher::setCurrentHeapToGameHeap() {
    MR::becomeCurrentHeap(NebulaArena::sSystemHeap);
}

void HeapMemoryWatcher::setCurrentHeapToSceneHeap() {
    MR::becomeCurrentHeap(mSceneHeap);
}

void HeapMemoryWatcher::destroySceneHeap() {
    ::destroyHeapAndSetNULL(reinterpret_cast< NebulaArena** >(&mSceneHeap));
}

void HeapMemoryWatcher::destroyGameHeap() {
    if (mSceneHeap != nullptr) {
        ::destroyHeapAndSetNULL(reinterpret_cast< NebulaArena** >(&mSceneHeap));
    }

    if (mFileCacheHeap != nullptr) {
        ::destroyHeapAndSetNULL(reinterpret_cast< NebulaArena** >(&mFileCacheHeap));
    }

    ::destroyHeapAndSetNULL(reinterpret_cast< NebulaArena** >(&mGameHeap));
    createGameHeap();
}

void HeapMemoryWatcher::createHeaps() {
    MR::CurrentHeapRestorer heapRestorer = MR::CurrentHeapRestorer(NebulaArena::sRootHeap);
    ::createArena(NebulaArena::sRootHeap)->becomeSystemHeap();
    mAudSystemHeap = ::createArena(NebulaArena::sRootHeap);
    mStationedHeap = ::createArena(NebulaArena::sRootHeap);
    createGameHeap();
}

void HeapMemoryWatcher::createGameHeap() {
    mGameHeap = ::createArena(NebulaArena::sRootHeap);
}

HeapMemoryWatcher::HeapMemoryWatcher()
    : mStationedHeap(nullptr), mGameHeap(nullptr), mFileCacheHeap(nullptr), mSceneHeap(nullptr), mAudSystemHeap(nullptr) {
    createHeaps();
}

void HeapMemoryWatcher::checkRestMemory() {
}
