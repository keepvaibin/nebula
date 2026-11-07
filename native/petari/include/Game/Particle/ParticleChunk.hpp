#pragma once

#include "Game/Particle/ParticleCore.hpp"
#include "JSystem/JParticle/JPAList.hpp"

// Slots an emitter's particles live in: they never move while their emitter has them.
struct ParticleChunk {
    enum { cSlots = 32 };
    ParticleChunk* mpNext;
    JPANode< ParticleCore > mSlots[cSlots];
};
