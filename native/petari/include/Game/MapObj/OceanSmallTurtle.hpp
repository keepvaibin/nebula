#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class MapPartsRailMover;
class OceanSmallTurtle : public LiveActor {
public:
    OceanSmallTurtle(const char*);

    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void exeWait();
    void exeFloatSignal();
    void exeFloatUp();
    void exeCatchRail();
    void exeRailMove();
    void startFloat();
    bool boundFloater();
    bool catchNearestRailPos();
    void floatFloater();
    bool isNeedsForBound();
    bool isEndSpringWater();

private:
    MapPartsRailMover* mRailMover;
    f32 mFloatUpSpeed;
    f32 mAccelRate;
    Vec3f mStartNearestRailPos;
    f32 mFloatDelayTimer;  // frames, counted in a float
    f32 mArg1;
    s32 mBoundTimer;
    Vec3f mMercatorPos;
    Vec3f mInfoRotate;
    f32 mBoundDisplacement;
    Vec3f mRailClippingPos;
    f32 mRailClippingRadius;
    f32 mStartRotationX;
    u8 mIsOnPlayer;
    u8 isBound;
    u8 isStartFloat;
    Pos3f mEffectMtx;
};
