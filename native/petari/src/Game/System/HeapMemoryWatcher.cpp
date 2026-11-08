#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/NebulaArena.hpp"

namespace {
    void destroyArena(NebulaArena** ppArena) {
        if (*ppArena != nullptr) {
            NebulaArena::destroy(*ppArena);
            *ppArena = nullptr;
        }
    }
};  // namespace

void HeapMemoryWatcher::createFileCacheHeapOnGameHeap() {
    mFileCacheHeap = NebulaArena::create(mGameHeap);
}

void HeapMemoryWatcher::createSceneHeapOnGameHeap() {
    mSceneHeap = NebulaArena::create(mGameHeap);
}

void HeapMemoryWatcher::setCurrentHeapToPermanent() {
    MR::becomeCurrentHeap(mPermanent);
}

void HeapMemoryWatcher::setCurrentHeapToGameHeap() {
    MR::becomeCurrentHeap(NebulaArena::sSystemHeap);
}

void HeapMemoryWatcher::setCurrentHeapToSceneHeap() {
    MR::becomeCurrentHeap(mSceneHeap);
}

void HeapMemoryWatcher::destroySceneHeap() {
    ::destroyArena(&mSceneHeap);
}

// The scene's objects go before the cached files they use.
void HeapMemoryWatcher::destroyGameHeap() {
    ::destroyArena(&mSceneHeap);
    ::destroyArena(&mFileCacheHeap);
    ::destroyArena(&mGameHeap);
    createGameHeap();
}

void HeapMemoryWatcher::createHeaps() {
    mPermanent = NebulaArena::create(NebulaArena::sRootHeap);
    mPermanent->becomeSystemHeap();
    createGameHeap();
}

void HeapMemoryWatcher::createGameHeap() {
    mGameHeap = NebulaArena::create(NebulaArena::sRootHeap);
}

HeapMemoryWatcher::HeapMemoryWatcher() : mPermanent(nullptr), mGameHeap(nullptr), mFileCacheHeap(nullptr), mSceneHeap(nullptr) {
    createHeaps();
}
