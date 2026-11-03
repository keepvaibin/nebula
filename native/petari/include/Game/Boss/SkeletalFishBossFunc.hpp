#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class SkeletalFishBossFunc {
public:
    static s32 getNearestGuardPosID(const TVec3f&);
    static bool copyGuardPos(TVec3f*, s32);
};