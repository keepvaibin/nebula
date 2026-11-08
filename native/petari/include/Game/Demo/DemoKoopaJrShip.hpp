#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class KoopaJr;

class DemoKoopaJrShip : public LiveActor {
public:
    DemoKoopaJrShip(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void control();

    void exeAppear();
    void exeTalk();
    void exeFlyAway();
    void initAnimID(const TableIter& rIter);
    void createKoopaJrObj(const TableIter& rIter);
    bool tryDemoEnd();
    void startEntryAnim();
    void startLeaveAnim();

    KoopaJr* mKoopaJrObj;
    s32 mAnimCameraIndex;
};
