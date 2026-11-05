#pragma once

#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

namespace MR {
    inline void getUpVecFromQuat(Vec3f* pOut, const Quat4f& rQuat) {
        pOut->set< f32 >((2.0f * (rQuat.x * rQuat.y)) - (2.0f * (rQuat.w * rQuat.z)),
                         ((1.0f - (2.0f * (rQuat.x * rQuat.x))) - (2.0f * (rQuat.z * rQuat.z))),
                         ((2.0f * (rQuat.y * rQuat.z)) + (2.0f * (rQuat.w * rQuat.x))));
    }

};  // namespace MR
