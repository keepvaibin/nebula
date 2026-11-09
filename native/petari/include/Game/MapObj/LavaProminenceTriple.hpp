#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class PartsModel;

class LavaProminenceTriple : public LiveActor {
public:
    LavaProminenceTriple(const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void updateEffectClipping();
    void updateEffectClippingIndividual(bool*, const char*, const char*);

    PartsModel* mBloomModel;
    Pos3f mBloomMtx;
    Quat4f mBaseRot;
    Quat4f mCurrRot;
    Vec3f mSide;
    Vec3f mFront;
    f32 mRotateVel;
    bool mIsAppearDrop1;
    bool mIsAppearDrop2;
    bool mIsAppearDrop3;
    bool mIsEnvironment;
    s32 mDefaultAppearTime;
    s32 mAppearTime;
};
