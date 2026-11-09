#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TombSpiderThreadAttacher : public LiveActor {
public:
    TombSpiderThreadAttacher(LiveActor*, const char*, f32, f32);

    virtual void init(const TableIter& rIter) override;

    void exeFree();
    void exeAttach();

    MtxPtr mJointMtx;
    f32 mRadius;
    f32 mZOffset;
};
