#pragma once
#include "Game/LiveActor/LiveActor.hpp"

class LavaSteam : public LiveActor {
public:
    LavaSteam(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void startClipped();
    virtual void endClipped();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void startSteam();
    void exeWait();
    void exeWaitForSwitchOn();
    void exeSteam();
    void exeSteamEnd();

    TVec3f mUp;
    TVec3f mEffectScale;
};
