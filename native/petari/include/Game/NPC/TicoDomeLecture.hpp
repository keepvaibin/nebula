#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TicoDomeLecture : public LiveActor {
public:
    TicoDomeLecture(const char*);

    virtual void init(const TableIter&);
    virtual void appear();

    void exeWait();
    void exeMove();
    void exeMetamorphosis();

    Vec3f mInitPos;
    Vec3f mInitRotation;
};
