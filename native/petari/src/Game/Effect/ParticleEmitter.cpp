#include "Game/Effect/ParticleEmitter.hpp"
#include <JSystem/JParticle/JPAEmitter.hpp>

ParticleEmitter::ParticleEmitter() : mEmitter(), mPaused(), mStopped() {
}

void ParticleEmitter::invalidate() {
    mEmitter = nullptr;
}

void ParticleEmitter::init(u16 unused) {
    mPaused = false;
    mStopped = false;
    mEmitter->mStatus |= JPAEmtrStts_Immortal;
}

void ParticleEmitter::pauseOn() {
    bool paused = mEmitter != nullptr && mEmitter->mStatus & JPAEmtrStts_StopCalc;

    if (paused) {
        return;
    }

    mEmitter->mStatus |= JPAEmtrStts_StopCalc;
    mPaused = true;
}

void ParticleEmitter::pauseOff() {
    if (mPaused) {
        mEmitter->mStatus &= ~JPAEmtrStts_StopCalc;
        mPaused = false;
    }
}

// No source in the decompilation; written from the RMGE01 assembly (0x800C9FE8): an emitter with no frame limit.
bool ParticleEmitter::isContinuousParticle() const {
    if (mEmitter == nullptr) {
        return false;
    }

    return mEmitter->mMaxFrame == 0;
}
