#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaSwitchKeeper : public LiveActor {
public:
    KoopaSwitchKeeper(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
};
