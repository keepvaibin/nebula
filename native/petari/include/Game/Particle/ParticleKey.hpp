#pragma once

#include "Game/Particle/ParticleData.hpp"

struct ParticleKey {
    explicit ParticleKey(const ParticleKeyData* pData) : mpData(pData) {
    }
    f32 calc(f32 frame) const;

    u32 getID() const {
        return mpData->mId;
    }

    const ParticleKeyData* mpData;
};
