#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class Koopa;
class ModelObj;
class PartsModel;

class KoopaShockWave : public LiveActor {
public:
    KoopaShockWave(Koopa*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void makeActorDead();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);

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
