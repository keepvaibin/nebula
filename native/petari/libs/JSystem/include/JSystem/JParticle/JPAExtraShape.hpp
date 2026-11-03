#pragma once


struct JPAEmitterWorkData;
class JPABaseParticle;

struct JPAExtraShapeData {
    // Common header.
    u8 mMagic[4];
    u32 mSize;

    u32 mFlags;
    f32 mScaleInTiming;
    f32 mScaleOutTiming;
    f32 mScaleInValueX;
    f32 mScaleOutValueX;
    f32 mScaleInValueY;
    f32 mScaleOutValueY;
    f32 mScaleOutRandom;
    s16 mScaleAnmCycleX;
    s16 mScaleAnmCycleY;
    f32 mAlphaInTiming;
    f32 mAlphaOutTiming;
    f32 mAlphaInValue;
    f32 mAlphaBaseValue;
    f32 mAlphaOutValue;
    f32 mAlphaWaveFrequency;
    f32 mAlphaWaveRandom;
    f32 mAlphaWaveAmplitude;
    f32 mRotateAngle;
    f32 mRotateAngleRandom;
    f32 mRotateSpeed;
    f32 mRotateSpeedRandom;
    f32 mRotateDirection;
};

class JPAExtraShape {
public:
    JPAExtraShape(u8 const*);
    void init();

    f32 getScaleInTiming() const {
        return mpData->mScaleInTiming;
    }
    f32 getScaleOutTiming() const {
        return mpData->mScaleOutTiming;
    }
    f32 getScaleInValueX() const {
        return mpData->mScaleInValueX;
    }
    f32 getScaleInValueY() const {
        return mpData->mScaleInValueY;
    }
    f32 getScaleOutValueX() const {
        return mpData->mScaleOutValueX;
    }
    f32 getScaleOutValueY() const {
        return mpData->mScaleOutValueY;
    }
    f32 getScaleRndm() const {
        return mpData->mScaleOutRandom;
    }
    s16 getScaleAnmCycleX() const {
        return mpData->mScaleAnmCycleX;
    }
    s16 getScaleAnmCycleY() const {
        return mpData->mScaleAnmCycleY;
    }
    f32 getAlphaInTiming() const {
        return mpData->mAlphaInTiming;
    }
    f32 getAlphaOutTiming() const {
        return mpData->mAlphaOutTiming;
    }
    f32 getAlphaInValue() const {
        return mpData->mAlphaInValue;
    }
    f32 getAlphaOutValue() const {
        return mpData->mAlphaOutValue;
    }
    f32 getAlphaBaseValue() const {
        return mpData->mAlphaBaseValue;
    }
    f32 getAlphaFreq() const {
        return mpData->mAlphaWaveFrequency;
    }
    f32 getAlphaFreqRndm() const {
        return mpData->mAlphaWaveRandom;
    }
    f32 getAlphaAmp() const {
        return mpData->mAlphaWaveAmplitude;
    }
    f32 getRotateInitAngle() const {
        return mpData->mRotateAngle;
    }
    f32 getRotateRndmAngle() const {
        return mpData->mRotateAngleRandom;
    }
    f32 getRotateInitSpeed() const {
        return mpData->mRotateSpeed;
    }
    f32 getRotateRndmSpeed() const {
        return mpData->mRotateSpeedRandom;
    }
    f32 getRotateDirection() const {
        return mpData->mRotateDirection;
    }
    f32 getScaleIncRateX() const {
        return mScaleIncRateX;
    }
    f32 getScaleDecRateX() const {
        return mScaleDecRateX;
    }
    f32 getScaleIncRateY() const {
        return mScaleIncRateY;
    }
    f32 getScaleDecRateY() const {
        return mScaleDecRateY;
    }
    f32 getAlphaIncRate() const {
        return mAlphaIncRate;
    }
    f32 getAlphaDecRate() const {
        return mAlphaDecRate;
    }

    BOOL isEnableScaleAnm() const {
        return mpData->mFlags & 1;
    }
    BOOL isScaleXYDiff() const {
        return mpData->mFlags & 2;
    }
    u32 getScaleAnmTypeX() const {
        return (mpData->mFlags >> 8) & 3;
    }
    u32 getScaleAnmTypeY() const {
        return (mpData->mFlags >> 10) & 3;
    }
    u32 getScaleCenterX() const {
        return (mpData->mFlags >> 12) & 3;
    }
    u32 getScaleCenterY() const {
        return (mpData->mFlags >> 14) & 3;
    }
    BOOL isEnableAlphaAnm() const {
        return mpData->mFlags & 0x10000;
    }
    BOOL isEnableAlphaFlick() const {
        return mpData->mFlags & 0x20000;
    }
    BOOL isEnableRotateAnm() const {
        return mpData->mFlags & 0x1000000;
    }

private:
    const JPAExtraShapeData* mpData;
    f32 mAlphaIncRate;
    f32 mAlphaDecRate;
    f32 mScaleIncRateX;
    f32 mScaleIncRateY;
    f32 mScaleDecRateX;
    f32 mScaleDecRateY;
};

void JPACalcAlphaFlickAnm(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcAlphaAnm(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcScaleX(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcScaleY(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcScaleCopy(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcScaleAnmNormal(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcScaleAnmRepeatX(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcScaleAnmReverseX(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcScaleAnmRepeatY(JPAEmitterWorkData*, JPABaseParticle*);
void JPACalcScaleAnmReverseY(JPAEmitterWorkData*, JPABaseParticle*);
