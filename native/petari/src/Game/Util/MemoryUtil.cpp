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
        _0 = NebulaArena::sCurrentHeap;
        MR::becomeCurrentHeap(pHeap);
    }

    CurrentHeapRestorer::~CurrentHeapRestorer() {
        MR::becomeCurrentHeap(_0);
    }

    NebulaArena* getCurrentHeap() {
        return NebulaArena::sCurrentHeap;
    }

    NebulaArena* getAproposHeapForSceneArchive(f32 maxFreeSizeRate) {
        (void)maxFreeSizeRate;
        return SingletonHolder< HeapMemoryWatcher >::get()->mFileCacheHeap;
    }

    NebulaArena* getStationedHeap() {
        return SingletonHolder< HeapMemoryWatcher >::get()->mStationedHeap;
    }

    NebulaArena* getSceneHeap() {
        return SingletonHolder< HeapMemoryWatcher >::get()->mSceneHeap;
    }

    void becomeCurrentHeap(NebulaArena* pHeap) {
        pHeap->becomeCurrentHeap();
    }

    bool isEqualCurrentHeap(NebulaArena* pHeap) {
        return NebulaArena::sCurrentHeap == pHeap;
    }

    void copyMemory(void* pDst, const void* pSrc, u32 size) {
        u8* pDstBytes = static_cast< u8* >(pDst);
        const u8* pSrcBytes = static_cast< const u8* >(pSrc);
        u32 srcAlignment = reinterpret_cast< uintptr_t >(pSrc) & 3;
        u32 dstAlignment = reinterpret_cast< uintptr_t >(pDst) & 3;

        if (srcAlignment == dstAlignment && (size & 0xF) == 0) {
            const u32* pSrcWords = reinterpret_cast< const u32* >(pSrcBytes);
            u32* pDstWords = reinterpret_cast< u32* >(pDstBytes);

            for (u32 count = size / 16; count != 0; count--) {
                u32 word3, word2, word1, word0;
                word0 = *pSrcWords++;
                word1 = *pSrcWords++;
                word2 = *pSrcWords++;
                word3 = *pSrcWords++;
                *pDstWords++ = word0;
                *pDstWords++ = word1;
                *pDstWords++ = word2;
                *pDstWords++ = word3;
            }
        } else if (srcAlignment == dstAlignment && size >= 16) {
            if (srcAlignment != 0) {
                for (u8 count = 4 - srcAlignment; count != 0; count--) {
                    *pDstBytes++ = *pSrcBytes++;
                    size--;
                }
            }

            while (size >= 4) {
                *reinterpret_cast< u32* >(pDstBytes) = *reinterpret_cast< const u32* >(pSrcBytes);
                size -= 4;
                pSrcBytes += 4;
                pDstBytes += 4;
            }

            if (size != 0) {
                for (; size != 0; size--) {
                    *pDstBytes++ = *pSrcBytes++;
                }
            }
        } else {
            for (; size != 0; size--) {
                *pDstBytes++ = *pSrcBytes++;
            }
        }
    }

    void fillMemory(void* pDst, u8 ch, u32 size) {
        if (ch == 0) {
            zeroMemory(pDst, size);
        } else {
            memset(pDst, ch, size);
        }
    }

    void zeroMemory(void* pDst, u32 size) {
        u8* pDstBytes = static_cast< u8* >(pDst);
        u32 alignment = reinterpret_cast< uintptr_t >(pDst) & 3;

        if ((reinterpret_cast< uintptr_t >(pDst) & 0x1F) == 0 && (size & 0x1F) == 0) {
            memset(pDst, 0, size);
        } else if (alignment == 0 && (size & 0xF) == 0) {
            for (u32 count = size / 16; count != 0; count--) {
                u32* pWords = reinterpret_cast< u32* >(pDstBytes);
                pWords[0] = 0;
                pWords[1] = 0;
                pWords[2] = 0;
                pWords[3] = 0;
                pDstBytes += 16;
            }
        } else if (size >= 16) {
            if (alignment != 0) {
                for (u8 count = 4 - alignment; count != 0; count--) {
                    *pDstBytes++ = 0;
                    size--;
                }
            }

            while (size >= 4) {
                *reinterpret_cast< u32* >(pDstBytes) = 0;
                pDstBytes += 4;
                size -= 4;
            }

            if (size != 0) {
                for (; size != 0; size--) {
                    *pDstBytes++ = 0;
                }
            }
        } else {
            for (; size != 0; size--) {
                *pDstBytes++ = 0;
            }
        }
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
