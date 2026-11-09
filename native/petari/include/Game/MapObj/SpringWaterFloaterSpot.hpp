#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class SpringWaterFloaterSpot : public LiveActor {
public:
    SpringWaterFloaterSpot(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void exeWait();
    void exeFloatSignal();
    void exeFloatUp();
    void exeFloatEnd();

    void startFloat();
    bool boundFloater();
    void floatFloater();
    bool isModelLongSide();
    bool isNeedsForBound();

    f32 mRiseSpeed;
    f32 mRiseAccel;
    f32 mHeight;
    f32 mWaterCheckOffset;
    s32 mBoundStep;
    Vec3f mFloatPos;
    Vec3f mInitialRotation;
    f32 mBoundSize;
    bool mIsOnPlayer;
    bool mNeedsBound;
    bool mHasSurfaced;
    Pos3f mFloatMtx;
    s32 mFloatTimer;
};
