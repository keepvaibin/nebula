#pragma once

#include <JSystem/JKernel/JKRHeap.hpp>

class JKRExpHeap;
class JKRSolidHeap;

namespace MR {
    class CurrentHeapRestorer {
    public:
        CurrentHeapRestorer(JKRHeap* pHeap);

        ~CurrentHeapRestorer();

    private:
        /* 0x00 */ JKRHeap* _0;
    };

    JKRHeap* getCurrentHeap();
    f32 getHeapFreeRatio(JKRHeap* pHeap);
    JKRHeap* getAproposHeapForSceneArchive(f32 maxFreeSizeRate);
    JKRExpHeap* getStationedHeapNapa();
    JKRExpHeap* getStationedHeapGDDR3();
    JKRSolidHeap* getSceneHeapNapa();
    JKRSolidHeap* getSceneHeapGDDR3();
    JKRHeap* getHeapNapa(const JKRHeap* pHeap);
    JKRHeap* getHeapGDDR3(const JKRHeap* pHeap);
    void becomeCurrentHeap(JKRHeap* pHeap) NO_INLINE;
    bool isEqualCurrentHeap(JKRHeap* pHeap);
    void adjustHeapSize(JKRExpHeap* pHeap, const char* pParam2);

    /// @brief Copies `size` bytes from `pSrc` to `pDst`.
    /// @param pDst The pointer to the memory to copy to.
    /// @param pSrc The pointer to the memory to copy from.
    /// @param size The number of bytes to copy.
    void copyMemory(void* pDst, const void* pSrc, u32 size);

    /// @brief Fills `size` bytes at `pDst` with `ch`.
    /// @param pDst The pointer to the memory to fill.
    /// @param ch The value to copy.
    /// @param size The number of bytes to fill.
    void fillMemory(void* pDst, u8 ch, u32 size);

    /// @brief Fills `size` bytes at `pDst` with `0`.
    /// @param pDst The pointer to the memory to fill.
    /// @param size The number of bytes to fill.
    void zeroMemory(void* pDst, u32 size);

    /// @brief Computes a checksum from the given block of memory.
    /// @param pPtr The pointer to the memory to process.
    /// @param size The number of bytes to process.
    /// @return The computed checksum.
    u32 calcCheckSum(const void* pPtr, u32 size);

    void* allocFromWPadHeap(u32 size);
    u8 freeFromWPadHeap(void* pPtr);

};  // namespace MR
