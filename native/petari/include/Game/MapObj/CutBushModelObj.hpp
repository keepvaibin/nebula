#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CutBushModelObj : public LiveActor {
public:
    CutBushModelObj(const char*, const char*, bool, MtxPtr);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void calcAndSetBaseMtx();

    MtxPtr mMtx;
};
