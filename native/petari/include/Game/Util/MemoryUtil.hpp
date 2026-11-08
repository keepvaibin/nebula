#pragma once

#include "Game/System/NebulaArena.hpp"

namespace MR {
    // Makes pHeap current for its lifetime.
    class CurrentHeapRestorer {
    public:
        CurrentHeapRestorer(NebulaArena* pHeap);

        ~CurrentHeapRestorer();

    private:
        NebulaArena* mPrevHeap;
    };

    NebulaArena* getCurrentHeap();
    NebulaArena* getSceneArchiveHeap();
    NebulaArena* getPermanentHeap();
    NebulaArena* getSceneHeap();
    void becomeCurrentHeap(NebulaArena* pHeap);
    bool isEqualCurrentHeap(NebulaArena* pHeap);

    void copyMemory(void* pDst, const void* pSrc, u32 size);
    void fillMemory(void* pDst, u8 ch, u32 size);
    void zeroMemory(void* pDst, u32 size);
    u32 calcCheckSum(const void* pPtr, u32 size);
};  // namespace MR
