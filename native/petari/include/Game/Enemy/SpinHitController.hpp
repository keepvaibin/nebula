#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class NameObj;
class LiveActor;

class SpinHitController {
public:
    SpinHitController(const NameObj*, s32 itemNum, bool isCoin);
    SpinHitController(const NameObj*, s32 flyStepMax, s32 flyStepMin, s32 stopSceneFrames, f32 gravityAccel, f32 horizontalSpeed, f32 verticalSpeed,
                      s32 itemNum, bool isCoin);

    void setParameter(const NameObj*, s32 flyStepMax, s32 flyStepMin, s32 stopSceneFrames, f32 gravityAccel, f32 horizontalSpeed, f32 verticalSpeed,
                      s32 itemNum, bool isCoin);
    void start(LiveActor*, const Vec3f& rFrom, const Vec3f& rTo);
    void startWithoutStopScene(LiveActor*, const Vec3f& rFrom, const Vec3f& rTo);
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
