#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpaceInner : public LiveActor {
public:
    SpaceInner(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override;
    virtual void appear() override;

    void exeAppear();
    void exeDisappear();

    void disappear();
    bool isAppeared() const;
};
