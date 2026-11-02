#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class InformationObserver : public LiveActor {
public:
    enum TYPE {
        Type_Bee,
        Type_Teresa,
        Type_Hopper,
        Type_Fire,
        Type_Ice,
        Type_Flying,
        Type_Invincible,
        Type_LifeUp,
        Type_OneUp,
        Type_Spin,
    };

    /// @brief Creates a new `InformationObserver`.
    InformationObserver();

    virtual void init(const JMapInfoIter& rIter);

    void entry(InformationObserver::TYPE, LiveActor*);
    void exeWait();
    void exeDisp();

private:
    TYPE mType;
    bool _90;
    LiveActor* _94;
    s32 mDisplayFrame;
};

namespace InformationObserverFunction {
    void explainBee();
    void explainTeresa();
    void explainHopper();
    void explainFire();
    void explainIce();
    void explainFlying();
    void explainInvincible();
    void explainLifeUp();
    void explainOneUp();
    void explainSpin(LiveActor*);
};  // namespace InformationObserverFunction
