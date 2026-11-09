#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/NebulaFrameClock.hpp"

ImageEffectResource::ImageEffectResource()
    : _0(nullptr), _8(nullptr), _C(nullptr) {
}

void ImageEffectResource::createBlurTexture() {
    if (_0 == nullptr) {
        _0 = createTexture(1.0f);
    }

    if (_8 == nullptr) {
        _8 = createTexture(0.5f);
    }
}

void ImageEffectResource::createDOFTexture() {
    if (_0 == nullptr) {
        _0 = createTexture(1.0f);
    }

    if (_C == nullptr) {
        _C = createTexture(0.25f);
    }

    if (_8 == nullptr) {
        _8 = createTexture(0.5f);
    }
}

GameTexture* ImageEffectResource::createTexture(f32 scale) {
    GameTexture* pTexture;

    NebulaArena::CurrentScope heapRestorer(Arenas::scene());

    pTexture = GameTexture::createTarget(scale, scale);
    pTexture->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);

    return pTexture;
}
