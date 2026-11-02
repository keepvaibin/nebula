#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/Screen/NebulaIcons.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <revolution/os.h>
#include "JSystem/JUtility/JUTException.hpp"


namespace {
    JKRExpHeap* createExpHeap(u32 size, JKRHeap* pHeap, bool a3) NO_INLINE {
        JKRExpHeap* heap;

        if (a3) {
            void* data = new (pHeap, -4) u8[size];
            heap = JKRExpHeap::create(data, size, pHeap, true);
        } else {
            heap = JKRExpHeap::create(size, pHeap, true);
        }

        if (MR::isEqualCurrentHeap(heap)) {
            JKRHeap::sRootHeap->becomeCurrentHeap();
        }

        return heap;
    }

    JKRSolidHeap* createSolidHeap(u32 size, JKRHeap* pHeap) {
        JKRSolidHeap* heap = JKRSolidHeap::create(size, pHeap, true);

        if (MR::isEqualCurrentHeap(heap)) {
            JKRHeap::sRootHeap->becomeCurrentHeap();
        }

        return heap;
    }

    void destroyHeapAndSetNULL(JKRHeap** pHeap) {
        if (*pHeap != nullptr) {
            JKRHeap::destroy(*pHeap);
            *pHeap = nullptr;
        }
    }
};  // namespace

void HeapMemoryWatcher::createFileCacheHeapOnGameHeap(u32 size) {
    mFileCacheHeap = ::createSolidHeap(size, mGameHeap);
}

void HeapMemoryWatcher::createSceneHeapOnGameHeap() {
    mSceneHeap = ::createSolidHeap(-1, mGameHeap);
}

void HeapMemoryWatcher::adjustStationedHeaps() {
    MR::adjustHeapSize(mStationedHeap, 0);
}

void HeapMemoryWatcher::setCurrentHeapToStationedHeap() {
    MR::becomeCurrentHeap(mStationedHeap);
}

void HeapMemoryWatcher::setCurrentHeapToGameHeap() {
    MR::becomeCurrentHeap(JKRHeap::sSystemHeap);
}

void HeapMemoryWatcher::setCurrentHeapToSceneHeap() {
    MR::becomeCurrentHeap(mSceneHeap);
}

void HeapMemoryWatcher::destroySceneHeap() {
    ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mSceneHeap));
}

void HeapMemoryWatcher::destroyGameHeap() {
    if (mSceneHeap != nullptr) {
        ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mSceneHeap));
    }

    if (mFileCacheHeap != nullptr) {
        ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mFileCacheHeap));
    }

    ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mGameHeap));
    createGameHeap();
}

void HeapMemoryWatcher::createHeaps() {
    MR::CurrentHeapRestorer heapRestorer = MR::CurrentHeapRestorer(JKRHeap::sRootHeap);
    ::createExpHeap(0x40000, JKRHeap::sRootHeap, false)->becomeSystemHeap();
    mAudSystemHeap = ::createSolidHeap(0x1E0000, JKRHeap::sRootHeap);
    // Room for what stays the whole game, what it does not use given back once it is loaded (adjustStationedHeaps), and what Nebula
    // keeps (the picture font's icon sheets).
    mStationedHeap = ::createExpHeap(0x1D00000 + NebulaIcons::cStationedBytes, JKRHeap::sRootHeap, false);
    mHomeButtonLayoutHeap = ::createExpHeap(0x80000, JKRHeap::sRootHeap, false);
    createGameHeap();
}

void HeapMemoryWatcher::createGameHeap() {
    mGameHeap = ::createExpHeap(-1, JKRHeap::sRootHeap, false);
}

HeapMemoryWatcher::HeapMemoryWatcher()
    : mStationedHeap(nullptr), mGameHeap(nullptr), mFileCacheHeap(nullptr), mSceneHeap(nullptr), mHomeButtonLayoutHeap(nullptr), mAudSystemHeap(nullptr) {
    JKRHeap::setErrorHandler(HeapMemoryWatcher::memoryErrorCallback);
    createHeaps();
}

// Natively the report names the heap and the request, for the host's crash report.
void HeapMemoryWatcher::memoryErrorCallback(void* pHeap, u32 size, int alignment) {
    if (static_cast< JKRHeap* >(pHeap)->getHeapType() == 'EXPH') {
        static_cast< JKRExpHeap* >(pHeap)->reportBlocks();
    }
    JUTException::panic_f(__FILE__, 537, "out of memory: heap %p (%d bytes free), %u bytes, alignment %d", pHeap,
            static_cast< JKRHeap* >(pHeap)->getFreeSize(), size, alignment);
}

void HeapMemoryWatcher::checkRestMemory() {
}
