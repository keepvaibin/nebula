#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>


class ElectricPressureBullet : public LiveActor {
public:
    ElectricPressureBullet(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void shotElectricBullet(LiveActor*, const Pos3f&, const f32&);
    void exeFly();

    Vec3f mFront;
    f32 mSpeed;
};
