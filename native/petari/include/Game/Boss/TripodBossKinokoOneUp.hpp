#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class BenefitItemOneUp;

class TripodBossKinokoOneUp : public LiveActor {
public:
    TripodBossKinokoOneUp(const char* pName);

    virtual void init(const TableIter&) override;

    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mMtx.mMtx);
    }

    virtual void control() override;

    void exeActive();
    void exeEnd();

    Pos3f mLocalMtx;
    Pos3f mMtx;
    BenefitItemOneUp* mOneUp;
    s32 mJointID;
};
