#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class KoopaRockBreak : public PartsModel {
public:
    KoopaRockBreak(LiveActor* pActor);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
};
