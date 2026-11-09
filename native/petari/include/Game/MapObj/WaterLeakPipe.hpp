#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class IceStepNoSlip : public ModelObj {
public:
    IceStepNoSlip(MtxPtr);

    virtual void init(const TableIter&) override;
    virtual void appear() override;

    void exeAppear();
    void exeBreak();
};

class WaterLeakPipe : public LiveActor {
public:
    WaterLeakPipe(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override {};
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void initPipeHeight();

    void exeWait();
    void exeFreeze();

    IceStepNoSlip* mIceStep;
    f32 mPipeHeight;
    MtxPtr mTopMtx;
    MtxPtr mBottomMtx;
    Vec3f mClippingCenter;
};
