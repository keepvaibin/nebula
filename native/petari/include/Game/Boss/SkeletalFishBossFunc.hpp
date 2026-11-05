#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class SkeletalFishBossFunc {
public:
    static s32 getNearestGuardPosID(const Vec3f&);
    static bool copyGuardPos(Vec3f*, s32);
};