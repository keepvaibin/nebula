#pragma once

#include <nebula_cooked_texture.h>
#include <nebula_game_api.h>
#include <NebulaGpu.h>
#include <nebula_math.h>

#ifdef __cplusplus
namespace J3DNativeTexture {
    // `pImage` at its size: linear filters, no levels.
    void init(J3DNativeTexObj* pObj, const void* pImage, u16 width, u16 height, u8 wrapS = NEBULA_WRAP_CLAMP,
              u8 wrapT = NEBULA_WRAP_CLAMP);
    void setFilter(J3DNativeTexObj* pObj, u8 minFilter, u8 magFilter, f32 minLod, f32 maxLod, f32 lodBias, bool edgeLod,
                   u8 maxAniso);
    // A texture record (nebula_cooked_texture.h: a cooked file's or one the game made) to the renderer, whole. Anything
    // else is not a texture the renderer draws from: a capture's picture is kept under its key.
    void registerRecord(const void* pImage);
    // Texels the game wrote, in rows (NEBULA_TEXTURE_LINEAR_*).
    void registerLinear(const void* pImage, u8 format, u16 width, u16 height, u8 levels);
    u32 linearSize(u8 format, u16 width, u16 height, u8 levels);
};  // namespace J3DNativeTexture

extern "C" {
#endif
// The same for the C sources (MiiLib).
void J3DNativeTextureInit(J3DNativeTexObj* pObj, const void* pImage, u16 width, u16 height, u8 wrapS, u8 wrapT);
void J3DNativeTextureSetFilter(J3DNativeTexObj* pObj, u8 minFilter, u8 magFilter, f32 minLod, f32 maxLod, f32 lodBias,
                               int edgeLod, u8 maxAniso);
void J3DNativeTextureRegisterRecord(const void* pImage);
#ifdef __cplusplus
}
#endif
