#pragma once

#include "Game/Enemy/BombHei.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class MapObjConnector;

class BombHeiLauncher : public LiveActor {
public:
    BombHeiLauncher(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();

    void startCountdown();
    void onSwitchA();
    void offSwitchA();

    void exeLauncherWait();
    void exeStop();

    TVec3f* getBombVelocity() const {
        return &mBomb->mVelocity;
    }

    BombHei* mBomb;
    s32 mCountdown;
    MapObjConnector* mConnector;
    bool mIsActive;
};
