#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class NameObj;
class LiveActor;

class SpinHitController {
public:
    SpinHitController(const NameObj*, s32, bool);
    SpinHitController(const NameObj*, s32, s32, s32, f32, f32, f32, s32, bool);

    void setParameter(const NameObj*, s32, s32, s32, f32, f32, f32, s32, bool);
    void start(LiveActor*, const Vec3f&, const Vec3f&);
    void startWithoutStopScene(LiveActor*, const Vec3f&, const Vec3f&);
    bool execute(LiveActor*);
    void emitItem(LiveActor*);

    s32 mFlyStepMax;
    s32 mFlyStepMin;
    s32 mStopSceneFrames;
    f32 mGravityAccel;
    f32 mHorizontalSpeed;
    f32 mVerticalSpeed;
    bool mIsStopScene;
    bool mIsCoin;
    u32 mItemNum;
};
