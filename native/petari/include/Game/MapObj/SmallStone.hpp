#pragma once

#include "Game/LiveActor/ModelObj.hpp"

class SmallStoneMember;

class SmallStone : public LiveActor {
public:
    SmallStone(const char* pName);

    virtual ~SmallStone() override;
    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual void startClipped() override;
    virtual void endClipped() override;

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

    const Vec3f& getRotation() const {
        return mRotation;
    }
    bool tryPlaceOnGround(const Vec3f& rStart, const Vec3f& rRay);
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
