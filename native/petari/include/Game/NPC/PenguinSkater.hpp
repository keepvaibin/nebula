#pragma once

#include "Game/Util/Array.hpp"
#include "Game/NPC/NPCActor.hpp"

class ActorCameraInfo;
class NPCSupportRail;

class PenguinSkater : public NPCActor {
public:
    PenguinSkater(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    f32 calcLead() const;
    void moveRail(f32, f32);
    void stopRail(f32);
    bool inProvokeRangeIn(f32) const;
    bool inProvokeRangeOut(f32) const;
    bool inSwitchRange(f32) const;
    bool trySwitchRail();
    void endSwitchRail();
    void setCaughtStartMarioPose();
    void blendBaseMatrixToMario(f32) const;

    void exeReaction();
    void exeTalk();
    void exeWait();
    void exeDemo();
    void exeAway();
    void exeTurn();
    void exeProvoke();
    void exeCaught();
    void exeFadeOut();
    void exeFadeIn();
    void exeGiveUp();
    void exeSitDown();
    void exeSwitch();

    bool isAttackable() const;

    ActorCameraInfo* mCameraInfo;
    NPCSupportRail* mCurrentRail;
    NPCSupportRail* mSwitchRail;
    MR::GrowTable< NPCSupportRail*, 16 > mSwitchRails;
    NPCSupportRail* mRail;
    s32 mNumRails;
    s32 mCrossPoint;
    Quat4f mMarioQuat;
    Vec3f mMarioPos;
};
