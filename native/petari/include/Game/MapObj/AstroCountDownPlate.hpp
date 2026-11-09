#pragma once

#include "Game/Render/Material.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class AstroCountDownPlate : public LiveActor {
public:
    AstroCountDownPlate(const char*);

    virtual void init(const TableIter&) override;

    void exeWait();
    void exeRevival();
    void exeCountToZero();
    void initTextureAtNumLeftStar();
    void setNumLeftStar();
    void selectNrvWait();
    void setupStateWait();
    void startDemoStartCountDown();
    void startDemoLastBattle();

    CoordMatrix mOnesTexMtx;
    CoordMatrix mTensTexMtx;
    bool mIsLightOn;
};
