#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class MapPartsRailMover;
class OceanSmallTurtle : public LiveActor {
public:
    OceanSmallTurtle(const char*);

    virtual void init(const JMapInfoIter& rIter);
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
    TVec3f mStartNearestRailPos;
    f32 mFloatDelayTimer;  // used as a timer. a float for some reason
    f32 mArg1;
    s32 mBoundTimer;
    TVec3f mMercatorPos;
    TVec3f mInfoRotate;
    f32 mBoundDisplacement;
    TVec3f mRailClippingPos;
    f32 mRailClippingRadius;
    f32 mStartRotationX;
    u8 mIsOnPlayer;
    u8 isBound;
    u8 isStartFloat;
    TPos3f mEffectMtx;
};
