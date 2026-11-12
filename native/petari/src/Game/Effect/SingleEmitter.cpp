#include "Game/Effect/SingleEmitter.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"

SingleEmitter::SingleEmitter() {
    mEmitter = nullptr;
    mResourceId = 0;
    mGroupId = 0;
    mSpawn = SINGLE_EMITTER_UNDEF_SPAWN;
}

void SingleEmitter::init(u16 resourceId) {
    mResourceId = resourceId;
}

void SingleEmitter::deleteEmitter() {
    if (isValid()) {
        MR::Effect::deleteParticleEmitter(mEmitter);
    }
}

void SingleEmitter::scanParticleEmitter(EffectSystem* pSystem) {
    ParticleEmitter* emitter;
    if (mSpawn == SINGLE_EMITTER_UNDEF_SPAWN && mResourceId != cNoResource) {
        emitter = pSystem->createEmitter(mResourceId, mGroupId, 0);
        if (emitter == nullptr) {
            return;
        }
        mSpawn = emitter->isContinuousParticle() ? SINGLE_EMITTER_CONTINUOUS_SPAWN : SINGLE_EMITTER_ONE_TIME_SPAWN;
        pSystem->forceDeleteEmitter(emitter);
    }
}

bool SingleEmitter::isOneTime() const {
    if (mEmitter) {
        return !mEmitter->isContinuousParticle();
    } else {
        return mSpawn == SINGLE_EMITTER_ONE_TIME_SPAWN;
    }
}

bool SingleEmitter::isValid() const {
    if (mEmitter == nullptr) {
        return false;
    } else {
        return mEmitter->mEmitter != nullptr;
    }
}

void SingleEmitter::link(ParticleEmitter* pEmitter) {
    mEmitter = pEmitter;
    MR::Effect::setLinkSingleEmitter(pEmitter, this);
}

void SingleEmitter::unlink() {
    MR::Effect::setLinkSingleEmitter(mEmitter, nullptr);
    mEmitter = nullptr;
}