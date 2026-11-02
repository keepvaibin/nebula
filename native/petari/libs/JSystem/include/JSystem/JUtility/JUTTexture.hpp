#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>

// In host order, as the importer cooks model TEX1 blocks, particle textures and BTI files (native_data.rs host_timg).
struct ResTIMG {
    /* 0x00 */ u8 mFormat;
    /* 0x01 */ u8 mTransparency;
    /* 0x02 */ u16 mWidth;
    /* 0x04 */ u16 mHeight;
    /* 0x06 */ u8 mWrapS;
    /* 0x07 */ u8 mWrapT;
    /* 0x08 */ const u8* mImage;  // null in a file; J3DTexture::setResTIMG's other image, anywhere in memory
    /* 0x10 */ bool mMipmap;
    /* 0x11 */ bool mDoEdgeLod;
    /* 0x12 */ bool mBiasClamp;
    /* 0x13 */ u8 mMaxAnisotropy;
    /* 0x14 */ u8 mMinType;
    /* 0x15 */ u8 mMagType;
    /* 0x16 */ u8 mMinLod;
    /* 0x17 */ u8 mMaxLod;
    /* 0x18 */ u8 mImageNum;
    /* 0x19 */ u8 _19;
    /* 0x1A */ s16 mLodBias;
    /* 0x1C */ s32 mImageDataOffset;  // from this header; 0 means right after it

    const u8* image() const {
        if (mImage != nullptr) {
            return mImage;
        }
        return reinterpret_cast< const u8* >(this) + (mImageDataOffset != 0 ? mImageDataOffset : 0x20);
    }
};

class JUTTexture {
public:
    /// @brief Creates a new `JUTTexture`.
    JUTTexture() {
        mFlag = 0;
        mTIMG = NULL;
    }

    /// @brief Creates a new `JUTTexture`.
    /// @param width The width of the texture, in pixels.
    /// @param height The height of the texture, in pixels.
    /// @param format The type of the texture.
    JUTTexture(int width, int height, GpuTexFmt format);

    /// @brief Creates a new `JUTTexture`.
    /// @param pTIMG The image.
    explicit JUTTexture(const ResTIMG* pTIMG) {
        // storeTIMG hands the record to the renderer unless the capture bit says the buffer is its own.
        mFlag = 0;
        storeTIMG(pTIMG);
    }

    /// @brief Destroys the `JUTTexture`.
    ~JUTTexture();

    void storeTIMG(const ResTIMG*);
    void init();
    J3DNativeTexObj getTexObj() const {
        return mObj;
    }
    const J3DNativeTexObj* texObj() const {
        return &mObj;
    }

    void initTexObj();
    // The frame's picture at x, y as this texture (half size, cleared afterwards, through the display's vertical
    // filter: J3DNativeTarget::capture's flags).
    void capture(int x, int y, GpuTexFmt format, bool half, u8 clear);

    const ResTIMG* getTexInfo() const {
        return mTIMG;
    }
    s32 getFormat() const {
        return mTIMG->mFormat;
    }
    s32 getTransparency() const {
        return mTIMG->mTransparency;
    }
    s32 getWidth() const {
        return mTIMG->mWidth;
    }
    s32 getHeight() const {
        return mTIMG->mHeight;
    }
    void setCaptureFlag(bool flag) {
        mFlag = flag;
    }
    bool getCaptureFlag() const {
        return mFlag & 1;
    }

    J3DNativeTexObj mObj;
    /* 0x20 */ const ResTIMG* mTIMG;
    /* 0x24 */ u8* mImage;
    /* 0x30 */ u8 mWrapS;
    /* 0x31 */ u8 mWrapT;
    /* 0x32 */ u8 mMinType;
    /* 0x33 */ u8 mMagType;
    /* 0x34 */ u16 mMinLod;
    /* 0x36 */ u16 mMaxLod;
    /* 0x38 */ s16 mLodBias;
    /* 0x3B */ u8 mFlag;
    /* 0x3C */ ResTIMG* _3C;
};
