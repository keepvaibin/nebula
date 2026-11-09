#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class FirePressureBullet : public LiveActor {
public:
    FirePressureBullet(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void shotFireBullet(LiveActor* pFirePressure, const Pos3f& rMtx, const f32& rSpeed, bool isHoming, bool canCrash);
    void exeFly();
    bool isCrash() const;

    Vec3f mFront;
    f32 mSpeed;
    bool mIsHoming;
    bool mCanCrash;
};
