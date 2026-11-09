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
    // RunawayRabbit's arg 1 in the stage data.
    enum HideLocation {
        /* -0x1 */ HideLocation_None = -1,
        HideLocation_Hole,
        HideLocation_Leaves
    };

    RunawayRabbit(const char* pName, RunawayRabbitCollect* pCollect);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
    Quat4f mPoseQuat;
    Vec3f mFrontVec;
    Quat4f mPlayerPoseQuat;
    Vec3f mPlayerBindPos;
    s32 mGroupId;
    s32 mRunawayLevel;
    s32 mHideLocation;
    s32 mPressureTimer;
    s32 mNotCaughtableTimer;
    bool mIsActive;
    bool mIsLinked;
    f32 mNearSoundRadius;
};
