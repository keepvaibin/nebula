#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_createBlurTexture_f1, "createBlurTexture.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDOFTexture_f1, "createDOFTexture.f1", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_createDOFTexture_f2, "createDOFTexture.f2", 0.5f);

ImageEffectResource::ImageEffectResource()
    : mFullTexture(nullptr), mHalfTexture(nullptr), mQuarterTexture(nullptr) {
}

void ImageEffectResource::createBlurTexture() {
    if (mFullTexture == nullptr) {
        mFullTexture = createTexture(1.0f);
    }

    if (mHalfTexture == nullptr) {
        mHalfTexture = createTexture(cNum_createBlurTexture_f1);
    }
}

void ImageEffectResource::createDOFTexture() {
    if (mFullTexture == nullptr) {
        mFullTexture = createTexture(1.0f);
    }

    if (mQuarterTexture == nullptr) {
        mQuarterTexture = createTexture(cNum_createDOFTexture_f1);
    }

    if (mHalfTexture == nullptr) {
        mHalfTexture = createTexture(cNum_createDOFTexture_f2);
    }
}

GameTexture* ImageEffectResource::createTexture(f32 scale) {
    GameTexture* pTexture;

    NebulaArena::CurrentScope heapRestorer(Arenas::scene());

    pTexture = GameTexture::createTarget(scale, scale);
    pTexture->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);

    return pTexture;
}
