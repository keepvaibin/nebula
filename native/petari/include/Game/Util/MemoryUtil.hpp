#pragma once

#include <NebulaGpu.h>

namespace MR {
    void copyMemory(void* pDst, const void* pSrc, u32 size);
    void fillMemory(void* pDst, u8 ch, u32 size);
    void zeroMemory(void* pDst, u32 size);
};  // namespace MR
