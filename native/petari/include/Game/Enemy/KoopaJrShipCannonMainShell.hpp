#pragma once

#include "Game/Enemy/CannonShellBase.hpp"

class KoopaJrShipCannonMainShell : public CannonShellBase {
public:
    KoopaJrShipCannonMainShell(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    virtual void launch(const Vec3f&, const Vec3f&) override;
    virtual f32 getBaseScale() const override;

    void explosion();

    void exeFly();

    Quat4f mRotateQuat;
};

namespace CannonShellUtil {
    void registerKoopaJrShipCannonMainShell(CannonShellHolder* pHolder, int shellNum);
};  // namespace CannonShellUtil
