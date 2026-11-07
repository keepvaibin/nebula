#pragma once

#include "Game/Particle/ParticleData.hpp"

struct EmitterWork;
class ParticleCore;

class ChildShape {
public:
    explicit ChildShape(const ChildShapeData* pData) : mpData(pData) {
    }

    f32 getPosRndm() const {
        return mpData->mPosRndm;
    }
    f32 getBaseVel() const {
        return mpData->mBaseVel;
    }
    f32 getBaseVelRndm() const {
        return mpData->mBaseVelRndm;
    }
    f32 getVelInhRate() const {
        return mpData->mVelInfRate;
    }
    f32 getGravity() const {
        return mpData->mGravity;
    }
    f32 getScaleX() const {
        return mpData->mScaleX;
    }
    f32 getScaleY() const {
        return mpData->mScaleY;
    }
    f32 getScaleInhRate() const {
        return mpData->mInheritScale;
    }
    f32 getAlphaInhRate() const {
        return mpData->mInheritAlpha;
    }
    f32 getColorInhRate() const {
        return mpData->mInheritRGB;
    }
    void getPrmClr(NebulaColor8* dst) const {
        *dst = mpData->mPrmClr;
    }
    u8 getPrmAlpha() const {
        return mpData->mPrmClr.a;
    }
    void getEnvClr(NebulaColor8* dst) const {
        *dst = mpData->mEnvClr;
    }
    f32 getTiming() const {
        return mpData->mTiming;
    }
    s16 getLife() const {
        return mpData->mLife;
    }
    s16 getRate() const {
        return mpData->mRate;
    }
    u8 getStep() const {
        return mpData->mStep;
    }
    u8 getTexIdx() const {
        return mpData->mTexIdx;
    }
    s16 getRotInitSpeed() const {
        return mpData->mRotSpeed;
    }
    u32 getType() const {
        return mpData->mType;
    }
    u32 getDirType() const {
        return mpData->mDirType;
    }
    u32 getRotType() const {
        return mpData->mRotType;
    }
    u32 getBasePlaneType() const {
        return mpData->mPlaneType;
    }

    bool isScaleInherited() const {
        return mpData->mScaleInherited != 0;
    }
    bool isAlphaInherited() const {
        return mpData->mAlphaInherited != 0;
    }
    bool isColorInherited() const {
        return mpData->mColorInherited != 0;
    }
    bool isFieldAffected() const {
        return mpData->mFieldAffected != 0;
    }
    bool isScaleOutOn() const {
        return mpData->mScaleOutOn != 0;
    }
    bool isAlphaOutOn() const {
        return mpData->mAlphaOutOn != 0;
    }
    bool isRotateOn() const {
        return mpData->mRotateOn != 0;
    }

public:
    const ChildShapeData* mpData;
};

void ParticleRegistChildPrmEnv(EmitterWork*);

void ParticleCalcChildAlphaOut(EmitterWork*, ParticleCore*);
void ParticleCalcChildScaleOut(EmitterWork*, ParticleCore*);
