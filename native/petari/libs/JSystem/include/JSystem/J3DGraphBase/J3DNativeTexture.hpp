#pragma once

#include <nebula_cooked_texture.h>
#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <nebula_math.h>
#include <revolution/os.h>

// A texture as the game sets it up (GpuTexObj's values, held in a GpuTexObj): GpuInitTexObj*'s settings, loaded as state
// records (nebula_game_api.h NEBULA_STATE_TEX_LOOKUP, _TEX_IMAGE: the image names its texture record).
struct J3DNativeTexObj {
    const void* mImage;
    u16 mWidth;
    u16 mHeight;
    u8 mFormat;
    u8 mWrapS;
    u8 mWrapT;
    u8 mMinFilter;  // GpuTexFilter
    u8 mMagFilter;
    u8 mMipmap;
    u8 mPalette;    // a colour-indexed texture: mTlutName names its palette
    u8 mBiasClamp;
    u8 mEdgeLod;
    u8 mMaxAniso;
    f32 mMinLod;
    f32 mMaxLod;
    f32 mLodBias;
    u32 mTlutName;
};

// A palette as the game sets it up (GpuTlutObj's values; its address as bytes, to fit GpuTlutObj's three words).
struct J3DNativeTlutObj {
    u8 mLut[sizeof(void*)];
    u16 mEntries;
    u8 mFormat;
};

#ifdef __cplusplus
namespace J3DNativeTexture {
    // GpuInitTexObj*: the SDK's defaults (a mipmap's filters and LOD range), its clamps.
    void init(GpuTexObj* pObj, void* pImage, u16 width, u16 height, GpuTexFmt format, GpuTexWrapMode wrapS,
              GpuTexWrapMode wrapT, GpuBool mipmap);
    void initCI(GpuTexObj* pObj, void* pImage, u16 width, u16 height, GpuCITexFmt format, GpuTexWrapMode wrapS,
                GpuTexWrapMode wrapT, GpuBool mipmap, u32 tlutName);
    void initLOD(GpuTexObj* pObj, GpuTexFilter minFilter, GpuTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias,
                 GpuBool biasClamp, GpuBool edgeLod, GpuAnisotropy maxAniso);
    // The texture bound to `map` (a colour-indexed one with its palette's format and place).
    void load(const GpuTexObj* pObj, GpuTexMapID map);
    // GpuGetTexObj*'s read-backs.
    void getAll(const GpuTexObj* pObj, void** ppImage, u16* pWidth, u16* pHeight, GpuTexFmt* pFormat,
                GpuTexWrapMode* pWrapS, GpuTexWrapMode* pWrapT, GpuBool* pMipmap);
    void getLODAll(const GpuTexObj* pObj, GpuTexFilter* pMinFilter, GpuTexFilter* pMagFilter, f32* pMinLod, f32* pMaxLod,
                   f32* pLodBias, GpuBool* pBiasClamp, GpuBool* pEdgeLod, GpuAnisotropy* pMaxAniso);
    u32 getTlut(const GpuTexObj* pObj);
    // GpuInitTexObjTlut: the palette place a colour-indexed texture reads.
    void setTlut(GpuTexObj* pObj, u32 tlutName);
    // An image's bytes handed to the renderer (nebula_game_api.h texture_image): where it is loaded or rewritten.
    // `format`: GX's numbering of texture formats; `levels`: its mipmap images (1 without mipmaps). An image that is a
    // texture record (nebula_cooked_texture.h: a cooked file's, or one the game made) goes whole, as its header says.
    void registerImage(const void* pImage, u8 format, u16 width, u16 height, u8 levels);
    // The bytes of an image of that format and size with that many levels, as GX lays them out (32-byte tiles).
    u32 imageSize(u8 format, u16 width, u16 height, u8 levels);
    // GpuInitTlutObj. The renderer takes no palettes: colour-indexed images are cooked with theirs applied.
    void initTlut(GpuTlutObj* pObj, void* pLut, GpuTlutFmt format, u16 entries);
};  // namespace J3DNativeTexture

extern "C" {
#endif
// The same for the C sources (RVLFaceLib).
void J3DNativeTextureInit(GpuTexObj* pObj, void* pImage, u16 width, u16 height, GpuTexFmt format, GpuTexWrapMode wrapS,
                          GpuTexWrapMode wrapT, GpuBool mipmap);
void J3DNativeTextureInitLOD(GpuTexObj* pObj, GpuTexFilter minFilter, GpuTexFilter magFilter, f32 minLod, f32 maxLod,
                             f32 lodBias, GpuBool biasClamp, GpuBool edgeLod, GpuAnisotropy maxAniso);
void J3DNativeTextureLoad(const GpuTexObj* pObj, GpuTexMapID map);
void J3DNativeTextureRegisterImage(const void* pImage, u8 format, u16 width, u16 height, u8 levels);
#ifdef __cplusplus
}
#endif
