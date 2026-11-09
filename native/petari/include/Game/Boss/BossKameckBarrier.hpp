#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BossKameckBarrier : public LiveActor {
public:
    BossKameckBarrier(const char* pName, const char* pModelName);

    virtual void init(const TableIter& rIter) override;

    void exeWait();

    const char* mModelName;
};
