#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaPowerUpSwitch : public LiveActor {
public:
    KoopaPowerUpSwitch(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
};
