#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class BossKameckBarrier : public LiveActor {
public:
    BossKameckBarrier(const char*, const char*);

    virtual void init(const TableIter&);
    virtual void control();

    void exeWait();

    const char* mModelName;
};
