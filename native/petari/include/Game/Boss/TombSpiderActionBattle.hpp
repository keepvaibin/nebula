#pragma once

#include "Game/Boss/TombSpiderActionBase.hpp"

class TombSpider;
class HitSensor;

struct TombSpiderBattleParam {
    s32 mStepWaitMin;
    s32 mStepWaitMax;
    s32 mStepAttackLoopMin;
    s32 mStepAttackLoopMax;
    f32 mAttackRotateAccel;
    f32 mAttackRotateSpeedMax;
    s32 mStepChanceWait;
    /// @brief Whether the gland attack sensors hurt too, not only the body's.
    bool mIsGlandAttack;
};

/// @brief One round of the Tomb Spider fight; the rounds differ only in their parameters.
class TombSpiderActionBattle : public TombSpiderActionBase {
public:
    TombSpiderActionBattle(TombSpider* pParent, const char* pName, const TombSpiderBattleParam& rParam);

    virtual void init();
    virtual void appear();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeAttackStart();
    void exeAttackLoop();
    void exeAttackEnd();
    void exeDamageEye();
    void exeDamageHip();
    void exeDamageGland();
    void exeSwoon();
    void exeChanceStart();
    void exeChanceWait();
    void exeChanceDamage();
    void exeChanceDamageLast();
    void exeChanceEnd();
    void exeWaitDemo();

private:
    const TombSpiderBattleParam mParam;
};
