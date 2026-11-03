#pragma once

// Mii faces as a native material (src/MiiLib/MiiNative.cpp, runtime/src/render/materials/mii_face.hlsl).

#include <NebulaGpuTypes.h>

#ifdef __cplusplus
extern "C" {
#endif

// The part forms (mii_face.hlsl params.x).
enum {
    MII_I_NATIVE_FORM_COLOR = 0,     // the constant colour: beard, nose, forehead, hair
    MII_I_NATIVE_FORM_CAP = 1,       // the cap: (K + K tex) / 2
    MII_I_NATIVE_FORM_FACE = 2,      // the face texture over the faceline colour (swap R, A, B, G)
    MII_I_NATIVE_FORM_MASK = 3,      // the mask texture
    MII_I_NATIVE_FORM_NOSELINE = 4,  // the nose line: black at the texture's alpha
    MII_I_NATIVE_FORM_GLASSES = 5    // the glasses: K tex at the texture's alpha
};

// The chains after a part's own form (mii_face.hlsl params.y).
enum {
    MII_I_NATIVE_CHAIN_NONE = 0,      // unlit (MiiLoadDrawSetting without light)
    MII_I_NATIVE_CHAIN_LIGHT = 1,     // Mii's light: the colour times the lit channel
    MII_I_NATIVE_CHAIN_NPC_OPA = 2,   // the game's Mii NPC stages, opaque pass
    MII_I_NATIVE_CHAIN_NPC_XLU = 3    // the game's Mii NPC stages, translucent pass
};

// The chain the next parts draw with: its lit channel's lights, diffuse and attenuation functions, ambient and material
// colours, and the alpha test (0 above 0, 1 not 255).
void Mii_NativeChain(u32 chain, u32 lightMask, u8 diffuse, u8 atten, NebulaColor8 ambient, NebulaColor8 material,
                     u32 alphaTest);
// A part's material: its form, constant colour, culling and texture (NULL: none).
void Mii_NativePart(u32 form, NebulaColor8 color, u8 cull, const J3DNativeTexObj* pTexture);

// The face texture's parts (MiiMakeTex.c, runtime/src/render/materials/mii_texture.hlsl): the form and colours the
// next parts take (0 solid c0, 1 mouth, 2 eye), and a part's material with its texture at its draw.
void Mii_NativeTextureForm(u32 form, NebulaColor8 c0, NebulaColor8 c1);
void Mii_NativeTexturePart(const J3DNativeTexObj* pTexture);

#ifdef __cplusplus
}
#endif
