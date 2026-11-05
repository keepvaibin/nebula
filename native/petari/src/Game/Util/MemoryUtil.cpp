#include "Game/System/SaveBinary.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/System/GameThreads.hpp"
#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/Util/MutexHolder.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include <JSystem/JAudio2/JASMutex.hpp>
#include "Game/System/NebulaArena.hpp"
#include <cstring>

namespace MR {
    CurrentHeapRestorer::CurrentHeapRestorer(NebulaArena* pHeap) {
        _0 = NebulaArena::currentSlot();
        MR::becomeCurrentHeap(pHeap);
    }

    CurrentHeapRestorer::~CurrentHeapRestorer() {
        MR::becomeCurrentHeap(_0);
    }

    NebulaArena* getCurrentHeap() {
        return NebulaArena::currentSlot();
    }

    NebulaArena* getSceneArchiveHeap() {
        return SingletonHolder< HeapMemoryWatcher >::get()->mFileCacheHeap;
    }

    NebulaArena* getPermanentHeap() {
        return SingletonHolder< HeapMemoryWatcher >::get()->mPermanent;
    }

    NebulaArena* getSceneHeap() {
        return SingletonHolder< HeapMemoryWatcher >::get()->mSceneHeap;
    }

    void becomeCurrentHeap(NebulaArena* pHeap) {
        pHeap->becomeCurrentHeap();
    }

    bool isEqualCurrentHeap(NebulaArena* pHeap) {
        return NebulaArena::currentSlot() == pHeap;
    }

    void copyMemory(void* pDst, const void* pSrc, u32 size) {
        memmove(pDst, pSrc, size);
    }

    void fillMemory(void* pDst, u8 ch, u32 size) {
        memset(pDst, ch, size);
    }

    void zeroMemory(void* pDst, u32 size) {
        memset(pDst, 0, size);
    }

    u32 calcCheckSum(const void* pPtr, u32 size) {
        u16 sum;
        u16 invSum;

        invSum = 0;
        sum = 0;

        // The save file's 16-bit words.
        const u8* p = static_cast< const u8* >(pPtr);
        u32 checkSize = size / sizeof(u16);

        for (int i = 0; i < checkSize; i++, p += sizeof(u16)) {
            const u16 word = MR::loadSaveU16(p);
            sum += word;
            invSum += ~word;
        }

        return (sum << 16) | invSum;
    }

};  // namespace MR
