#include "Game/System/HeapMemoryWatcher.hpp"
#ifdef TARGET_PC
#include "Game/Screen/NebulaIcons.hpp"
#endif
#include "Game/Util/MemoryUtil.hpp"
#include <JSystem/JKernel/JKRExpHeap.hpp>
#include <JSystem/JKernel/JKRSolidHeap.hpp>
#include <revolution/os.h>
#include <revolution/wpad.h>

JKRExpHeap* HeapMemoryWatcher::sRootHeapGDDR3;

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

JKRHeap* HeapMemoryWatcher::getHeapNapa(const JKRHeap* pHeap) {
    if (pHeap == mStationedHeapNapa || pHeap == mStationedHeapGDDR) {
        return mStationedHeapNapa;
    }

    if (pHeap == mSceneHeapNapa || pHeap == mSceneHeapGDDR) {
        return mSceneHeapNapa;
    }

    return nullptr;
}

JKRHeap* HeapMemoryWatcher::getHeapGDDR3(const JKRHeap* pHeap) {
    if (pHeap == mStationedHeapNapa || pHeap == mStationedHeapGDDR) {
        return mStationedHeapGDDR;
    }

    if (pHeap == mSceneHeapNapa || pHeap == mSceneHeapGDDR) {
        return mSceneHeapGDDR;
    }

    return nullptr;
}

void HeapMemoryWatcher::createFileCacheHeapOnGameHeap(u32 size) {
    mFileCacheHeap = ::createSolidHeap(size, mGameHeapGDDR);
}

void HeapMemoryWatcher::createSceneHeapOnGameHeap() {
    mSceneHeapNapa = ::createSolidHeap(-1, mGameHeapNapa);
    mSceneHeapGDDR = ::createSolidHeap(-1, mGameHeapGDDR);
}

void HeapMemoryWatcher::adjustStationedHeaps() {
    MR::adjustHeapSize(mStationedHeapNapa, 0);
    MR::adjustHeapSize(mStationedHeapGDDR, 0);
}

void HeapMemoryWatcher::setCurrentHeapToStationedHeap() {
    MR::becomeCurrentHeap(mStationedHeapNapa);
}

void HeapMemoryWatcher::setCurrentHeapToGameHeap() {
    MR::becomeCurrentHeap(JKRHeap::sSystemHeap);
}

void HeapMemoryWatcher::setCurrentHeapToSceneHeap() {
    MR::becomeCurrentHeap(mSceneHeapNapa);
}

void HeapMemoryWatcher::destroySceneHeap() {
    ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mSceneHeapNapa));
    ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mSceneHeapGDDR));
}

void HeapMemoryWatcher::destroyGameHeap() {
    if (mSceneHeapNapa != nullptr) {
        ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mSceneHeapNapa));
    }

    if (mSceneHeapGDDR != nullptr) {
        ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mSceneHeapGDDR));
    }

    if (mFileCacheHeap != nullptr) {
        ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mFileCacheHeap));
    }

    ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mGameHeapNapa));
    ::destroyHeapAndSetNULL(reinterpret_cast< JKRHeap** >(&mGameHeapGDDR));
    createGameHeap();
}

void HeapMemoryWatcher::createRootHeap() {
    JKRExpHeap* pHeap;
    void* newHi;
    uintptr_t arenaHi, arenaLo;

    JKRExpHeap::createRoot(1, true);
    arenaLo = reinterpret_cast< uintptr_t >(OSGetMEM2ArenaLo());
    arenaHi = reinterpret_cast< uintptr_t >(OSGetMEM2ArenaHi());
    newHi = reinterpret_cast< void* >(arenaLo + 0xE00000);
    OSSetMEM2ArenaHi(newHi);
    JKRHeap::setAltAramStartAdr(arenaLo);
    pHeap = JKRExpHeap::create(newHi, (u32)(arenaHi - reinterpret_cast< uintptr_t >(newHi)), JKRHeap::sRootHeap, true);

    if (MR::isEqualCurrentHeap(pHeap)) {
        JKRHeap::sRootHeap->becomeCurrentHeap();
    }

    HeapMemoryWatcher::sRootHeapGDDR3 = pHeap;
}

void HeapMemoryWatcher::createHeaps() {
    MR::CurrentHeapRestorer heapRestorer = MR::CurrentHeapRestorer(JKRHeap::sRootHeap);
    ::createExpHeap(0x40000, JKRHeap::sRootHeap, false)->becomeSystemHeap();
    mAudSystemHeap = ::createSolidHeap(0x1E0000, JKRHeap::sRootHeap);
    mStationedHeapNapa = ::createExpHeap(0x900000, JKRHeap::sRootHeap, false);
    JKRHeap* pRootHeapGDDR = HeapMemoryWatcher::sRootHeapGDDR3;
    u32 wpadHeapSize = OSRoundUp32B(WPADGetWorkMemorySize()) + 0xD0;
    mWPadHeap = ::createExpHeap(wpadHeapSize, pRootHeapGDDR, false);
    mHomeButtonLayoutHeap = ::createExpHeap(0x80000, HeapMemoryWatcher::sRootHeapGDDR3, false);
#ifdef TARGET_PC
    // Room for what Nebula keeps the whole game (the picture font's icon sheets).
    mStationedHeapGDDR = ::createExpHeap(0x1400000 + NebulaIcons::cStationedBytes, HeapMemoryWatcher::sRootHeapGDDR3, false);
#else
    mStationedHeapGDDR = ::createExpHeap(0x1400000, HeapMemoryWatcher::sRootHeapGDDR3, false);
#endif
    createGameHeap();
}

void HeapMemoryWatcher::createGameHeap() {
    mGameHeapNapa = ::createExpHeap(-1, JKRHeap::sRootHeap, false);
    mGameHeapGDDR = ::createExpHeap(-1, HeapMemoryWatcher::sRootHeapGDDR3, false);
}

HeapMemoryWatcher::HeapMemoryWatcher()
    : mStationedHeapNapa(nullptr), mStationedHeapGDDR(nullptr), mGameHeapNapa(nullptr), mGameHeapGDDR(nullptr), mFileCacheHeap(nullptr),
      mSceneHeapNapa(nullptr), mSceneHeapGDDR(nullptr), mWPadHeap(nullptr), mHomeButtonLayoutHeap(nullptr), mAudSystemHeap(nullptr) {
    JKRHeap::setErrorHandler(HeapMemoryWatcher::memoryErrorCallback);
    createHeaps();
}

#ifdef TARGET_PC
// Natively the report names the heap and the request, for the host's crash report.
void HeapMemoryWatcher::memoryErrorCallback(void* pHeap, u32 size, int alignment) {
    if (static_cast< JKRHeap* >(pHeap)->getHeapType() == 'EXPH') {
        static_cast< JKRExpHeap* >(pHeap)->reportBlocks();
    }
    OSPanic(__FILE__, 537, "out of memory: heap %p (%d bytes free), %u bytes, alignment %d", pHeap,
            static_cast< JKRHeap* >(pHeap)->getFreeSize(), size, alignment);
}
#else
void HeapMemoryWatcher::memoryErrorCallback(void*, u32, int) {
    OSPanic(__FILE__, 537, "");
}
#endif

void HeapMemoryWatcher::checkRestMemory() {
}
