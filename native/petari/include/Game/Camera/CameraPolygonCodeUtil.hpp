#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

namespace MR {
    void initCameraCodeCollection(const char*, s32);
    void registerCameraCode(u32);
    void termCameraCodeCollection();
};  // namespace MR
