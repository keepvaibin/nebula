#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaViewSwitchKeeper : public LiveActor {
public:
    KoopaViewSwitchKeeper(const char* pName);

    virtual ~KoopaViewSwitchKeeper();
    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
};
