#include "JSystem/JKernel/JKRHeap.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include "JSystem/JUtility/JUTPalette.hpp"
#include "JSystem/JUtility/JUTTexture.hpp"

JUTTexture::JUTTexture(int width, int height, GXTexFmt format) {
    mFlag = mFlag & 2 | 1;
    u32 bufSize = J3DNativeTexture::imageSize(format, width, height, 1);

    ResTIMG* texBuf = reinterpret_cast< ResTIMG* >(new (sizeof(ResTIMG)) u8[bufSize + sizeof(ResTIMG)]);
    _3C = texBuf;
    texBuf->mFormat = format;
    texBuf->mTransparency = 0;
    texBuf->mWidth = width;
    texBuf->mHeight = height;
    texBuf->mWrapS = GX_CLAMP;
    texBuf->mWrapT = GX_CLAMP;
    texBuf->mPaletteName = GX_TLUT0;
    texBuf->mPaletteFormat = GX_TL_IA8;
    texBuf->mPaletteNum = 0;
    texBuf->mPaletteDataOffset = 0;
    texBuf->mMipmap = false;
    texBuf->mDoEdgeLod = false;
    texBuf->mBiasClamp = false;
    texBuf->mMaxAnisotropy = GX_ANISO_1;
    texBuf->mMinType = GX_LINEAR;
    texBuf->mMagType = GX_LINEAR;
    texBuf->mMinLod = 0;
    texBuf->mMaxLod = 0;
    texBuf->mImageNum = 1;
    texBuf->mLodBias = 0;
    texBuf->mImageDataOffset = sizeof(ResTIMG);
    mEmbPalette = nullptr;
    storeTIMG(texBuf, static_cast< u8 >(0));
}

JUTTexture::~JUTTexture() {
    if (mFlag & 0x1) {
        delete[] _3C;
    }

    if (mFlag & 0x2) {
        delete mEmbPalette;
    }
}

void JUTTexture::storeTIMG(const ResTIMG* pTIMG, u8 a1) {
    if (pTIMG != nullptr && a1 < 0x10) {
        mTIMG = pTIMG;
        mImage = reinterpret_cast< u8* >(const_cast< ResTIMG* >(pTIMG)) + pTIMG->mImageDataOffset;
        if (pTIMG->mImageDataOffset == 0)
            mImage = reinterpret_cast< u8* >(const_cast< ResTIMG* >(pTIMG)) + sizeof(ResTIMG);

        mPalette = nullptr;
        mTlutName = 0;
        mWrapS = getTexInfo()->mWrapS;
        mWrapT = getTexInfo()->mWrapT;
        mMinType = getTexInfo()->mMinType;
        mMagType = getTexInfo()->mMagType;
        mMinLod = static_cast< s8 >(getTexInfo()->mMinLod);
        mMaxLod = static_cast< s8 >(getTexInfo()->mMaxLod);
        mLodBias = static_cast< s16 >(getTexInfo()->mLodBias);

        if (getTexInfo()->mPaletteNum == 0) {
            initTexObj();
            return;
        }

        s32 lut;
        if (getTexInfo()->mPaletteNum > 256) {
            lut = (a1 % 4) + GX_BIGTLUT0;
        } else {
            lut = a1;
        }

        if (mEmbPalette == nullptr || (mFlag & 2) == 0) {
            mEmbPalette = new JUTPalette(static_cast< GXTlut >(lut), static_cast< GXTlutFmt >(getTexInfo()->mPaletteFormat),
                                         static_cast< JUTTransparency >(getTexInfo()->mTransparency), getTexInfo()->mPaletteNum,
                                         reinterpret_cast< u8* >(const_cast< ResTIMG* >(getTexInfo())) + getTexInfo()->mPaletteDataOffset);
            setEmbPaletteDelFlag(true);
        } else {
            mEmbPalette->storeTLUT(static_cast< GXTlut >(lut), static_cast< GXTlutFmt >(getTexInfo()->mPaletteFormat),
                                   static_cast< JUTTransparency >(getTexInfo()->mTransparency), getTexInfo()->mPaletteNum,
                                   reinterpret_cast< u8* >(const_cast< ResTIMG* >(getTexInfo())) + getTexInfo()->mPaletteDataOffset);
        }
        attachPalette(mEmbPalette);
    }
}

