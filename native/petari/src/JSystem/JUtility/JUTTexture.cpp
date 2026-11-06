#include "Game/Render/TypedDraw.hpp"
#include <string.h>
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/FrameList.hpp"
#include "JSystem/JUtility/JUTTexture.hpp"

JUTTexture::JUTTexture(int width, int height, u8 layout) {
    makeBuffer(width, height, layout);
    RenderTexture::linear(mImage, layout, width, height, 1);
}

JUTTexture::JUTTexture(TargetOnly) {
    makeBuffer(4, 4, NEBULA_TEXTURE_LINEAR_L8);
}

void JUTTexture::makeBuffer(int width, int height, u8 layout) {
    mFlag = 1;
    u32 bufSize = RenderTexture::linearSize(layout, width, height, 1);

    ResTIMG* texBuf = reinterpret_cast< ResTIMG* >(new (32) u8[bufSize + sizeof(ResTIMG)]);
    _3C = texBuf;
    texBuf->mFormat = layout;
    texBuf->mTransparency = 0;
    texBuf->mWidth = width;
    texBuf->mHeight = height;
    texBuf->mWrapS = NEBULA_WRAP_CLAMP;
    texBuf->mWrapT = NEBULA_WRAP_CLAMP;
    texBuf->mImage = nullptr;
    texBuf->mMipmap = false;
    texBuf->mMaxAnisotropy = 0;
    texBuf->mMinType = NEBULA_FILTER_LINEAR;
    texBuf->mMagType = NEBULA_FILTER_LINEAR;
    texBuf->mMinLod = 0;
    texBuf->mMaxLod = 0;
    texBuf->mImageNum = 1;
    texBuf->mLodBias = 0;
    texBuf->mImageDataOffset = sizeof(ResTIMG);
    storeTIMG(texBuf);
    // Black until its first capture or until the game writes it.
    memset(mImage, 0, bufSize);
}

JUTTexture::~JUTTexture() {
    if (mFlag & 0x1) {
        delete[] _3C;
    }
}

void JUTTexture::storeTIMG(const ResTIMG* pTIMG) {
    if (pTIMG != nullptr) {
        mTIMG = pTIMG;
        mImage = const_cast< u8* >(pTIMG->image());

        mWrapS = getTexInfo()->mWrapS;
        mWrapT = getTexInfo()->mWrapT;
        mMinType = getTexInfo()->mMinType;
        mMagType = getTexInfo()->mMagType;
        mMinLod = getTexInfo()->mMinLod;
        mMaxLod = getTexInfo()->mMaxLod;
        mLodBias = getTexInfo()->mLodBias;
        initTexObj();
    }
}

void JUTTexture::init() {
    initTexObj();
}

void JUTTexture::initTexObj() {
    const u8* image = mTIMG->image();
    SiteTextures::init(&mObj, image, mTIMG->mWidth, mTIMG->mHeight, mWrapS, mWrapT);
    SiteTextures::setFilter(&mObj, mMinType, mMagType, mMinLod, mMaxLod, mLodBias,
                                false, mTIMG->mMaxAnisotropy);
}

void JUTTexture::frameColor() {
    if (mFlag & 0x1) {
        Frame::copyFrame(mImage);
    }
}

JUTTexture* JUTTexture::createPixelTarget(u32 width, u32 height) {
    JUTTexture* pTexture = new JUTTexture(TargetOnly{});
    ResTIMG* pInfo = const_cast< ResTIMG* >(pTexture->mTIMG);
    pInfo->mWidth = width;
    pInfo->mHeight = height;
    pTexture->initTexObj();
    return pTexture;
}

JUTTexture* JUTTexture::createTarget(f32 across, f32 down) {
    JUTTexture* pTexture = new JUTTexture(TargetOnly{});
    pTexture->mShareAcross = across;
    pTexture->mShareDown = down;
    ResTIMG* pInfo = const_cast< ResTIMG* >(pTexture->mTIMG);
    pInfo->mWidth = static_cast< u32 >(across * cAuthoredAcross + 0.5f);
    pInfo->mHeight = static_cast< u32 >(down * cAuthoredDown + 0.5f);
    pTexture->initTexObj();
    return pTexture;
}


