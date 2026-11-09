#include "Game/Effect/ParticleEmitter.hpp"
#include "Game/Particle/EmitterCore.hpp"

ParticleEmitter::ParticleEmitter() : mEmitter(), mPaused(), mUpdated(), mHolder(), mPlace() {
}

void ParticleEmitter::invalidate() {
    mEmitter = nullptr;
}

void ParticleEmitter::init() {
    mPaused = false;
    mUpdated = false;
    mEmitter->mStatus |= EmitterStatus_Immortal;
}

void ParticleEmitter::pauseOn() {
    if (mEmitter->mStatus & EmitterStatus_StopCalc) {
        return;
    }

    mEmitter->mStatus |= EmitterStatus_StopCalc;
    mPaused = true;
}

void ParticleEmitter::pauseOff() {
    if (mPaused) {
        mEmitter->mStatus &= ~EmitterStatus_StopCalc;
        mPaused = false;
    }
}

// An emitter with no frame limit.
bool ParticleEmitter::isContinuousParticle() const {
    if (mEmitter == nullptr) {
        return false;
    }

    return mEmitter->mMaxFrame == 0;
}
