#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"

class HitSensor;
class Koopa;

namespace MR {
    struct ActorMoveParam;
};  // namespace MR

class KoopaStateDamageEscape : public ActorStateBase< Koopa > {
public:
    KoopaStateDamageEscape(Koopa* pKoopa);

    virtual void init();
    virtual void appear();
    virtual void kill();

    void startDamageReverse();
    bool isDownEnd() const;
    bool tryDamage(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    bool attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void exeEscapeStart();
    void exeEscapeStartLoop();
    void exeEscapeStartLand();
    void exeEscapeRun();
    void exeEscapeFind();
    void exeEscapeEnd();
    void exeDamageTailRunStart();
    void exeDamageTailRun();
    void exeDamageTailRunEnd();
    void exeDown();
    void exeDownLand();
    void exeDownEnd();

private:
    s32 mEscapeTime;
    s32 mMaxEscapeTime;
    bool mIsTurnClockwise;
    bool mIsLastHit;
    MR::ActorMoveParam* mEscapeRunParam;
    MR::ActorMoveParam* mDamageTailRunParam;
    u32 mMaxRunFrames;
    f32 mRotateVelocity;
    f32 mJumpAwayVelocity;
};
