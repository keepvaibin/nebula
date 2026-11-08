#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>


class ElectricPressureBullet : public LiveActor {
public:
    ElectricPressureBullet(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void shotElectricBullet(LiveActor*, const Pos3f&, const f32&);
    void exeFly();

    Vec3f mFront;
    f32 mSpeed;
};
