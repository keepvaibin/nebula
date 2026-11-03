#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry.hpp>

class PartsModel;

class LavaProminenceTriple : public LiveActor {
public:
    LavaProminenceTriple(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void updateEffectClipping();
    void updateEffectClippingIndividual(bool*, const char*, const char*);

    void calcAngle(f32*);

    PartsModel* mBloomModel;
    TPos3f mBloomMtx;
    TQuat4f mBaseRot;
    TQuat4f mCurrRot;
    TVec3f mSide;
    TVec3f mFront;
    f32 mRotateVel;
    bool mIsAppearDrop1;
    bool mIsAppearDrop2;
    bool mIsAppearDrop3;
    bool mIsEnvironment;
    s32 mDefaultAppearTime;
    s32 mAppearTime;
};
