#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class SmallStoneMember;

class SmallStone : public LiveActor {
public:
    SmallStone(const char* pName);

    virtual ~SmallStone();
    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual void startClipped();
    virtual void endClipped();

    s32 getStoneType() const {
        return mStoneType;
    }
    bool isAllMemberBreak();
    bool tryBreak();
    void initMember(const char* pModelName, bool useGravity);

    SmallStoneMember** mMembers;
    s32 mMemberCount;
    f32 mPlacementOffset;
    s32 mStoneType;
    bool mUseGravity;
    bool mIsStarPiece;
};

class SmallStoneMember : public ModelObj {
public:
    SmallStoneMember(const char* pModelName);

    virtual ~SmallStoneMember();

    const TVec3f& getRotation() const {
        return mRotation;
    }
    bool tryPlaceOnGround(const TVec3f& rStart, const TVec3f& rRay);
    bool tryShake();
    bool tryPush(HitSensor* pSender, HitSensor* pReceiver);
    void movementByHost(SmallStone* pHost);
    void animControl();
    void doBreak(SmallStone* pHost);

    f32 mAnimRate;
    u8 mShakeRequested;
    bool mAnimStopped;
    s32 mBreakTimer;
    const char* mBreakEffectName;
    bool mUseGravity;
};
