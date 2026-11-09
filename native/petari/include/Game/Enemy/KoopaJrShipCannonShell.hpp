#pragma once

#include "Game/Enemy/CannonShellBase.hpp"

class KoopaJrShipCannonShell : public CannonShellBase {
public:
    KoopaJrShipCannonShell(const char*);

    virtual ~KoopaJrShipCannonShell() override {
    }

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    virtual void launch(const Vec3f&, const Vec3f&) override;

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
