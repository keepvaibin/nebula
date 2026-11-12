#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaFrameClock.hpp"

ImageEffectResource::ImageEffectResource()
    : mFullTexture(nullptr), mHalfTexture(nullptr), mQuarterTexture(nullptr) {
}

void ImageEffectResource::createBlurTexture() {
    if (mFullTexture == nullptr) {
        mFullTexture = createTexture(1.0f);
    }

    if (mHalfTexture == nullptr) {
        mHalfTexture = createTexture(0.5f);
    }
}

void ImageEffectResource::createDOFTexture() {
    if (mFullTexture == nullptr) {
        mFullTexture = createTexture(1.0f);
    }

    if (mQuarterTexture == nullptr) {
        mQuarterTexture = createTexture(0.25f);
    }

    if (mHalfTexture == nullptr) {
        mHalfTexture = createTexture(0.5f);
    }
}

GameTexture* ImageEffectResource::createTexture(f32 scale) {
    GameTexture* pTexture;

    NebulaArena::CurrentScope heapRestorer(Arenas::scene());

    pTexture = GameTexture::createTarget(scale, scale);
    pTexture->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);

    return pTexture;
}
