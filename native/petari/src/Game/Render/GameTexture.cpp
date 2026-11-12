#include "Game/Render/GameTexture.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include <string.h>

GameTexture::GameTexture(u32 width, u32 height, u8 layout) {
    makeImage(width, height, layout);
    RenderTexture::linear(mImage, layout, width, height, 1);
}

GameTexture::~GameTexture() {
    delete[] mOwned;
}

// A record and its pixels (black until the game writes them or a frame is copied in), clamped and linear.
void GameTexture::makeImage(u32 width, u32 height, u8 layout) {
    const u32 bytes = RenderTexture::linearSize(layout, width, height, 1);
    mOwned = new (32) u8[sizeof(ResTIMG) + bytes];
    ResTIMG* const pInfo = reinterpret_cast< ResTIMG* >(mOwned);
    *pInfo = ResTIMG{};
    pInfo->mFormat = layout;
    pInfo->mWidth = width;
    pInfo->mHeight = height;
    pInfo->mWrapS = NEBULA_WRAP_CLAMP;
    pInfo->mWrapT = NEBULA_WRAP_CLAMP;
    pInfo->mMinType = NEBULA_FILTER_LINEAR;
    pInfo->mMagType = NEBULA_FILTER_LINEAR;
    pInfo->mImageNum = 1;
    pInfo->mImageDataOffset = sizeof(ResTIMG);
    set(pInfo);
    memset(mImage, 0, bytes);
}

void GameTexture::set(const ResTIMG* pInfo) {
    if (pInfo == nullptr) {
        return;
    }
    mInfo = pInfo;
    mImage = const_cast< u8* >(pInfo->image());
    SiteTextures::init(&mSite, mImage, pInfo->mWidth, pInfo->mHeight, pInfo->mWrapS, pInfo->mWrapT);
    SiteTextures::setFilter(&mSite, pInfo->mMinType, pInfo->mMagType, pInfo->mMinLod, pInfo->mMaxLod, pInfo->mLodBias, false,
                            pInfo->mMaxAnisotropy);
}

void GameTexture::copyFrame() {
    if (mOwned != nullptr) {
        Frame::copyFrame(mImage);
    }
}

GameTexture* GameTexture::createPixelTarget(u32 width, u32 height) {
    GameTexture* const pTexture = new GameTexture();
    // A key: four texels the frame passes name, never drawn from as pixels; its record and sampling say the target's size.
    pTexture->makeImage(4, 4, NEBULA_TEXTURE_LINEAR_L8);
    ResTIMG* const pInfo = reinterpret_cast< ResTIMG* >(pTexture->mOwned);
    pInfo->mWidth = width;
    pInfo->mHeight = height;
    pTexture->mSite.mWidth = width;
    pTexture->mSite.mHeight = height;
    return pTexture;
}

GameTexture* GameTexture::createTarget(f32 across, f32 down) {
    GameTexture* const pTexture =
        createPixelTarget(static_cast< u32 >(across * NebulaFrameClock::cEffectTexelsAcross + 0.5f),
                          static_cast< u32 >(down * NebulaFrameClock::cScreenHeight + 0.5f));
    pTexture->mShareAcross = across;
    pTexture->mShareDown = down;
    return pTexture;
}
