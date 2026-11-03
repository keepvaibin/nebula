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
    ParticleEmitter* findAvailableParticleEmitter();
    void requestMovementOffAllLoopEmitters();

    const EffectSystem* mEffectSystem;
    struct Chunk {
        MR::AssignableArray< ParticleEmitter > mEmitters;
        Chunk* mNext;
    };

    Chunk mFirst;
};
