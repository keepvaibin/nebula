#include <string.h>
#include "Game/System/NebulaArena.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include "JSystem/JUtility/JUTTexture.hpp"

JUTTexture::JUTTexture(int width, int height, GpuTexFmt format) {
    mFlag = 1;
    u32 bufSize = J3DNativeTexture::imageSize(format, width, height, 1);

    ResTIMG* texBuf = reinterpret_cast< ResTIMG* >(new (sizeof(ResTIMG)) u8[bufSize + sizeof(ResTIMG)]);
    _3C = texBuf;
    texBuf->mFormat = format;
    texBuf->mTransparency = 0;
    texBuf->mWidth = width;
    texBuf->mHeight = height;
    texBuf->mWrapS = GPU_CLAMP;
    texBuf->mWrapT = GPU_CLAMP;
    texBuf->mImage = nullptr;
    texBuf->mMipmap = false;
    texBuf->mDoEdgeLod = false;
    texBuf->mBiasClamp = false;
    texBuf->mMaxAnisotropy = GPU_ANISO_1;
    texBuf->mMinType = GPU_LINEAR;
    texBuf->mMagType = GPU_LINEAR;
    texBuf->mMinLod = 0;
    texBuf->mMaxLod = 0;
    texBuf->mImageNum = 1;
    texBuf->mLodBias = 0;
    texBuf->mImageDataOffset = sizeof(ResTIMG);
    storeTIMG(texBuf);
    // Black until its first capture or until the game writes it.
    memset(mImage, 0, bufSize);
    J3DNativeTexture::registerImage(mImage, NEBULA_TEXTURE_LINEAR_L8, width, height, 1);
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
    GpuBool mipmapEnabled;
    if (mTIMG->mMipmap != 0) {
        mipmapEnabled = 1;
    } else {
        mipmapEnabled = 0;
    }
    u8* image = const_cast< u8* >(mTIMG->image());
    // A texture with its own buffer (made at run time: a capture's or target's picture, or texels the game writes and
    // hands over itself) has nothing to hand over yet; a texture over loaded data hands its record over.
    if ((mFlag & 0x1) == 0) {
        J3DNativeTexture::registerImage(image, mTIMG->mFormat, mTIMG->mWidth, mTIMG->mHeight,
                                        mTIMG->mImageNum != 0 ? mTIMG->mImageNum : 1);
    }
    J3DNativeTexture::init(&mObj, image, mTIMG->mWidth, mTIMG->mHeight, static_cast< GpuTexFmt >(mTIMG->mFormat), static_cast< GpuTexWrapMode >(mWrapS),
                 static_cast< GpuTexWrapMode >(mWrapT), mipmapEnabled);

    J3DNativeTexture::initLOD(&mObj, static_cast< GpuTexFilter >(mMinType), static_cast< GpuTexFilter >(mMagType), mMinLod / 8.0f, mMaxLod / 8.0f,
                    mLodBias / 100.0f, mTIMG->mBiasClamp, mTIMG->mDoEdgeLod, static_cast< GpuAnisotropy >(mTIMG->mMaxAnisotropy));
}

void JUTTexture::frameColor(bool half) {
    if (mFlag & 0x1) {
        const int scale = half ? 2 : 1;
        J3DNativeTarget::capture(mImage, 0, 0, mTIMG->mWidth * scale, mTIMG->mHeight * scale, NEBULA_TARGET_RGB,
                                 half ? NEBULA_CAPTURE_HALF : 0u);
    }
}

void JUTTexture::frameColorAlpha() {
    if (mFlag & 0x1) {
        J3DNativeTarget::capture(mImage, 0, 0, mTIMG->mWidth, mTIMG->mHeight, NEBULA_TARGET_RGBA, 0u);
    }
}

void JUTTexture::frameAlpha(bool half) {
    if (mFlag & 0x1) {
        const int scale = half ? 2 : 1;
        J3DNativeTarget::capture(mImage, 0, 0, mTIMG->mWidth * scale, mTIMG->mHeight * scale, NEBULA_TARGET_ALPHA,
                                 half ? NEBULA_CAPTURE_HALF : 0u);
    }
}

void JUTTexture::frameDepth(bool half) {
    if (mFlag & 0x1) {
        const int scale = half ? 2 : 1;
        J3DNativeTarget::capture(mImage, 0, 0, mTIMG->mWidth * scale, mTIMG->mHeight * scale, NEBULA_TARGET_RED,
                                 NEBULA_CAPTURE_DEPTH | (half ? NEBULA_CAPTURE_HALF : 0u));
    }
}

