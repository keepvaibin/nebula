#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TicoDomeLecture : public LiveActor {
public:
    TicoDomeLecture(const char*);

    virtual ~TicoDomeLecture();
    virtual void init(const TableIter&);
    virtual void appear();

    void exeWait();
    void exeMove();
    void exeMetamorphosis();

    Vec3f _8C;
    Vec3f _98;
};
