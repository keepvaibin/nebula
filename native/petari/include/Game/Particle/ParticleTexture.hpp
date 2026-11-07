#pragma once

#include "JSystem/JParticle/JPANativeColor.hpp"

#include "JSystem/JUtility/JUTTexture.hpp"
#include "Game/Particle/ParticleData.hpp"

class ParticleTexture {
public:
    ParticleTexture(const char* pName, const NebulaTextureInfo* pInfo) : mInfo(ResTIMG::fromNative(*pInfo)), mpName(pName) {
        mTexture.storeTIMG(&mInfo);
    }

    // This texture in the particle material's slot.
    void use(u32 slot) {
        JPANativeColor::texture(slot, mTexture.texObj());
    }

    JUTTexture* getJUTTexture() {
        return &mTexture;
    }
    const char* getName() const {
        return mpName;
    }

public:
    ResTIMG mInfo;
    JUTTexture mTexture;
    const char* mpName;
};
