#pragma once

#include <NebulaGpu.h>
#include <nebula_cooked_texture.h>
#include <nebula_math.h>

// In host order, as the importer cooks model TEX1 blocks, particle textures and BTI files (native_data.rs host_timg).
struct ResTIMG {
    u8 mFormat;
    u8 mTransparency;
    u16 mWidth;
    u16 mHeight;
    u8 mWrapS;
    u8 mWrapT;
    const u8* mImage;  // null in a file; J3DTexture::setResTIMG's other image, anywhere in memory
    bool mMipmap;
    bool mDoEdgeLod;
    bool mBiasClamp;
    u8 mMaxAnisotropy;
    u8 mMinType;
    u8 mMagType;
    u8 mMinLod;
    u8 mMaxLod;
    u8 mImageNum;
    u8 _19;
    s16 mLodBias;
    s32 mImageDataOffset;  // from this header; 0 means right after it

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
    /// @param layout How the game writes it (NEBULA_TEXTURE_LINEAR_*); a capture's buffer is only its key.
    JUTTexture(int width, int height, u8 layout = NEBULA_TEXTURE_LINEAR_L8);

    /// @brief A texture a target pass draws: `across` x `down` of the frame. It holds no pixels of its own; its size says
    /// what effects' texel offsets were authored for, a whole frame being this many texels.
    static constexpr f32 cAuthoredAcross = 640.0f;
    static constexpr f32 cAuthoredDown = 456.0f;
    static JUTTexture* createTarget(f32 across, f32 down);

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
    // The frame as drawn so far as this texture: its colour, alpha or depth; half: each texel averages 2x2.
    void frameColor();

    const ResTIMG* getTexInfo() const {
        return mTIMG;
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
    f32 mShareAcross = 1.0f;
    f32 mShareDown = 1.0f;
    const ResTIMG* mTIMG;
    u8* mImage;
    u8 mWrapS;
    u8 mWrapT;
    u8 mMinType;
    u8 mMagType;
    u16 mMinLod;
    u16 mMaxLod;
    s16 mLodBias;
    u8 mFlag;
    ResTIMG* _3C;
};
