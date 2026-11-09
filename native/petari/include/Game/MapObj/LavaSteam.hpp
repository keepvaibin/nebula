#pragma once
#include "Game/LiveActor/LiveActor.hpp"

class LavaSteam : public LiveActor {
public:
    LavaSteam(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;

    void startSteam();
    void exeWait();
    void exeWaitForSwitchOn();
    void exeSteam();
    void exeSteamEnd();

    Vec3f mUp;
    Vec3f mEffectScale;
};
