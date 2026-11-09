#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;

class ShockWaveGenerator : public LiveActor {
public:
    ShockWaveGenerator(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWait();
    void exeDemoEcho();
    void exeGenerate();

    void startShockWave();
    void sendMsgShockWaveToNearEnemy();
    bool isHitCylinder(HitSensor* pSender, HitSensor* pReceiver) const;

    bool isPlayerInCameraStartRange() const;

    ActorCameraInfo* mCameraInfo;
};
