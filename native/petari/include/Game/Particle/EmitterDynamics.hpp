#pragma once

#include "Game/Particle/ParticleData.hpp"

struct EmitterWork;

class EmitterDynamics {
public:
    explicit EmitterDynamics(const EmitterDynamicsData* pData);
    void create(EmitterWork*);

    // A new particle's place and velocity in the emitter's volume.
    void calc(EmitterWork* work);

    s16 getStartFrame() const {
        return mpData->mStartFrame;
    }
    u32 getFlag() const {
        return mpData->mFlags;
    }
    u32 getVolumeType() const {
        return mpData->mVolumeType;
    }
    u16 getDivNumber() const {
        return mpData->mDivNumber;
    }
    f32 getRateRndm() const {
        return mpData->mRateRndm;
    }
    s16 getMaxFrame() {
        return mpData->mMaxFrame;
    }
    s16 getLifetime() {
        return mpData->mLifeTime;
    }
    u16 getVolumeSize() {
        return mpData->mVolumeSize;
    }
    f32 getRate() {
        return mpData->mRate;
    }
    u8 getRateStep() {
        return mpData->mRateStep;
    }
    f32 getVolumeSweep() {
        return mpData->mVolumeSweep;
    }
    f32 getVolumeMinRad() {
        return mpData->mVolumeMinRad;
    }
    f32 getInitVelOmni() {
        return mpData->mInitialVelOmni;
    }
    f32 getInitVelAxis() {
        return mpData->mInitialVelAxis;
    }
    f32 getInitVelDir() {
        return mpData->mInitialVelDir;
    }
    f32 getInitVelDirSp() {
        return mpData->mSpread;
    }
    f32 getInitVelRndm() {
        return mpData->mInitialVelRndm;
    }
    f32 getInitVelRatio() const {
        return mpData->mInitialVelRatio;
    }
    f32 getAirRes() {
        return mpData->mAirResist;
    }
    f32 getLifetimeRndm() const {
        return mpData->mLifeTimeRndm;
    }
    f32 getMomentRndm() const {
        return mpData->mMoment;
    }

public:
    const EmitterDynamicsData* mpData;
};
