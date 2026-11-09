#include "Game/Effect/ParticleEmitterHolder.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/ParticleEmitter.hpp"

#include "Game/System/NebulaArena.hpp"
#include "Game/Particle/EmitterCore.hpp"

ParticleEmitterHolder::ParticleEmitterHolder(const EffectSystem* pEffectSystem, int numEmitters) : mEffectSystem(pEffectSystem) {
    mFirst.mEmitters.init(numEmitters);
    mFirst.mNext = nullptr;
}

void ParticleEmitterHolder::update(bool ignorePause) {
    for (Chunk* pChunk = &mFirst; pChunk != nullptr; pChunk = pChunk->mNext)
    for (ParticleEmitter* pEmitter = pChunk->mEmitters.begin(); pEmitter != pChunk->mEmitters.end(); pEmitter++) {
        EmitterCore* pBaseEmitter = pEmitter->mEmitter;
        if (pBaseEmitter == nullptr) {
            continue;
        }

        // Group 1 is the 3D pause-ignoring group.
        if (ignorePause != (pBaseEmitter->getGroupID() == 1)) {
            continue;
        }

        if (pBaseEmitter->isEnableDeleteEmitter()) {
            mEffectSystem->forceDeleteEmitter(pEmitter);
        } else if (!pEmitter->mUpdated) {
            if (MR::Effect::getLinkSingleEmitter(pBaseEmitter) != nullptr) {
                pBaseEmitter->getEmitterCallBackPtr()->init(pBaseEmitter);
            }

            pEmitter->mUpdated = true;
        }
    }
}

void ParticleEmitterHolder::forceDeleteAllOneTimeEmitters() {
    for (Chunk* pChunk = &mFirst; pChunk != nullptr; pChunk = pChunk->mNext)
    for (ParticleEmitter* pEmitter = pChunk->mEmitters.begin(); pEmitter != pChunk->mEmitters.end(); pEmitter++) {
        if (pEmitter->mEmitter == nullptr) {
            continue;
        }

        if (pEmitter->isContinuousParticle()) {
            continue;
        }

        mEffectSystem->forceDeleteEmitter(pEmitter);
    }
}

void ParticleEmitterHolder::forceDeleteAllEmitters() {
    for (Chunk* pChunk = &mFirst; pChunk != nullptr; pChunk = pChunk->mNext)
    for (ParticleEmitter* pEmitter = pChunk->mEmitters.begin(); pEmitter != pChunk->mEmitters.end(); pEmitter++) {
        mEffectSystem->forceDeleteEmitter(pEmitter);
    }
}

void ParticleEmitterHolder::requestMovementOnAllEmitters() {
    for (Chunk* pChunk = &mFirst; pChunk != nullptr; pChunk = pChunk->mNext)
    for (ParticleEmitter* pEmitter = pChunk->mEmitters.begin(); pEmitter != pChunk->mEmitters.end(); pEmitter++) {
        EmitterCore* pBaseEmitter = pEmitter->mEmitter;
        if (pBaseEmitter == nullptr) {
            continue;
        }

        if (pBaseEmitter->getGroupID() == 1 || pBaseEmitter->getGroupID() == 7) {
            continue;
        }

        pEmitter->pauseOff();
    }
}

ParticleEmitter* ParticleEmitterHolder::findAvailableParticleEmitter() {
    Chunk* pLast = nullptr;
    for (Chunk* pChunk = &mFirst; pChunk != nullptr; pChunk = pChunk->mNext) {
        for (auto& item : pChunk->mEmitters) {
            if (!item.isValid()) {
                return &item;
            }
        }
        pLast = pChunk;
    }

    // Full: as many again, in the arena this lies in. Effects keep pointers to the records, so the old ones stay.
    Chunk* pChunk = new (NebulaArena::findFromRoot(this), 0) Chunk();
    pChunk->mEmitters.mArr = new (NebulaArena::findFromRoot(this), 0) ParticleEmitter[mFirst.mEmitters.size()];
    pChunk->mEmitters.mMaxSize = mFirst.mEmitters.size();
    pChunk->mNext = nullptr;
    pLast->mNext = pChunk;
    return &pChunk->mEmitters[0];
}

void ParticleEmitterHolder::requestMovementOffAllLoopEmitters() {
    for (Chunk* pChunk = &mFirst; pChunk != nullptr; pChunk = pChunk->mNext)
    for (ParticleEmitter* pEmitter = pChunk->mEmitters.begin(); pEmitter != pChunk->mEmitters.end(); pEmitter++) {
        if (pEmitter->mEmitter == nullptr) {
            continue;
        }

        if (!pEmitter->isContinuousParticle()) {
            continue;
        }

        pEmitter->pauseOn();
    }
}

inline bool ParticleEmitter::isValid() const {
    return mEmitter != nullptr;
}

