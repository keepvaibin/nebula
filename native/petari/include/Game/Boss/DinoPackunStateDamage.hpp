#pragma once

#include "Game/LiveActor/ActorStateBase.hpp"
#include <nebula_geometry.hpp>

class DinoPackun;
class HitSensor;

class DinoPackunStateDamage : public ActorStateBase< DinoPackun > {
public:
    enum DamageType {
        DamageType_Egg,
        DamageType_Normal,
        DamageType_Last,
    };

    // Where the pulled tail was, seen from the head.
    enum PunchedDir {
        PunchedDir_None,
        PunchedDir_Left,
        PunchedDir_Right,
        PunchedDir_Top,
    };

    DinoPackunStateDamage(DinoPackun* pHost);

    virtual void appear();

    bool isDamageMessage(u32 msg) const;
    bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    void setDamageEgg();
    void setDamageNormal();
    void setDamageLast();
    const Vec3f& getTailOutPosition() const;
    void startBckByPunchedDir(const char* const* pBckNames);
    void exePunched();
    void exePulled();
    void exeTryPulledDemo();
    void exeEggBroken();
    void exeDamage();
    void exeLastDamage();
    void updateDamage();

    PunchedDir mPunchedDir;
    DamageType mDamageType;
};
