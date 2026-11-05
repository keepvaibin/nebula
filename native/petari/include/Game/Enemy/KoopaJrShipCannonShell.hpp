#pragma once

#include "Game/Enemy/CannonShellBase.hpp"

class KoopaJrShipCannonShell : public CannonShellBase {
public:
    KoopaJrShipCannonShell(const char*);

    virtual ~KoopaJrShipCannonShell() {
    }

    virtual void init(const JMapInfoIter& rIter);
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    virtual void launch(const Vec3f&, const Vec3f&);

    void initStarPointer();
    bool tryFreeze();
    bool isStateEnableExplosion() const;
    void explosion();
    void misfire();

    void exeWait();
    void exeFly();
    void exeDown();
    void exeFreeze();

    Quat4f _8C;
    Vec3f _9C;
    Vec3f _A8;
    s32 _B4;  // Freeze timing?
    bool _B8;
};

namespace CannonShellUtil {
    void registerKoopaJrShipCannonShell(CannonShellHolder* pHolder, int shellNum);
};  // namespace CannonShellUtil
