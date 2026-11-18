#include "Game/Effect/ParticleEmitterHolder.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Effect/EffectSystemUtil.hpp"
#include "Game/Effect/ParticleEmitter.hpp"

#include "Game/System/NebulaArena.hpp"
#include "Game/Particle/EmitterCore.hpp"

ParticleEmitterHolder::ParticleEmitterHolder(const EffectSystem* pEffectSystem, int numEmitters)
    : mEffectSystem(pEffectSystem), mFreeFrom(0), mLive(), mLiveWords(0) {
    mFirst.mEmitters.init(numEmitters);
    mFirst.mNext = nullptr;
    claim(&mFirst, 0);
}

void ParticleEmitterHolder::claim(Chunk* pChunk, u32 base) {
    for (u32 i = 0; i < static_cast< u32 >(pChunk->mEmitters.size()); i++) {
        ParticleEmitter& rItem = pChunk->mEmitters[static_cast< int >(i)];
        rItem.mHolder = this;
        rItem.mPlace = base + i;
    }
}

void ParticleEmitterHolder::update(bool ignorePause) {
    auto visit = [this, ignorePause](ParticleEmitter* pEmitter) {
        EmitterCore* pBaseEmitter = pEmitter->mEmitter;
        if (pBaseEmitter == nullptr) {
            return;
        }

        // Group 1 is the 3D pause-ignoring group.
        if (ignorePause != (pBaseEmitter->getGroupID() == 1)) {
            return;
        }

        if (pBaseEmitter->isEnableDeleteEmitter()) {
            mEffectSystem->forceDeleteEmitter(pEmitter);
        } else if (!pEmitter->mUpdated) {
            if (MR::Effect::getLinkSingleEmitter(pBaseEmitter) != nullptr) {
                pBaseEmitter->getEmitterCallBackPtr()->init(pBaseEmitter);
            }

            pEmitter->mUpdated = true;
        }
    };

#if defined(NEBULA_PLAIN_MODULE)
    // The records in use, in place order as the walk of every record meets them (P-141). A word is read again after each
    // visit, and the count of words and chunks at each step, as a visit may fill or free records further on (a callback
    // may make an emitter).
    const u32 size = static_cast< u32 >(mFirst.mEmitters.size());
    Chunk* pChunk = &mFirst;
    u32 base = 0;
    for (u32 word = 0; word < mLiveWords; word++) {
        u32 next = 0;
        while (next < 64u) {
            const u64 bits = mLive[word] >> next;
            if (bits == 0) {
                break;
            }

            const u32 place = word * 64u + next + static_cast< u32 >(__builtin_ctzll(bits));
            while (place >= base + size) {
                pChunk = pChunk->mNext;
                base += size;
            }
            visit(&pChunk->mEmitters[static_cast< int >(place - base)]);
            next = place - word * 64u + 1u;
        }
    }
#else
    for (Chunk* pChunk = &mFirst; pChunk != nullptr; pChunk = pChunk->mNext)
    for (ParticleEmitter* pEmitter = pChunk->mEmitters.begin(); pEmitter != pChunk->mEmitters.end(); pEmitter++) {
        visit(pEmitter);
    }
#endif
}

void ParticleEmitterHolder::taken(const ParticleEmitter* pEmitter) {
#if defined(NEBULA_PLAIN_MODULE)
    const u32 word = pEmitter->mPlace / 64u;
    if (word >= mLiveWords) {
        u64* const pWords = mLive.room(word + 1u);
        for (u32 i = mLiveWords; i <= word; i++) {
            pWords[i] = 0;
        }
        mLiveWords = word + 1u;
    }
    mLive[word] |= 1ull << (pEmitter->mPlace % 64u);
#else
    (void)pEmitter;
#endif
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
    const u32 size = static_cast< u32 >(mFirst.mEmitters.size());
    Chunk* pLast = nullptr;
    u32 base = 0;
    for (Chunk* pChunk = &mFirst; pChunk != nullptr; pChunk = pChunk->mNext, base += size) {
        // Chunks wholly before mFreeFrom are full.
        for (u32 i = mFreeFrom > base ? mFreeFrom - base : 0; i < size; i++) {
            ParticleEmitter& rItem = pChunk->mEmitters[static_cast< int >(i)];
            if (!rItem.isValid()) {
                mFreeFrom = base + i;
                return &rItem;
            }
        }
        pLast = pChunk;
    }
    mFreeFrom = base;

    // Full: as many again, in the arena this lies in. Effects keep pointers to the records, so the old ones stay.
    Chunk* pChunk = new (NebulaArena::findFromRoot(this), 0) Chunk();
    pChunk->mEmitters.mArr = new (NebulaArena::findFromRoot(this), 0) ParticleEmitter[mFirst.mEmitters.size()];
    pChunk->mEmitters.mMaxSize = mFirst.mEmitters.size();
    pChunk->mNext = nullptr;
    pLast->mNext = pChunk;
    claim(pChunk, base);
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

