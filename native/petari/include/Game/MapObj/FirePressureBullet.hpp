#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class FirePressureBullet : public LiveActor {
public:
    FirePressureBullet(const char*);

    virtual ~FirePressureBullet();
    virtual void init(const TableIter&);
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void shotFireBullet(LiveActor*, const Pos3f&, const f32&, bool, bool);
    void exeFly();
    bool isCrash() const;

    Vec3f mFront;
    LiveActor* mFirePressure;
    f32 mSpeed;
    bool mIsHoming;
    bool mCanCrash;
};
