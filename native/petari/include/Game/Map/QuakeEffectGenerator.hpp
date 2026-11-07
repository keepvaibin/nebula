#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class QuakeEffectGenerator : public LiveActor {
public:
    /// @brief Creates a new `QuakeEffectGenerator`.
    QuakeEffectGenerator();

    virtual void init(const TableIter&);

    void exeWait();
    void exeWaitInArea();
    void exeQuaking();
};
