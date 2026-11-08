#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class MarioParts : public PartsModel {
public:
    MarioParts(LiveActor*, const char*, const char*, MtxPtr);
    MarioParts(LiveActor*, const char*, const char*, bool, MtxPtr, MtxPtr);

    virtual void init(const TableIter&);

    const char* mEffectName;  // the effect keeper's name, set before init
};
