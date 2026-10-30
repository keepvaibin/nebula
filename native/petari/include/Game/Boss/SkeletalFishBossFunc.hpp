#pragma once

#include <JSystem/JGeometry/TVec.hpp>
#include <revolution/gx.h>
#include <revolution/mtx.h>
#include <revolution/os.h>

class SkeletalFishBossFunc {
public:
    static s32 getNearestGuardPosID(const TVec3f&);
    static bool copyGuardPos(TVec3f*, s32);
};