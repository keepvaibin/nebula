#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class GreenCaterpillarBig;
class LodCtrl;
class PartsModel;

class WormEatenPlanet : public LiveActor {
public:
    WormEatenPlanet(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();

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
