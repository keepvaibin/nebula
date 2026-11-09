#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PartsModel;

class WatchTowerRotateStep : public LiveActor {
public:
    WatchTowerRotateStep(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;

    void initLift(const TableIter& rIter);
    void attachLift();
    void rotate(f32 degree);

    void exeWait();
    void exeMoveStart();
    void exeMove();

    Vec3f mRotateVec;
    PartsModel** mLift;
};
