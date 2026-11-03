#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class PartsModel;

class SpiderCoin : public LiveActor {
public:
    SpiderCoin(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void calcAndSetBaseMtx();

    void exeWait();
    void exeTouch();
    void exeTouchAndApart();
    bool tryRub(s32, TVec2f*);

    const TVec3f* mPointPos;
    const TVec3f* mNeutralPos;
    const TVec3f* mUp;
    u32 _98;
    u32 _9C;
    u32 _A0;
    f32 mThreadZ;
    TVec2f mPad0Velocity;
    TVec2f mPad1Velocity;
    PartsModel* mCocoonModel;

    TPos3f mBaseMtx;
};
