#include <string.h>
#include "Game/System/NebulaArena.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include "JSystem/JUtility/JUTTexture.hpp"

JUTTexture::JUTTexture(int width, int height, u8 layout) {
    mFlag = 1;
    u32 bufSize = J3DNativeTexture::linearSize(layout, width, height, 1);

    ResTIMG* texBuf = reinterpret_cast< ResTIMG* >(new (sizeof(ResTIMG)) u8[bufSize + sizeof(ResTIMG)]);
    _3C = texBuf;
    texBuf->mFormat = layout;
    texBuf->mTransparency = 0;
    texBuf->mWidth = width;
    texBuf->mHeight = height;
    texBuf->mWrapS = NEBULA_WRAP_CLAMP;
    texBuf->mWrapT = NEBULA_WRAP_CLAMP;
    texBuf->mImage = nullptr;
    texBuf->mMipmap = false;
    texBuf->mDoEdgeLod = false;
    texBuf->mBiasClamp = false;
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
    J3DNativeTexture::registerLinear(mImage, layout, width, height, 1);
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
        mMinLod = static_cast< s8 >(getTexInfo()->mMinLod);
        mMaxLod = static_cast< s8 >(getTexInfo()->mMaxLod);
        mLodBias = static_cast< s16 >(getTexInfo()->mLodBias);
        initTexObj();
    }
}

void JUTTexture::init() {
    initTexObj();
}

void JUTTexture::initTexObj() {
    const u8* image = mTIMG->image();
    // A texture with its own buffer (made at run time: a capture's or target's picture, or texels the game writes and
    // hands over itself) has nothing to hand over yet; a texture over loaded data hands its record over.
    if ((mFlag & 0x1) == 0) {
        J3DNativeTexture::registerRecord(image);
    }
    J3DNativeTexture::init(&mObj, image, mTIMG->mWidth, mTIMG->mHeight, mWrapS, mWrapT);
    J3DNativeTexture::setFilter(&mObj, mMinType, mMagType, mMinLod / 8.0f, mMaxLod / 8.0f, mLodBias / 100.0f,
                                mTIMG->mDoEdgeLod, mTIMG->mMaxAnisotropy);
}

void JUTTexture::frameColor() {
    if (mFlag & 0x1) {
        J3DNativeTarget::framePass(mImage, NEBULA_TARGET_RGB);
    }
}

void JUTTexture::frameColorAlpha() {
    if (mFlag & 0x1) {
        J3DNativeTarget::framePass(mImage, NEBULA_TARGET_RGBA);
    }
}


