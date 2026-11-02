#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class SpringWaterFloaterSpot : public LiveActor {
public:
    SpringWaterFloaterSpot(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void exeWait();
    void exeFloatSignal();
    void exeFloatUp();
    void exeFloatEnd();

    void startFloat();
    bool boundFloater();
    void floatFloater();
    bool isModelLongSide();
    bool isNeedsForBound();

    f32 _8C;
    f32 _90;
    u32 _94;
    u32 _98;
    u32 _9C;
    f32 mHeight;
    f32 _A4;
    s32 mBoundStep;
    TVec3f _AC;
    TVec3f mInitialRotation;
    f32 mBoundSize;
    bool _C8;
    bool mNeedsBound;
    bool _CA;
    TPos3f _CC;
    s32 mFloatTimer;
};
