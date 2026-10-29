#pragma once

#include <nebula_game_api.h>
#include <revolution.h>

// A texture as the game sets it up (GXTexObj's values, held in a GXTexObj): GXInitTexObj*'s settings, loaded as state
// records (nebula_game_api.h NEBULA_STATE_TEX_LOOKUP, _TEX_IMAGE_ATTR, _TEX_IMAGE, _TEX_TLUT).
struct J3DNativeTexObj {
    const void* mImage;
    u16 mWidth;
    u16 mHeight;
    u8 mFormat;
    u8 mWrapS;
    u8 mWrapT;
    u8 mMinFilter;  // GXTexFilter
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

// A palette as the game sets it up (GXTlutObj's values; its address as bytes, to fit GXTlutObj's three words).
struct J3DNativeTlutObj {
    u8 mLut[sizeof(void*)];
    u16 mEntries;
    u8 mFormat;
};

#ifdef __cplusplus
namespace J3DNativeTexture {
    // GXInitTexObj*: the SDK's defaults (a mipmap's filters and LOD range), its clamps.
    void init(GXTexObj* pObj, void* pImage, u16 width, u16 height, GXTexFmt format, GXTexWrapMode wrapS,
              GXTexWrapMode wrapT, GXBool mipmap);
    void initCI(GXTexObj* pObj, void* pImage, u16 width, u16 height, GXCITexFmt format, GXTexWrapMode wrapS,
                GXTexWrapMode wrapT, GXBool mipmap, u32 tlutName);
    void initLOD(GXTexObj* pObj, GXTexFilter minFilter, GXTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias,
                 GXBool biasClamp, GXBool edgeLod, GXAnisotropy maxAniso);
    // The texture bound to `map` (a colour-indexed one with its palette's format and place).
    void load(const GXTexObj* pObj, GXTexMapID map);
    // GXGetTexObj*'s read-backs.
    void getAll(const GXTexObj* pObj, void** ppImage, u16* pWidth, u16* pHeight, GXTexFmt* pFormat,
                GXTexWrapMode* pWrapS, GXTexWrapMode* pWrapT, GXBool* pMipmap);
    void getLODAll(const GXTexObj* pObj, GXTexFilter* pMinFilter, GXTexFilter* pMagFilter, f32* pMinLod, f32* pMaxLod,
                   f32* pLodBias, GXBool* pBiasClamp, GXBool* pEdgeLod, GXAnisotropy* pMaxAniso);
    u32 getTlut(const GXTexObj* pObj);
    // GXInitTexObjTlut: the palette place a colour-indexed texture reads.
    void setTlut(GXTexObj* pObj, u32 tlutName);
    // An image's bytes handed to the renderer (nebula_game_api.h texture_image): where it is loaded or rewritten.
    // `format`: GX's numbering of texture formats; `levels`: its mipmap images (1 without mipmaps).
    void registerImage(const void* pImage, u8 format, u16 width, u16 height, u8 levels);
    // A palette's colours handed to the renderer (`entries` 16-bit colours).
    void registerPalette(const void* pPalette, u16 entries);
    // The bytes of an image of that format and size with that many levels, as GX lays them out (32-byte tiles).
    u32 imageSize(u8 format, u16 width, u16 height, u8 levels);
    // Palettes: GXInitTlutObj, and GXLoadTlut into one of GX's 20 palette places.
    void initTlut(GXTlutObj* pObj, void* pLut, GXTlutFmt format, u16 entries);
    void loadTlut(const GXTlutObj* pObj, u32 tlutName);
};  // namespace J3DNativeTexture

extern "C" {
#endif
// The same for the C sources (RVLFaceLib).
void J3DNativeTextureInit(GXTexObj* pObj, void* pImage, u16 width, u16 height, GXTexFmt format, GXTexWrapMode wrapS,
                          GXTexWrapMode wrapT, GXBool mipmap);
void J3DNativeTextureInitLOD(GXTexObj* pObj, GXTexFilter minFilter, GXTexFilter magFilter, f32 minLod, f32 maxLod,
                             f32 lodBias, GXBool biasClamp, GXBool edgeLod, GXAnisotropy maxAniso);
void J3DNativeTextureLoad(const GXTexObj* pObj, GXTexMapID map);
void J3DNativeTextureRegisterImage(const void* pImage, u8 format, u16 width, u16 height, u8 levels);
#ifdef __cplusplus
}
#endif
