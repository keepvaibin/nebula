#pragma once

#include "Game/Particle/ParticleData.hpp"

struct ParticleKey {
    // The emitter value a key animates (its data's id).
    enum Target {
        Target_Rate = 0,
        Target_VolumeSize = 1,
        Target_VolumeMinRad = 3,
        Target_LifeTime = 4,
        Target_AwayFromCenterSpeed = 6,
        Target_AwayFromAxisSpeed = 7,
        Target_DirSpeed = 8,
        Target_Spread = 9,
        Target_ScaleOut = 10,
    };

    explicit ParticleKey(const ParticleKeyData* pData) : mpData(pData) {
    }
    f32 calc(f32 frame) const;

    u32 getID() const {
        return mpData->mId;
    }

    const ParticleKeyData* mpData;
};
