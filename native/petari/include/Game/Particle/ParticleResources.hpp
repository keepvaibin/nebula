#pragma once

#include "Game/Particle/ParticleTexture.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class NebulaArena;
class ParticleResource;
struct ResTIMG;

// A cooked particle file's resources and textures (Game/Particle/ParticleData.hpp), read in place.
class ParticleResources {
public:
    ParticleResources(void const* pFile, NebulaArena* pHeap);
    ParticleResource* getResource(u32 userIndex) const {
        return userIndex < mUserIndexCount ? mpByUserIndex[userIndex] : nullptr;
    }
    const ResTIMG* swapTexture(ResTIMG const*, char const*);

    void use(u32 idx, u32 slot) {
        mpTexArr[idx]->use(slot);
    }

public:
    NebulaArena* mpHeap;
    ParticleResource** mpResArr;
    ParticleTexture** mpTexArr;
    // The resources by user index (null: none).
    ParticleResource** mpByUserIndex;
    u32 mResNum;
    u32 mTexNum;
    u32 mUserIndexCount;
};
