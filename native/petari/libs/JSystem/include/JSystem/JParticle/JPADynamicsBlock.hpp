#pragma once

#include "JSystem/JGeometry/TVec.hpp"

struct JPAEmitterWorkData;

struct JPADynamicsBlockData {
    // Common header.
    u8 mMagic[4];
    u32 mSize;

    u32 mFlags;
    u32 mResUserWork;
    JGeometry::TVec3< f32 > mEmitterScl;
    JGeometry::TVec3< f32 > mEmitterTrs;
    JGeometry::TVec3< f32 > mEmitterDir;
    f32 mInitialVelOmni;
    f32 mInitialVelAxis;
    f32 mInitialVelRndm;
    f32 mInitialVelDir;
    f32 mSpread;
    f32 mInitialVelRatio;
    f32 mRate;
    f32 mRateRndm;
    f32 mLifeTimeRndm;
    f32 mVolumeSweep;
    f32 mVolumeMinRad;
    f32 mAirResist;
    f32 mMoment;
    JGeometry::TVec3< s16 > mEmitterRot;
    s16 mMaxFrame;
    s16 mStartFrame;
    s16 mLifeTime;
    u16 mVolumeSize;
    u16 mDivNumber;
    u8 mRateStep;
};

typedef void (*JPADynamicsCalcVolumeFunc)(JPAEmitterWorkData*);

enum {
    JPADynFlag_FixedDensity = 0x01,
    JPADynFlag_FixedInterval = 0x02,
    JPADynFlag_InheritScale = 0x04,
    JPADynFlag_FollowEmtr = 0x08,
    JPADynFlag_FollowEmtrChld = 0x10,
};

class JPADynamicsBlock {
public:
    JPADynamicsBlock(u8 const*);
    void init();
    void create(JPAEmitterWorkData*);

    void calc(JPAEmitterWorkData* work) {
        mpCalcVolumeFunc(work);
    }

    s16 getStartFrame() const {
        return mpData->mStartFrame;
    }
    u32 getResUserWork() const {
        return mpData->mResUserWork;
    }
    u32 getFlag() const {
        return mpData->mFlags;
    }
    u32 getVolumeType() const {
        return (mpData->mFlags >> 8) & 0x07;
    }
    u16 getDivNumber() const {
        return mpData->mDivNumber;
    }
    f32 getRateRndm() const {
        return mpData->mRateRndm;
    }
    void getEmitterScl(JGeometry::TVec3< f32 >* vec) const {
        vec->set(mpData->mEmitterScl.x, mpData->mEmitterScl.y, mpData->mEmitterScl.z);
    }
    void getEmitterTrs(JGeometry::TVec3< f32 >* vec) const {
        vec->set(mpData->mEmitterTrs.x, mpData->mEmitterTrs.y, mpData->mEmitterTrs.z);
    }
    void getEmitterDir(JGeometry::TVec3< f32 >* vec) const {
        vec->set(mpData->mEmitterDir.x, mpData->mEmitterDir.y, mpData->mEmitterDir.z);
    }
    void getEmitterRot(JGeometry::TVec3< s16 >* vec) const {
        vec->set(mpData->mEmitterRot.x, mpData->mEmitterRot.y, mpData->mEmitterRot.z);
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
    const JPADynamicsBlockData* mpData;
    JPADynamicsCalcVolumeFunc mpCalcVolumeFunc;
};
