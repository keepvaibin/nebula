#pragma once

#include "Game/Enemy/KameckBeam.hpp"

class ActiveActorList;
class AnimScaleController;
class SmallKameckBeamEventListener;
class WalkerStateBindStarPointer;

/// @brief Magikoopa
class Kameck : public LiveActor {
public:
    Kameck(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void makeActorDead();
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void startClipped();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initBeam();
    void initJMapParam(const JMapInfoIter& rIter);
    void initDemo(const JMapInfoIter& rIter);
    void exeWait();
    void exeAppear();
    void exeHit();
    void exeBindStarPointer();
    void endBindStarPointer();
    void exeDown();
    void exeOpeningDemo();
    void exeDemoAppear();
    void endDemoAppear();
    void exeNonActive();
    void exeHide();
    void exeAttackWait();
    void exeAttack();
    void exeGuard();
    void exeMoveHide();
    void exeMove();
    void exePressDown();
    void startDemoAppear();
    void killForce();
    void resetBeam();
    bool isEnableAttack() const;
    bool isEnableDown() const;
    bool isEnableGurad() const;
    bool tryNonActive();
    bool requestAttack(HitSensor*, HitSensor*);
    bool requestGuard(HitSensor*, HitSensor*);
    bool requestDown(HitSensor*, HitSensor*);
    bool requestPressDown();
    bool tryOpeningDemo();
    bool tryOpeningDemoEnd();
    bool canNonActive() const;
    void setNonActive();
    bool tryActive();
    bool tryAppearEnd();
    bool tryAttackWait();
    bool tryAttack();
    bool tryAttackEnd();
    bool tryHitEnd();
    bool tryMove();
    bool tryMoveEnd();
    bool tryPointBind();
    void hitBeam(s32);
    void setBeamType(s32);
    void makeActorDeadForce();

    KameckBeam* mBeam;
    ActiveActorList* mActiveActorList;
    SmallKameckBeamEventListener* mBeamEventListener;
    AnimScaleController* mAnimScaleController;
    WalkerStateBindStarPointer* mStateBindStarPointer;
    TQuat4f mRotateQuat;
    TVec3f mFrontVec;
    s32 mBeamType;
    s32 mMoveStep;
    f32 mRailCoord;
    f32 mRailNextPointCoord;
    f32 mActiveDistance;
};

class SmallKameckBeamEventListener : public KameckBeamEventListener {
public:
    SmallKameckBeamEventListener(Kameck* pHost) : mHost(pHost) {
    }

    virtual void hitBeam(s32 beamType);

    Kameck* mHost;
};

namespace MR {
    NameObj* createFireBallBeamKameck(const char*);
    NameObj* createTurtleBeamKameck(const char*);
};  // namespace MR
