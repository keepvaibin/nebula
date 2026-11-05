#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <nebula_geometry.hpp>

class AnimStampController : public NerveExecutor {
public:
    AnimStampController();

    void init(s32, f32, f32, f32, f32, f32, f32);
    void startAnim(const Vec3f&);
    void exeAnim();
    void exeAnimEnd();
    bool isAnimEnd() const;

    /* 0x8  */ Vec3f _8;
    Pos3f _14;
    s32 _44;
    Vec3f _50;  // unused
    Vec3f _5C;  // unused
};
