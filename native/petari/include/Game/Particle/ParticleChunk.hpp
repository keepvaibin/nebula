#pragma once

#include "Game/Particle/ParticleList.hpp"

// Slots an emitter's particles live in: they never move while their emitter has them.
struct ParticleChunk {
    enum { cSlots = 32 };
    ParticleChunk* mpNext;
    ParticleSlot mSlots[cSlots];
};
