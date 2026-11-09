#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FlashingCtrl : public NameObj {
public:
    FlashingCtrl(LiveActor*, bool);

    virtual void movement() override;

    void start(int timer);
    void end();
    s32 getCurrentInterval() const;
    bool isNowFlashing() const;
    bool isNowOn() const;
    void updateFlashing();

    LiveActor* mActor;
    bool mToggleDraw;
    bool mIsEnded;
    s32 mOverrideInterval;
    s32 mTimer;
    s32 mFlashStartTime;
};
