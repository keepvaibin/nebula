#pragma once

// Mii faces as a native material (src/RVLFaceLib/RFL_Native.cpp, runtime/src/render/materials/mii_face.hlsl).

#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXStruct.h>
#include <revolution/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// The part forms (mii_face.hlsl params.x).
enum {
    RFLi_NATIVE_FORM_COLOR = 0,     // the constant colour: beard, nose, forehead, hair
    RFLi_NATIVE_FORM_CAP = 1,       // the cap: (K + K tex) / 2
    RFLi_NATIVE_FORM_FACE = 2,      // the face texture over the faceline colour (swap R, A, B, G)
    RFLi_NATIVE_FORM_MASK = 3,      // the mask texture
    RFLi_NATIVE_FORM_NOSELINE = 4,  // the nose line: black at the texture's alpha
    RFLi_NATIVE_FORM_GLASSES = 5    // the glasses: K tex at the texture's alpha
};

// The chains after a part's own form (mii_face.hlsl params.y).
enum {
    RFLi_NATIVE_CHAIN_NONE = 0,      // unlit (RFLLoadDrawSetting without light)
    RFLi_NATIVE_CHAIN_LIGHT = 1,     // RFL's light: the colour times the lit channel
    RFLi_NATIVE_CHAIN_NPC_OPA = 2,   // the game's Mii NPC stages, opaque pass
    RFLi_NATIVE_CHAIN_NPC_XLU = 3    // the game's Mii NPC stages, translucent pass
};

// The chain the next parts draw with: its lit channel's lights, diffuse and attenuation functions, ambient and material
// colours, and the alpha test (0 above 0, 1 not 255).
void RFLiNativeChain(u32 chain, u32 lightMask, GXDiffuseFn diffuse, GXAttnFn atten, GXColor ambient, GXColor material,
                     u32 alphaTest);
// A part's material: its form, constant colour, culling and texture (NULL: none).
void RFLiNativePart(u32 form, GXColor color, GXCullMode cull, const GXTexObj* pTexture);

// The face texture's parts (RFL_MakeTex.c, runtime/src/render/materials/mii_texture.hlsl): the form and colours the
// next parts take (0 solid c0, 1 mouth, 2 eye), and a part's material with its texture at its draw.
void RFLiNativeTextureForm(u32 form, GXColor c0, GXColor c1);
void RFLiNativeTexturePart(const GXTexObj* pTexture);

#ifdef __cplusplus
}
#endif
