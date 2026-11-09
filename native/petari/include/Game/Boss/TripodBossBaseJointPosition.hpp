#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TripodBossBaseJointPosition : public LiveActor {
public:
    TripodBossBaseJointPosition(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;

    virtual MtxPtr getBaseMtx() const override {
        return const_cast< MtxPtr >(mMtx.mMtx);
    }

    virtual void control() override;

    s32 mID;
    Pos3f mMtx;
};
