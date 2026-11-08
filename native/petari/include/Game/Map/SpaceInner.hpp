#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpaceInner : public LiveActor {
public:
    SpaceInner(const char* pName);

    virtual void init(const TableIter&);
    virtual void calcAnim();
    virtual void appear();

    void exeAppear();
    void exeDisappear();

    void disappear();
    bool isAppeared() const;
};
