#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PointGravityCreator;

class MarioLauncherAttractor : public LiveActor {
public:
    MarioLauncherAttractor(const char* pName);

    virtual void init(const TableIter& rIter) override;

    void exeWait();

    PointGravityCreator* mGravityCreator;
};
