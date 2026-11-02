#pragma once

#include <JSystem/JGeometry/TMatrix.hpp>

class LiveActor;
class JMapInfoIter;
class SpinDriverShootPath;
class SpinDriverPathDrawer;

class ReturnDemoRailMove {
public:
    ReturnDemoRailMove(LiveActor*, LiveActor*, const JMapInfoIter&, bool, TPos3f*);

    void posToStart();
    void posToEnd();
    void offPathDraw();
    s32 getDemoFlyBrakeFrame() const;
    void calcPathPosDir(TVec3f*, TVec3f*, f32) const;
    void setupPathDrawForGraneStarReturnDemo();
    void start();
    void update(s32, s32);

    inline TPos3f* getMtx() {
        return mTransform;
    }

    LiveActor* mDemoStarter;  // Can be either StarReturnDemoStarter or GrandStarReturnDemoStarter
    LiveActor* mPowerStar;
    bool mIsGrandStar;
    TPos3f* mTransform;
    SpinDriverShootPath* mShootPath;
    SpinDriverPathDrawer* mPathDrawer;
    TVec3f mForward;
};
