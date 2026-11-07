#pragma once

#include <nebula_geometry.hpp>

class LiveActor;
class TableIter;
class SpinDriverShootPath;
class SpinDriverPathDrawer;

class ReturnDemoRailMove {
public:
    ReturnDemoRailMove(LiveActor*, LiveActor*, const TableIter&, bool, Pos3f*);

    void posToStart();
    void posToEnd();
    void offPathDraw();
    s32 getDemoFlyBrakeFrame() const;
    void calcPathPosDir(Vec3f*, Vec3f*, f32) const;
    void setupPathDrawForGraneStarReturnDemo();
    void start();
    void update(s32, s32);

    inline Pos3f* getMtx() {
        return mTransform;
    }

    LiveActor* mDemoStarter;  // Can be either StarReturnDemoStarter or GrandStarReturnDemoStarter
    LiveActor* mPowerStar;
    bool mIsGrandStar;
    Pos3f* mTransform;
    SpinDriverShootPath* mShootPath;
    SpinDriverPathDrawer* mPathDrawer;
    Vec3f mForward;
};
