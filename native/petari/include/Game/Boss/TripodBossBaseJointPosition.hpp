#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TripodBossBaseJointPosition : public LiveActor {
public:
    TripodBossBaseJointPosition(const char* pName);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();

    virtual MtxPtr getBaseMtx() const {
        return const_cast< MtxPtr >(mMtx.mMtx);
    }

    virtual void control();

    s32 mID;
    Pos3f mMtx;
};