void JUTTexture::storeTIMG(const ResTIMG* pTIMG, JUTPalette* pPalette) {
    GXTlut tlut;

    if (pPalette != nullptr) {
        tlut = static_cast< GXTlut >(pPalette->mName);
    } else {
        tlut = GX_TLUT0;
    }

    storeTIMG(pTIMG, pPalette, tlut);
}

void JUTTexture::storeTIMG(ResTIMG const* pTIMG, JUTPalette* pPalette, GXTlut a1) {
    if (pTIMG != nullptr) {
        mTIMG = pTIMG;
        mImage = reinterpret_cast< u8* >(const_cast< ResTIMG* >(pTIMG)) + pTIMG->mImageDataOffset;
        if (pTIMG->mImageDataOffset == 0)
            mImage = reinterpret_cast< u8* >(const_cast< ResTIMG* >(pTIMG)) + sizeof(ResTIMG);

        if ((mFlag & 2) != 0) {
            delete mEmbPalette;
        }
        mEmbPalette = pPalette;
        setEmbPaletteDelFlag(false);
        mPalette = nullptr;
        if (pPalette != nullptr) {
            mTlutName = a1;
            if (a1 != pPalette->mName) {
                pPalette->storeTLUT(a1, static_cast< GXTlutFmt >(pPalette->mFormat), static_cast< JUTTransparency >(pPalette->mTransparency),
                                    pPalette->mLutNum, pPalette->mColorTable);
            }
        }

        mWrapS = getTexInfo()->mWrapS;
        mWrapT = getTexInfo()->mWrapT;
        mMinType = getTexInfo()->mMinType;
        mMagType = getTexInfo()->mMagType;
        mMinLod = static_cast< s8 >(getTexInfo()->mMinLod);
        mMaxLod = static_cast< s8 >(getTexInfo()->mMaxLod);
        mLodBias = static_cast< s16 >(getTexInfo()->mLodBias);
        init();
    }
}

void JUTTexture::attachPalette(JUTPalette* pPalette) {
    if (mTIMG->mPaletteName == GX_TLUT0) {
        return;
    }

    if (pPalette == nullptr && mEmbPalette != nullptr) {
        mPalette = mEmbPalette;
    } else {
        mPalette = pPalette;
    }

    initTexObj(static_cast< GXTlut >(mPalette->mName));
}

void JUTTexture::init() {
    if (mTIMG->mPaletteNum == 0) {
        initTexObj();
    } else if (mEmbPalette != nullptr) {
        mPalette = mEmbPalette;

        initTexObj(static_cast< GXTlut >(mPalette->mName));
    }
}

void JUTTexture::initTexObj() {
    GXBool mipmapEnabled;
    if (mTIMG->mMipmap != 0) {
        mipmapEnabled = 1;
    } else {
        mipmapEnabled = 0;
    }
    u8* image = reinterpret_cast< u8* >(const_cast< ResTIMG* >(mTIMG));
    image += (mTIMG->mImageDataOffset ? (s32)mTIMG->mImageDataOffset : 0x20);
    // A texture with its own buffer (made at run time: a capture's or target's picture, or texels the game writes and
    // hands over itself) has nothing to hand over yet; a texture over loaded data hands its record over.
    if ((mFlag & 0x1) == 0) {
        J3DNativeTexture::registerImage(image, mTIMG->mFormat, mTIMG->mWidth, mTIMG->mHeight,
                                        mTIMG->mImageNum != 0 ? mTIMG->mImageNum : 1);
    }
    J3DNativeTexture::init(&mObj, image, mTIMG->mWidth, mTIMG->mHeight, static_cast< GXTexFmt >(mTIMG->mFormat), static_cast< GXTexWrapMode >(mWrapS),
                 static_cast< GXTexWrapMode >(mWrapT), mipmapEnabled);

    J3DNativeTexture::initLOD(&mObj, static_cast< GXTexFilter >(mMinType), static_cast< GXTexFilter >(mMagType), mMinLod / 8.0f, mMaxLod / 8.0f,
                    mLodBias / 100.0f, mTIMG->mBiasClamp, mTIMG->mDoEdgeLod, static_cast< GXAnisotropy >(mTIMG->mMaxAnisotropy));
}

