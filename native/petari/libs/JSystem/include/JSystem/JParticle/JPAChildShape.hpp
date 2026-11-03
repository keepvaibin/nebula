#pragma once

#include <NebulaGpu.h>

struct JPAEmitterWorkData;
class JPABaseParticle;

struct JPAChildShapeData {
    // Common header.
    u8 mMagic[4];
    u32 mSize;

    u32 mFlags;
    f32 mPosRndm;
    f32 mBaseVel;
    f32 mBaseVelRndm;
    f32 mVelInfRate;
    f32 mGravity;
    f32 mScaleX;
    f32 mScaleY;
    f32 mInheritScale;
    f32 mInheritAlpha;
    f32 mInheritRGB;
    NebulaColor8 mPrmClr;
    NebulaColor8 mEnvClr;
    f32 mTiming;
    s16 mLife;
    s16 mRate;
    u8 mStep;
    u8 mTexIdx;
    s16 mRotSpeed;
};

class JPAChildShape {
public:
    JPAChildShape(u8 const*);

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
        return mpData->mFlags & 0xF;
    }
    u32 getDirType() const {
        return (mpData->mFlags >> 4) & 7;
    }
    u32 getRotType() const {
        return (mpData->mFlags >> 7) & 7;
    }
    u32 getBasePlaneType() const {
        return (mpData->mFlags >> 10) & 1;
    }

    BOOL isScaleInherited() const {
        return mpData->mFlags & 0x10000;
    }
    BOOL isAlphaInherited() const {
        return mpData->mFlags & 0x20000;
    }
    BOOL isColorInherited() const {
        return mpData->mFlags & 0x40000;
    }
    BOOL isClipOn() const {
        return mpData->mFlags & 0x100000;
    }
    BOOL isFieldAffected() const {
        return mpData->mFlags & 0x200000;
    }
    BOOL isScaleOutOn() const {
        return mpData->mFlags & 0x400000;
    }
    BOOL isAlphaOutOn() const {
        return mpData->mFlags & 0x800000;
    }
    BOOL isRotateOn() const {
        return mpData->mFlags & 0x1000000;
    }

public:
    const JPAChildShapeData* mpData;
};

void JPARegistChildPrmEnv(JPAEmitterWorkData*);

void JPACalcChildAlphaOut(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcChildScaleOut(JPAEmitterWorkData*, JPABaseParticle*);
