#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LotusLeaf : public LiveActor {
public:
    LotusLeaf(const char* pName);

    virtual void init(const TableIter&) override;

    void convergeToInitPos();

    void exeWait();
    void exeWaitPlayerOn();
    void exeShake();

    Vec3f mInitPos;
    f32 mShakeSpeed;
    f32 mShakePeriod;
};
