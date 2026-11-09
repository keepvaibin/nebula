#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CutBushModelObj : public LiveActor {
public:
    CutBushModelObj(const char*, const char*, bool, MtxPtr);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void calcAndSetBaseMtx() override;

    MtxPtr mMtx;
};
