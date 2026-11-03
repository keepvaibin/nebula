#pragma once

#include "JSystem/JParticle/JPANativeColor.hpp"

#include "JSystem/JUtility/JUTTexture.hpp"

struct JPATextureData {
    char field_0x00[0x0C];
    char mName[0x14];
    ResTIMG mResTIMG;
};

class JPATexture {
public:
    JPATexture(u8 const*);
    virtual ~JPATexture();

    // This texture in the particle material's slot.
    void use(u32 slot) {
        JPANativeColor::texture(slot, mTexture.texObj());
    }

    JUTTexture* getJUTTexture() {
        return &mTexture;
    }
    const char* getName() const {
        return mpData->mName;
    }

public:
    JUTTexture mTexture;
    const JPATextureData* mpData;
};
