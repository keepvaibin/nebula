#include "Game/Effect/ParticleEmitterHolder.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/ParticleEmitter.hpp"

#include "Game/System/NebulaArena.hpp"
#include <JSystem/JParticle/JPAEmitter.hpp>
#include <algorithm>

ParticleEmitterHolder::ParticleEmitterHolder(const EffectSystem* pEffectSystem, int numEmitters) : mEffectSystem(pEffectSystem) {
    mFirst.mEmitters.init(numEmitters);
    mFirst.mNext = nullptr;
}

void ParticleEmitterHolder::update(bool param1) {
    for (Chunk* pChunk = &mFirst; pChunk != nullptr; pChunk = pChunk->mNext)
    for (ParticleEmitter* pEmitter = pChunk->mEmitters.begin(); pEmitter != pChunk->mEmitters.end(); pEmitter++) {
        JPABaseEmitter* pBaseEmitter = pEmitter->mEmitter;
        if (pBaseEmitter == nullptr) {
            continue;
        }

        if (param1 != (pBaseEmitter->getGroupID() == 1)) {
            continue;
        }

        const bool shouldDelete = pBaseEmitter->checkStatus(0x8) && pBaseEmitter->getParticleNumber() == 0;

        if (shouldDelete) {
            mEffectSystem->forceDeleteEmitter(pEmitter);
        } else if (!pEmitter->mStopped) {
            if (MR::Effect::getLinkSingleEmitter(pBaseEmitter) != nullptr) {
                pBaseEmitter->getEmitterCallBackPtr()->init(pBaseEmitter);
            }

            pEmitter->mStopped = true;
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
        JPABaseEmitter* pBaseEmitter = pEmitter->mEmitter;
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

    // Full: as many again, in the heap this lies in. Effects keep pointers to the records, so the old ones stay.
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

