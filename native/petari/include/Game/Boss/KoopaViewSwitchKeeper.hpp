#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaViewSwitchKeeper : public LiveActor {
public:
    KoopaViewSwitchKeeper(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
};
