#include "Game/Util/MemoryUtil.hpp"
#include <cstring>

namespace MR {
    void copyMemory(void* pDst, const void* pSrc, u32 size) {
        memmove(pDst, pSrc, size);
    }

    void fillMemory(void* pDst, u8 ch, u32 size) {
        memset(pDst, ch, size);
    }

    void zeroMemory(void* pDst, u32 size) {
        memset(pDst, 0, size);
    }

};  // namespace MR