void JUTTexture::initTexObj(GXTlut lut) {
    GXBool mipmapEnabled;
    if (mTIMG->mMipmap != 0) {
        mipmapEnabled = 1;
    } else {
        mipmapEnabled = 0;
    }
    mTlutName = lut;
    u8* image = reinterpret_cast< u8* >(const_cast< ResTIMG* >(mTIMG));
    image += (mTIMG->mImageDataOffset ? (s32)mTIMG->mImageDataOffset : 0x20);
    // A texture with its own buffer (made at run time: a capture's or target's picture, or texels the game writes and
    // hands over itself) has nothing to hand over yet; a texture over loaded data hands its record over.
    if ((mFlag & 0x1) == 0) {
        J3DNativeTexture::registerImage(image, mTIMG->mFormat, mTIMG->mWidth, mTIMG->mHeight,
                                        mTIMG->mImageNum != 0 ? mTIMG->mImageNum : 1);
    }
    J3DNativeTexture::initCI(&mObj, image, mTIMG->mWidth, mTIMG->mHeight, static_cast< GXCITexFmt >(mTIMG->mFormat), static_cast< GXTexWrapMode >(mWrapS),
                   static_cast< GXTexWrapMode >(mWrapT), mipmapEnabled, lut);

    J3DNativeTexture::initLOD(&mObj, static_cast< GXTexFilter >(mMinType), static_cast< GXTexFilter >(mMagType), mMinLod / 8.0f, mMaxLod / 8.0f,
                    mLodBias / 100.0f, mTIMG->mBiasClamp, mTIMG->mDoEdgeLod, static_cast< GXAnisotropy >(mTIMG->mMaxAnisotropy));
}

void JUTTexture::load(GXTexMapID texMapID) {
    J3DNativeTexture::load(&mObj, texMapID);
}

namespace {
    // A capture read as a texture of `format`: the picture's channels it reads, and what its intensity holds.
    NebulaTargetChannels captureChannels(GXTexFmt format, u32* pFlags) {
        switch (format) {
        case GX_TF_RGBA8:
            return NEBULA_TARGET_RGBA;
        case GX_TF_RGB565:
            return NEBULA_TARGET_RGB;
        case GX_TF_I8:
            *pFlags |= NEBULA_CAPTURE_LUMA;
            return NEBULA_TARGET_RED;
        case GX_TF_Z8:
            *pFlags |= NEBULA_CAPTURE_DEPTH;
            return NEBULA_TARGET_RED;
        case GX_CTF_A8:
            return NEBULA_TARGET_ALPHA;
        case GX_CTF_G8:
            return NEBULA_TARGET_GREEN;
        case GX_CTF_B8:
            return NEBULA_TARGET_BLUE;
        default:
            return NEBULA_TARGET_RED;
        }
    }

    void captureInto(void* pBuffer, int width, int height, int x, int y, bool half, GXTexFmt format, GXBool clear,
                     bool filtered) {
        u32 flags = (half ? NEBULA_CAPTURE_HALF : 0u) | (clear ? NEBULA_CAPTURE_CLEAR : 0u) |
                    (filtered ? NEBULA_CAPTURE_FILTERED : 0u);
        const NebulaTargetChannels channels = captureChannels(format, &flags);
        const int scale = half ? 2 : 1;
        J3DNativeTarget::capture(pBuffer, x, y, width * scale, height * scale, channels, flags);
    }
}  // namespace

void JUTTexture::capture(int x, int y, GXTexFmt format, bool half, u8 clear, bool filtered) {
    if (mFlag & 0x1) {
        captureInto(mImage, mTIMG->mWidth, mTIMG->mHeight, x, y, half, format, clear, filtered);
    }
}

void JUTTexture::captureDolTexture(void* pBuffer, int width, int height, int x, int y, bool mipmap, GXTexFmt format) {
    captureInto(pBuffer, width, height, x, y, mipmap, format, GX_FALSE, false);
}