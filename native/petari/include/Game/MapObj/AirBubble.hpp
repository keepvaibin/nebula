#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AirBubble : public LiveActor {
public:
    AirBubble(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void appearMove(const Vec3f&, s32);
    void exeWait();
    void exeMove();
    void exeBreak();
    void exeKillWait();
    bool canSpinGet() const;

    Vec3f mBasePos;
    Vec3f mDriftDir;
    f32 mBobOffset;
    f32 mRailMoveSpeed;
    s32 mLife;
    bool mIsFollowRail;
    bool mIsItemMoving;
};
