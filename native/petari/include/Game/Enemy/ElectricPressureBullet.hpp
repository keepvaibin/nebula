#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>


class ElectricPressureBullet : public LiveActor {
public:
    ElectricPressureBullet(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void kill();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void shotElectricBullet(LiveActor*, const Pos3f&, const f32&);
    void exeFly();

    Vec3f _8C;
    LiveActor* _98;
    f32 _9C;
};
