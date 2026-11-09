#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TalkMessageCtrl;
class Tamakoro;

class TamakoroTutorial : public LiveActor {
public:
    TamakoroTutorial(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    bool isNeedTutorial();
    void requestStart();
    void requestEnd();
    void exeFirst();
    void exeFirstForceTalk();
    void exeWaitRideTalk();
    void exeWaitRide();
    void exeWaitRaiseTalk();
    void exeWaitRaiseStable();
    void exeRaiseOk();
    void exeRaiseOkTalk();
    void exeFinalTalk();
    void exeWait();
    void exeTrample();
    void exeRecover();
    void startTimerSound(s32 step);

    Tamakoro* mHost;
    TalkMessageCtrl* mTalkCtrl;
    TalkMessageCtrl* mTalkCtrlAutomatic;
    Vec3f mPadAccel;
    bool mIsTrampled;
    bool mCanTalk;
};
