#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class SpringWaterFloaterSpot : public LiveActor {
public:
    SpringWaterFloaterSpot(const char*);

    virtual void init(const TableIter&);
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
    Vec3f _AC;
    Vec3f mInitialRotation;
    f32 mBoundSize;
    bool _C8;
    bool mNeedsBound;
    bool _CA;
    Pos3f _CC;
    s32 mFloatTimer;
};
