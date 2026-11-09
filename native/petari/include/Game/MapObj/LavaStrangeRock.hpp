#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;

class LavaStrangeRock : public LiveActor {
public:
    enum Type {
        Type_Normal,
        Type_Large,  // LavaStrangeRockL
        Type_Table,  // LavaStrangeRockTable
        Type_None,
    };

    LavaStrangeRock(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    void initModelAndClipping(const TableIter&);
    void initEffect();
    void exeWait();

    s32 mRockType;
    s32 mHitType;
    LodCtrl* mLodCtrlPlanet;
};
