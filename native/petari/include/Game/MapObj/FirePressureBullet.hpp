#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class FirePressureBullet : public LiveActor {
public:
    FirePressureBullet(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void shotFireBullet(LiveActor* pFirePressure, const Pos3f& rMtx, const f32& rSpeed, bool isHoming, bool canCrash);
    void exeFly();
    bool isCrash() const;

    Vec3f mFront;
    f32 mSpeed;
    bool mIsHoming;
    bool mCanCrash;
};
