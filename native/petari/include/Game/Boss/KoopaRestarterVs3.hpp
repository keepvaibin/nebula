#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaRestarterVs3 : public LiveActor {
public:
    KoopaRestarterVs3(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
};
