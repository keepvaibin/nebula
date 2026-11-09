#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class Koopa;
class ModelObj;
class PartsModel;

class KoopaShockWave : public LiveActor {
public:
    KoopaShockWave(Koopa*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void makeActorDead() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void exeWaveAttack();

private:
    Koopa* mKoopa;
    f32 mAngle;
    Pos3f mKoopaMtx;
    Vec3f mUp;
    Vec3f mFront;
    Pos3f mBaseMtx;
    Pos3f mMtx;
    PartsModel* mPartsModel;
    ModelObj* mShadow;
};
