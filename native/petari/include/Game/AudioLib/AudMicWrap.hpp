#pragma once

#include <nebula_geometry.hpp>

namespace AudMicWrap {
    void setMicMtx(MtxPtr, s32);
    void setMicEnv();
    const Vec3f& getMicPos();
};  // namespace AudMicWrap
