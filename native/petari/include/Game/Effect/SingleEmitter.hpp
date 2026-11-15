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
    // The resource id of a particle name the resources lack (ParticleResourceHolder::getUserIndex's miss): the slot keeps
    // its place in the effect's emitter list and is never created.
    static const u16 cNoResource = 0xFFFF;

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
    u16 mResourceId;
    u8 mGroupId;
    s8 mSpawn;
};
