#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class GreenCaterpillarBig;
class LodCtrl;
class PartsModel;

class WormEatenPlanet : public LiveActor {
public:
    WormEatenPlanet(const char*);

    virtual void init(const TableIter&) override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;

    void exeFirstWait();
    void exeSecondWait();
    void exeThirdWait();
    void exeFourthWait();
    void initWormEatenHill();
    bool tryGenerateWormEatenHill();
    void startSecondDemo();
    void startThirdDemo();

    GreenCaterpillarBig* mCaterpillar;
    PartsModel** mWormEatenHill;
    CollisionParts* mSecondHill;
    CollisionParts* mThirdHill;
    CollisionParts* mFourthHill;
    LodCtrl* mLodCtrl;
};
