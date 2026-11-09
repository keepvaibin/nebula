#pragma once

#include "Game/Effect/ParticleEmitter.hpp"
#include "Game/Util/Array.hpp"

class EffectSystem;

class ParticleEmitterHolder {
public:
    ParticleEmitterHolder(const EffectSystem*, int);

    void update(bool);
    void forceDeleteAllOneTimeEmitters();
    void forceDeleteAllEmitters();
    void requestMovementOnAllEmitters();
    // The first free record in chunk order.
    ParticleEmitter* findAvailableParticleEmitter();
    // pEmitter was just invalidated (EffectSystem's deletes, the only places a record is freed).
    void released(const ParticleEmitter* pEmitter) {
        if (pEmitter->mPlace < mFreeFrom) {
            mFreeFrom = pEmitter->mPlace;
        }
    }
    void requestMovementOffAllLoopEmitters();

    const EffectSystem* mEffectSystem;
    struct Chunk {
        MR::AssignableArray< ParticleEmitter > mEmitters;
        Chunk* mNext;
    };
    void claim(Chunk* pChunk, u32 base);

    Chunk mFirst;
    u32 mFreeFrom;  // every record before this place (counted across chunks, all of one size) is in use
};
