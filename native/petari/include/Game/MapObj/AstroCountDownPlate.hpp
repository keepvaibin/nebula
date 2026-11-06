#pragma once

#include "Game/Render/Material.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class AstroCountDownPlate : public LiveActor {
public:
    AstroCountDownPlate(const char*);

    virtual void init(const JMapInfoIter&);

    void exeWait();
    void exeRevival();
    void exeCountToZero();
    void initTextureAtNumLeftStar();
    void setNumLeftStar();
    void selectNrvWait();
    void setupStateWait();
    void startDemoStartCountDown();
    void startDemoLastBattle();

    CoordMatrix _8C;
    CoordMatrix _120;
    bool _1B4;
};
