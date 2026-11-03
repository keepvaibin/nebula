#include "JSystem/JAudio2/JASHeapCtrl.hpp"
#include "Game/System/GameThreads.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include "JSystem/JAudio2/JASMutex.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaClock.hpp"

JASGenericMemPool::JASGenericMemPool() : _0(), mFreeMemCount(), mTotalMemCount(), mUsedMemCount() {
}

JASGenericMemPool::~JASGenericMemPool() {
    void* pChunk = _0;
    while (pChunk != nullptr) {
        void* pNextChunk = *(void**)pChunk;
        delete[] static_cast< u8* >(pChunk);
        pChunk = pNextChunk;
    }
}

NebulaArena* JASDram;

void JASGenericMemPool::newMemPool(u32 size, int n) {
    void* pChunk;
    for (int i = 0; i < n; i++) {
        pChunk = new (JASDram, 0) u8[size];
        *(void**)pChunk = _0;
        _0 = pChunk;
    }

    mFreeMemCount += n;
    mTotalMemCount += n;
}

void* JASGenericMemPool::alloc(u32 size) {
    // Empty: as many again, from memory that lasts as long as the sound system.
    if (_0 == nullptr) {
        const int num = mTotalMemCount != 0 ? mTotalMemCount : 16;
        for (int i = 0; i < num; i++) {
            void* pChunk = new (NebulaArena::sRootHeap, 0) u8[size];
            *(void**)pChunk = _0;
            _0 = pChunk;
        }

        mFreeMemCount += num;
        mTotalMemCount += num;
    }

    void* pChunk = _0;
    _0 = *(void**)pChunk;
    mFreeMemCount--;
    if (mUsedMemCount < mTotalMemCount - mFreeMemCount) {
        mUsedMemCount = mTotalMemCount - mFreeMemCount;
    }

    return pChunk;
}

void JASGenericMemPool::free(void* pMemory, u32 size) {
    if (pMemory == nullptr) {
        return;
    }

    void* pChunk = pMemory;
    *(void**)pChunk = _0;
    _0 = pChunk;
    mFreeMemCount++;
}

NebulaArena* JASKernel::sSystemHeap;

JASMemChunkPool< 1024, JASThreadingModel::ObjectLevelLockable >* JASKernel::sCommandHeap;

void JASKernel::setupRootHeap(NebulaArena* pHeap, u32 size) {
    sSystemHeap = NebulaArena::create(pHeap);
    sCommandHeap = new (pHeap, 0) JASMemChunkPool< 1024, JASThreadingModel::ObjectLevelLockable >();
    JASDram = pHeap;
}

NebulaArena* JASKernel::getSystemHeap() {
    return JASKernel::sSystemHeap;
}

JASMemChunkPool< 1024, JASThreadingModel::ObjectLevelLockable >* JASKernel::getCommandHeap() {
    return JASKernel::sCommandHeap;
}
