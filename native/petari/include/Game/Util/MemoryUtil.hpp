#pragma once

#include "Game/System/NebulaArena.hpp"

class NebulaArena;
class NebulaArena;

namespace MR {
    class CurrentHeapRestorer {
    public:
        CurrentHeapRestorer(NebulaArena* pHeap);

        ~CurrentHeapRestorer();

    private:
        NebulaArena* _0;
    };

    NebulaArena* getCurrentHeap();
    NebulaArena* getAproposHeapForSceneArchive(f32 maxFreeSizeRate);
    NebulaArena* getStationedHeap();
    NebulaArena* getSceneHeap();
    void becomeCurrentHeap(NebulaArena* pHeap);
    bool isEqualCurrentHeap(NebulaArena* pHeap);

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


};  // namespace MR
