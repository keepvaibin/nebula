#pragma once

#include <NebulaGpu.h>
#include <nebula_cooked_texture.h>
#include <nebula_math.h>

// Input edge for BTI and particle files; never a model's native texture API.
struct CookedResTIMG {
    u8 format, transparency;
    u16 width, height;
    u8 wrapS, wrapT;
    u64 reserved;
    u8 mipmap, edgeLod, biasClamp, anisotropy, minFilter, magFilter;
    s8 minLod, maxLod;
    u8 levels, pad;
    s16 bias;
    s32 imageOffset;
};
static_assert(sizeof(CookedResTIMG) == 32);

struct ResTIMG {
    u8 mFormat, mTransparency;
    u32 mWidth, mHeight;
    u8 mWrapS, mWrapT;
    const u8* mImage;
    bool mMipmap;
    u8 mMaxAnisotropy, mMinType, mMagType;
    f32 mMinLod, mMaxLod, mLodBias;
    u32 mImageNum;
    s32 mImageDataOffset;

    const u8* image() const {
        return mImage != nullptr ? mImage : reinterpret_cast< const u8* >(this) +
            (mImageDataOffset != 0 ? mImageDataOffset : sizeof(ResTIMG));
    }
    static ResTIMG fromNative(const NebulaTextureInfo& rSource) {
        if (rSource.magic != NEBULA_TEXTURE_INFO_MAGIC || rSource.version != NEBULA_TEXTURE_INFO_VERSION) {
            NEBULA_PANIC("a texture metadata file the importer did not write");
        }
        ResTIMG out{};
        out.mFormat = rSource.format; out.mTransparency = rSource.transparency;
        out.mWidth = rSource.width; out.mHeight = rSource.height;
        out.mWrapS = rSource.wrap_s; out.mWrapT = rSource.wrap_t;
        out.mImage = reinterpret_cast< const u8* >(&rSource) + rSource.image_at;
        out.mMipmap = rSource.mipmap != 0; out.mMaxAnisotropy = rSource.anisotropy;
        out.mMinType = rSource.min_filter; out.mMagType = rSource.mag_filter;
        out.mMinLod = rSource.min_lod; out.mMaxLod = rSource.max_lod;
        out.mLodBias = rSource.lod_bias; out.mImageNum = rSource.levels;
        return out;
    }
    static ResTIMG fromCooked(const CookedResTIMG& rSource) {
        ResTIMG out{};
        out.mFormat = rSource.format; out.mTransparency = rSource.transparency;
        out.mWidth = rSource.width; out.mHeight = rSource.height;
        out.mWrapS = rSource.wrapS; out.mWrapT = rSource.wrapT;
        out.mImage = reinterpret_cast< const u8* >(&rSource) + (rSource.imageOffset != 0 ? rSource.imageOffset : 32);
        out.mMipmap = rSource.mipmap != 0; out.mMaxAnisotropy = 1u << (rSource.anisotropy < 2u ? rSource.anisotropy : 2u);
        out.mMinType = rSource.minFilter; out.mMagType = rSource.magFilter;
        out.mMinLod = rSource.minLod * 0.125f; out.mMaxLod = rSource.maxLod * 0.125f;
        out.mLodBias = rSource.bias * 0.01f; out.mImageNum = rSource.levels;
        return out;
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
    static JUTTexture* createPixelTarget(u32 width, u32 height);

    /// @brief Creates a new `JUTTexture`.
    /// @param pTIMG The image.
    explicit JUTTexture(const ResTIMG* pTIMG) {
        // storeTIMG hands the record to the renderer unless the capture bit says the buffer is its own.
        mFlag = 0;
        storeTIMG(pTIMG);
    }

    /// @brief Destroys the `JUTTexture`.
    ~JUTTexture();

private:
    struct TargetOnly {};
    // Only createTarget's: a key whose id names a target, never an image.
    explicit JUTTexture(TargetOnly);
    void makeBuffer(int width, int height, u8 layout);

public:

    void storeTIMG(const ResTIMG*);
    void storeTIMG(const CookedResTIMG* pSource) {
        mSourceInfo = ResTIMG::fromCooked(*pSource);
        storeTIMG(&mSourceInfo);
    }
    void init();
    SiteTexture getTexObj() const {
        return mObj;
    }
    const SiteTexture* texObj() const {
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

    SiteTexture mObj;
    f32 mShareAcross = 1.0f;
    f32 mShareDown = 1.0f;
    const ResTIMG* mTIMG;
    u8* mImage;
    u8 mWrapS;
    u8 mWrapT;
    u8 mMinType;
    u8 mMagType;
    f32 mMinLod;
    f32 mMaxLod;
    f32 mLodBias;
    u8 mFlag;
    // The image and pixels makeBuffer allocated (freed with the texture; mFlag bit 0 says it is set).
    ResTIMG* mOwnedImage;
    ResTIMG mSourceInfo;
};
