#pragma once

#include <nebula_cooked_texture.h>
#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <nebula_math.h>



#ifdef __cplusplus
namespace J3DNativeTexture {
    // GpuInitTexObj*: the SDK's defaults (a mipmap's filters and LOD range), its clamps.
    void init(J3DNativeTexObj* pObj, void* pImage, u16 width, u16 height, GpuTexFmt format, GpuTexWrapMode wrapS,
              GpuTexWrapMode wrapT, GpuBool mipmap);
    void initLOD(J3DNativeTexObj* pObj, GpuTexFilter minFilter, GpuTexFilter magFilter, f32 minLod, f32 maxLod, f32 lodBias,
                 GpuBool biasClamp, GpuBool edgeLod, GpuAnisotropy maxAniso);
    // The texture bound to `map`.
    // GpuGetTexObj*'s read-backs.
    void getAll(const J3DNativeTexObj* pObj, void** ppImage, u16* pWidth, u16* pHeight, GpuTexFmt* pFormat,
                GpuTexWrapMode* pWrapS, GpuTexWrapMode* pWrapT, GpuBool* pMipmap);
    void getLODAll(const J3DNativeTexObj* pObj, GpuTexFilter* pMinFilter, GpuTexFilter* pMagFilter, f32* pMinLod, f32* pMaxLod,
                   f32* pLodBias, GpuBool* pBiasClamp, GpuBool* pEdgeLod, GpuAnisotropy* pMaxAniso);
    // An image's bytes handed to the renderer (nebula_game_api.h texture_image): where it is loaded or rewritten.
    // `format`: the console's numbering of texture formats; `levels`: its mipmap images (1 without mipmaps). An image that is a
    // texture record (nebula_cooked_texture.h: a cooked file's, or one the game made) goes whole, as its header says.
    void registerImage(const void* pImage, u8 format, u16 width, u16 height, u8 levels);
    // The bytes of an image of that format and size with that many levels, as console lays them out (32-byte tiles).
    u32 imageSize(u8 format, u16 width, u16 height, u8 levels);
};  // namespace J3DNativeTexture

extern "C" {
#endif
// The same for the C sources (RVLFaceLib).
void J3DNativeTextureInit(J3DNativeTexObj* pObj, void* pImage, u16 width, u16 height, GpuTexFmt format, GpuTexWrapMode wrapS,
                          GpuTexWrapMode wrapT, GpuBool mipmap);
void J3DNativeTextureInitLOD(J3DNativeTexObj* pObj, GpuTexFilter minFilter, GpuTexFilter magFilter, f32 minLod, f32 maxLod,
                             f32 lodBias, GpuBool biasClamp, GpuBool edgeLod, GpuAnisotropy maxAniso);
void J3DNativeTextureRegisterImage(const void* pImage, u8 format, u16 width, u16 height, u8 levels);
#ifdef __cplusplus
}
#endif
