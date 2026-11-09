#pragma once

#include "Game/Particle/ParticleDraw.hpp"

#include "Game/Render/GameTexture.hpp"
#include "Game/Particle/ParticleData.hpp"

class ParticleTexture {
public:
    ParticleTexture(const char* pName, const NebulaTextureInfo* pInfo) : mInfo(ResTIMG::fromNative(*pInfo)), mpName(pName) {
        mTexture.set(&mInfo);
    }

    // This texture in the particle material's slot.
    void use(u32 slot) {
        ParticleMaterial::texture(slot, &mTexture.site());
    }

    GameTexture* getTexture() {
        return &mTexture;
    }
    const char* getName() const {
        return mpName;
    }

public:
    ResTIMG mInfo;
    GameTexture mTexture;
    const char* mpName;
};
