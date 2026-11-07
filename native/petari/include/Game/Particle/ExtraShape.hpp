#pragma once

#include "Game/Particle/ParticleData.hpp"

struct EmitterWork;
class ParticleCore;

class ExtraShape {
public:
    explicit ExtraShape(const ExtraShapeData* pData) : mpData(pData) {
    }

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
        return mpData->mScaleIncRateX;
    }
    f32 getScaleDecRateX() const {
        return mpData->mScaleDecRateX;
    }
    f32 getScaleIncRateY() const {
        return mpData->mScaleIncRateY;
    }
    f32 getScaleDecRateY() const {
        return mpData->mScaleDecRateY;
    }
    f32 getAlphaIncRate() const {
        return mpData->mAlphaIncRate;
    }
    f32 getAlphaDecRate() const {
        return mpData->mAlphaDecRate;
    }

    bool isEnableScaleAnm() const {
        return mpData->mScaleAnm != 0;
    }
    bool isScaleXYDiff() const {
        return mpData->mScaleXYDiff != 0;
    }
    u32 getScaleAnmTypeX() const {
        return mpData->mScaleAnmTypeX;
    }
    u32 getScaleAnmTypeY() const {
        return mpData->mScaleAnmTypeY;
    }
    u32 getScaleCenterX() const {
        return mpData->mScaleCenterX;
    }
    u32 getScaleCenterY() const {
        return mpData->mScaleCenterY;
    }
    bool isEnableAlphaAnm() const {
        return mpData->mAlphaAnm != 0;
    }
    bool isEnableAlphaFlick() const {
        return mpData->mAlphaFlick != 0;
    }
    bool isEnableRotateAnm() const {
        return mpData->mRotateAnm != 0;
    }

private:
    const ExtraShapeData* mpData;
};

void JPACalcAlphaFlickAnm(EmitterWork*, ParticleCore*);
void JPACalcAlphaAnm(EmitterWork*, ParticleCore*);
void JPACalcScaleX(EmitterWork*, ParticleCore*);
void JPACalcScaleY(EmitterWork*, ParticleCore*);
void JPACalcScaleCopy(EmitterWork*, ParticleCore*);
void JPACalcScaleAnmNormal(EmitterWork*, ParticleCore*);
void JPACalcScaleAnmRepeatX(EmitterWork*, ParticleCore*);
void JPACalcScaleAnmReverseX(EmitterWork*, ParticleCore*);
void JPACalcScaleAnmRepeatY(EmitterWork*, ParticleCore*);
void JPACalcScaleAnmReverseY(EmitterWork*, ParticleCore*);
