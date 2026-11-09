#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class QuakeEffectGenerator : public LiveActor {
public:
    QuakeEffectGenerator();

    virtual void init(const TableIter&) override;

    void exeWait();
    void exeWaitInArea();
    void exeQuaking();
};
