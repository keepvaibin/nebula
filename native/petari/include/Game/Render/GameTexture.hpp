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

// A picture the game draws with: the renderer's texture for an image and how it is sampled. The image is a texture file's
// (set), one the game writes (made with a size: its own pixels, black to start) or a share of the frame the game draws into
// (a target: no pixels, its image is the key the frame passes name).
class GameTexture {
public:
    GameTexture() {}
    explicit GameTexture(const ResTIMG* pInfo) {
        set(pInfo);
    }
    // An image the game writes: `layout` is how (NEBULA_TEXTURE_LINEAR_*).
    GameTexture(u32 width, u32 height, u8 layout = NEBULA_TEXTURE_LINEAR_L8);
    ~GameTexture();
    GameTexture(const GameTexture&) = delete;
    GameTexture& operator=(const GameTexture&) = delete;

    // A target `across` x `down` of the frame (sized in NebulaFrameClock's effect texels), or one of a size in pixels.
    static GameTexture* createTarget(f32 across, f32 down);
    static GameTexture* createPixelTarget(u32 width, u32 height);

    // A file's image, sampled as the file says.
    void set(const ResTIMG* pInfo);
    void set(const CookedResTIMG* pSource) {
        mSourceInfo = ResTIMG::fromCooked(*pSource);
        set(&mSourceInfo);
    }
    // How it is sampled from now on (NEBULA_WRAP_*, NEBULA_FILTER_*).
    void setSampling(u8 wrapS, u8 wrapT, u8 minFilter, u8 magFilter) {
        mSite.mWrapS = wrapS;
        mSite.mWrapT = wrapT;
        mSite.mMinFilter = minFilter;
        mSite.mMagFilter = magFilter;
    }
    void setFilter(u8 minFilter, u8 magFilter) {
        mSite.mMinFilter = minFilter;
        mSite.mMagFilter = magFilter;
    }
    // Its image moved (a copy the game made of it).
    void setImage(u8* pImage) {
        mImage = pImage;
        mSite.mImage = pImage;
    }
    // The frame as drawn so far into an image the game writes.
    void copyFrame();

    const SiteTexture& site() const {
        return mSite;
    }
    const ResTIMG* info() const {
        return mInfo;
    }
    // The image: an image the game writes is its pixels; a target's is its key.
    u8* image() const {
        return mImage;
    }
    u32 getWidth() const {
        return mSite.mWidth;
    }
    u32 getHeight() const {
        return mSite.mHeight;
    }
    // A target's share of the frame (1 for any other).
    f32 shareAcross() const {
        return mShareAcross;
    }
    f32 shareDown() const {
        return mShareDown;
    }

private:
    void makeImage(u32 width, u32 height, u8 layout);

    SiteTexture mSite = {};
    const ResTIMG* mInfo = nullptr;
    u8* mImage = nullptr;
    // The record and pixels this texture made (freed with it).
    u8* mOwned = nullptr;
    f32 mShareAcross = 1.0f;
    f32 mShareDown = 1.0f;
    ResTIMG mSourceInfo = {};
};
