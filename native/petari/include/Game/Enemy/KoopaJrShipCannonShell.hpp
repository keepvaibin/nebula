#pragma once

#include "Game/Enemy/CannonShellBase.hpp"

class KoopaJrShipCannonShell : public CannonShellBase {
public:
    KoopaJrShipCannonShell(const char*);

    virtual ~KoopaJrShipCannonShell() {
    }

    virtual void init(const TableIter& rIter);
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    virtual void launch(const Vec3f&, const Vec3f&);

    bool tryFreeze();
    bool isStateEnableExplosion() const;
    void explosion();
    void misfire();

    void exeFly();
    void exeDown();
    void exeFreeze();

    Quat4f mPoseQuat;
    // Where and how fast the shell was when the pointer froze it; restored when the freeze ends.
    Vec3f mFreezePos;
    Vec3f mFreezeVelocity;
    // Frozen frames since the last tryFreeze, across re-freezes.
    s32 mFreezeStep;
};

namespace CannonShellUtil {
    void registerKoopaJrShipCannonShell(CannonShellHolder* pHolder, int shellNum);
}  // namespace CannonShellUtil
