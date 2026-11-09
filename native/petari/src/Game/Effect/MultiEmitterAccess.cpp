#include "Game/Effect/MultiEmitter.hpp"
#include "Game/Effect/MultiEmitterCallBack.hpp"
#include "Game/Effect/SingleEmitter.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Particle/EmitterCore.hpp"

// rApply on every valid emitter (idx -1) or on the one at idx when it is valid.
template < typename F >
void MultiEmitter::forEachValid(s32 idx, const F& rApply) {
    if (idx == -1) {
        for (s32 i = 0; i < mEmitters.size(); i++) {
            SingleEmitter* pEmitter = getValidEmitter(i);
            if (pEmitter != nullptr) {
                rApply(pEmitter->mEmitter);
            }
        }
        return;
    }

    SingleEmitter* pEmitter = getValidEmitter(idx);
    if (pEmitter != nullptr) {
        rApply(pEmitter->mEmitter);
    }
}

void MultiEmitter::setGlobalRotation(const Vec3s& rRotation, s32 idx) {
    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->setGlobalRotation(rRotation); });
}

void MultiEmitter::setGlobalTranslation(const Vec3f& rTranslation, s32 idx) {
    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->mEmitter->mGlobalTrs.set(rTranslation); });
}

void MultiEmitter::setGlobalScale(const Vec3f& rScale, s32 idx) {
    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->setGlobalScale(rScale); });
}

void MultiEmitter::setGlobalSRTMatrix(const Mtx pMtx, s32 idx) {
    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->setGlobalSRTMatrix(pMtx); });
}

// A light-affected emitter's colour is mixed with the light in its callback, so for all of them only the callback's is set.
void MultiEmitter::setGlobalPrmColor(u8 r, u8 g, u8 b, s32 idx) {
    if (idx == -1) {
        mCallBack->mPrmColor.set(r, g, b, -1);
        if (!MR::isNearZero(mLightAffect)) {
            return;
        }
    }

    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->setGlobalPrmColor(r, g, b); });
}

void MultiEmitter::setGlobalEnvColor(u8 r, u8 g, u8 b, s32 idx) {
    if (idx == -1) {
        mCallBack->mEnvColor.set(r, g, b, -1);
        if (!MR::isNearZero(mLightAffect)) {
            return;
        }
    }

    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->setGlobalEnvColor(r, g, b); });
}

void MultiEmitter::setLocalScale(const Vec3f& rScale, s32 idx) {
    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->mEmitter->mLocalScl.set(rScale); });
}

void MultiEmitter::setDirectionalSpeed(f32 speed, s32 idx) {
    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->mEmitter->setDirectionalSpeed(speed); });
}

void MultiEmitter::setRate(f32 rate, s32 idx) {
    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->mEmitter->setRate(rate); });
}

void MultiEmitter::stopCalcEmitter(s32 idx) {
    forEachValid(idx, [](ParticleEmitter* pEmitter) { pEmitter->mEmitter->stopCalcEmitter(); });
}

void MultiEmitter::playCalcEmitter(s32 idx) {
    forEachValid(idx, [](ParticleEmitter* pEmitter) { pEmitter->mEmitter->playCalcEmitter(); });
}

void MultiEmitter::stopDrawParticle(s32 idx) {
    forEachValid(idx, [](ParticleEmitter* pEmitter) { pEmitter->mEmitter->stopDrawParticle(); });
}

void MultiEmitter::playDrawParticle(s32 idx) {
    forEachValid(idx, [](ParticleEmitter* pEmitter) { pEmitter->mEmitter->playDrawParticle(); });
}

void MultiEmitter::setParticleCallBackPtr(ParticleCallBack* pCallBack, s32 idx) {
    forEachValid(idx, [&](ParticleEmitter* pEmitter) { pEmitter->mEmitter->setParticleCallBackPtr(pCallBack); });
}

void MultiEmitter::pauseOff(s32 idx) {
    forEachValid(idx, [](ParticleEmitter* pEmitter) { pEmitter->pauseOff(); });
}

void ParticleEmitter::setGlobalRotation(const Vec3s& rRotation) {
    mEmitter->setGlobalRotation(rRotation);
}

void ParticleEmitter::setGlobalScale(const Vec3f& rScale) {
    mEmitter->setGlobalScale(rScale);
}

void ParticleEmitter::setGlobalSRTMatrix(const Mtx pMtx) {
    mEmitter->setGlobalSRTMatrix(pMtx);
}

void ParticleEmitter::setGlobalPrmColor(u8 r, u8 g, u8 b) {
    mEmitter->setGlobalPrmColor(r, g, b);
}

void ParticleEmitter::setGlobalEnvColor(u8 r, u8 g, u8 b) {
    mEmitter->setGlobalEnvColor(r, g, b);
}
