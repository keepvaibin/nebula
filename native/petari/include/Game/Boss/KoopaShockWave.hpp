#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class Koopa;
class ModelObj;
class PartsModel;

class KoopaShockWave : public LiveActor {
public:
    KoopaShockWave(Koopa*);

    virtual ~KoopaShockWave();
    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void makeActorDead();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void exeWaveAttack();

private:
    Koopa* mKoopa;
    f32 mAngle;
    TPos3f _94;
    TVec3f mUp;
    TVec3f mFront;
    TPos3f mBaseMtx;
    TPos3f mMtx;
    PartsModel* mPartsModel;
    ModelObj* mShadow;
};
