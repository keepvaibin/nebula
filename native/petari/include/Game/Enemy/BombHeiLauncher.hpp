#pragma once

#include "Game/Enemy/BombHei.hpp"
#include "Game/LiveActor/LiveActor.hpp"

class MapObjConnector;

class BombHeiLauncher : public LiveActor {
public:
    BombHeiLauncher(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;

    void startCountdown();
    void onSwitchA();
    void offSwitchA();

    void exeLauncherWait();
    void exeStop();

    Vec3f* getBombVelocity() const {
        return &mBomb->mVelocity;
    }

    BombHei* mBomb;
    s32 mCountdown;
    MapObjConnector* mConnector;
    bool mIsActive;
};
