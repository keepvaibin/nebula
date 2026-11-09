#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/Swinger.hpp"

class MagicBell : public LiveActor {
public:
    MagicBell(const char*);

    virtual void init(const TableIter&) override;
    virtual MtxPtr getBaseMtx() const override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeRing();
    bool tryRing();
    void startRing(const Vec3f& rHitDir, const Vec3f& rHitPos);
    void updateJointMtx();

    Swinger* mBellSwinger;
    MtxPtr mSurface2Mtx;
    Swinger* mBellRodSwinger;
    MtxPtr mSurface1Mtx;
    Vec3f mHitMarkPosition;
};
