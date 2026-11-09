#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class SpringValue;

class TripodBossBreakMovement : public LiveActor {
public:
    TripodBossBreakMovement(const char* pName);

    virtual void movement() override;

    void start(const Pos3f& rMtx, s32 jointIndex);
    void setBreakDownLevel(s32);

    void exeWait();
    void exeStartBreak();
    void exeBreak();

    Pos3f mMtx;
    SpringValue* mSpring;
    Vec3f mPos;
    Vec3f mMoment;
    Vec3f mBreakVelocity;
    Vec3f mFlyDir;
    f32 mFlyDirRandomRate;
    f32 mFlySpeed;
    f32 mDamping;
    s32 mJointIndex;
};
