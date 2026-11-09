#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class TalkMessageCtrl;
class Tamakoro;

class TamakoroTutorial : public LiveActor {
public:
    TamakoroTutorial(const char* pName);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

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
