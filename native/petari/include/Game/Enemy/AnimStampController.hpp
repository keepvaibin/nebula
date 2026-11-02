#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <JSystem/JGeometry.hpp>

class AnimStampController : public NerveExecutor {
public:
    AnimStampController();

    void init(s32, f32, f32, f32, f32, f32, f32);
    void startAnim(const TVec3f&);
    void exeAnim();
    void exeAnimEnd();
    bool isAnimEnd() const;

    /* 0x8  */ TVec3f _8;
    TPos3f _14;
    s32 _44;
    TVec3f _50;  // unused
    TVec3f _5C;  // unused
};
