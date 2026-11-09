#pragma once

#include "Game/NameObj/NameObj.hpp"
#include <nebula_geometry.hpp>

class Coin;
class Spine;

class TripodBossCoin : public NameObj {
public:
    TripodBossCoin(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    void exeNonActive();
    void exeActive();
    void exeEnd();

    Coin* mCoin;
    Spine* mSpine;
    Pos3f mPlacementMtx;
    s32 mJointID;
};
