#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FootPrint;
class RunawayRabbitCollect;
class SpotMarkLight;
class TalkMessageCtrl;
class WalkerStateBlowDamage;
class WalkerStateRunaway;

class RunawayRabbit : public LiveActor {
public:
    enum HideLocation {
        /* -0x1 */ HideLocation_None = -1,
        HideLocation_Hole,
        HideLocation_Leaves
    };

    RunawayRabbit(const char* pName, RunawayRabbitCollect* pCollect);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initSensor();
    void initShadow();
    void initFootPrint();
    void updatePose();
    void updateBindActorMatrix();
    void activate();
    void startRunnaway();
    void incrementRunawayLevel();
    void setLastMessage();
    void setMessage();
    void setNotCaughtable();
    void startJumpSound();
    void setMsgCtrl(TalkMessageCtrl* pMsgCtrl);
    bool isCaught() const;
    bool isCaughtable() const;
    bool isRunnaway() const;
    bool isChasing() const;
    bool isEnableBlow() const;
    bool isValidFollow(s32 id) const;
    s32 getGroupId() const {
        return mGroupId;
    }

    void exeNoActive();
    void exeHide();
    void exeAppear();
    void exeRunaway();
    void exeTryCaughtDemo();
    void exeCaught();
    void exeCaughtTalk();
    void exeCaughtEnd();
    void exeStop();
    void exeBlowDamage();

    WalkerStateRunaway* mStateRunaway;
    WalkerStateBlowDamage* mStateBlowDamage;
    RunawayRabbitCollect* mCollect;
    FootPrint* mFootPrint;
    SpotMarkLight* mSpotMarkLight;
    TalkMessageCtrl* mMsgCtrl;
    TQuat4f mPoseQuat;
    TVec3f mFrontVec;
    TQuat4f mPlayerPoseQuat;
    TVec3f mPlayerBindPos;
    u8 _DC[4];
    s32 mGroupId;
    s32 mRunawayLevel;
    s32 mHideLocation;
    s32 mPressureTimer;
    s32 mNotCaughtableTimer;
    bool mIsActive;
    bool mIsLinked;
    f32 mNearSoundRadius;
};
