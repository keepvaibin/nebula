#pragma once

#include "Game/Effect/ParticleEmitter.hpp"

class EffectSystem;

enum SingleEmitterSpawn {
    SINGLE_EMITTER_ONE_TIME_SPAWN,
    SINGLE_EMITTER_CONTINUOUS_SPAWN,
    SINGLE_EMITTER_UNDEF_SPAWN,
};

class SingleEmitter {
public:
    SingleEmitter();

    void init(u16);
    void deleteEmitter();
    void scanParticleEmitter(EffectSystem*);
    bool isOneTime() const;
    bool isValid() const;
    void link(ParticleEmitter*);
    void unlink();

    void setGroupID(u8);

    ParticleEmitter* mEmitter;
    u16 _4;
    u8 mGroupId;
    s8 mSpawn;
};
