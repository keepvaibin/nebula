// Mii faces as a native material (runtime/src/render/materials/mii_face.hlsl): Mii's part draws (MiiModel.c) name
// each part's form, colour, culling and texture; the lit chain after it is Mii's own light (MiiLoadDrawSetting) or the
// stages the game adds for its Mii NPCs (Game/NPC/MiiFacePartsHolder.cpp). The blending and depth stay the drawing
// state's (the callers set them).
#include "Game/Util/NativeShader.hpp"
#include "MiiNative.h"

namespace {
    // The chain the next parts draw with (Mii_NativeChain).
    u32 sChain = 0;
    u32 sLightMask = 0;
    u32 sDiffuse = 0;
    u32 sAtten = 0;
    u32 sAlphaTest = 0;
    NebulaColor8 sAmbient = {0, 0, 0, 0};
    NebulaColor8 sMaterial = {255, 255, 255, 255};
    // The face texture's form and colours (Mii_NativeTextureForm).
    u32 sTextureForm = 0;
    NebulaColor8 sTextureC0 = {0, 0, 0, 0};
    NebulaColor8 sTextureC1 = {0, 0, 0, 0};
}  // namespace

extern "C" void Mii_NativeTextureForm(u32 form, NebulaColor8 c0, NebulaColor8 c1) {
    sTextureForm = form;
    sTextureC0 = c0;
    sTextureC1 = c1;
}

extern "C" void Mii_NativeTexturePart(const J3DNativeTexObj* pTexture) {
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_MII_TEXTURE, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                       NEBULA_CULL_NONE);
    material.blend_op = NEBULA_NATIVE_KEPT;
    material.depth_test = NEBULA_NATIVE_KEPT;
    material.cull = NEBULA_NATIVE_KEPT;
    NativeShader::setColor(material, 0, sTextureC0.r, sTextureC0.g, sTextureC0.b, sTextureC0.a);
    NativeShader::setColor(material, 4, sTextureC1.r, sTextureC1.g, sTextureC1.b, sTextureC1.a);
    material.constants[8] = static_cast< f32 >(sTextureForm);
    NativeShader::setTexObj(material, 0, pTexture);
    NativeShader::send(material);
}

extern "C" void Mii_NativeChain(u32 chain, u32 lightMask, u8 diffuse, u8 atten, NebulaColor8 ambient,
                                NebulaColor8 material, u32 alphaTest) {
    sChain = chain;
    sLightMask = lightMask;
    // The light function's forms as the material shaders take them (j3d_light: attenuation 1 specular, 2 none
    // (directional), 3 spot; diffuse 0 under specular attenuation).
    sAtten = atten == J3D_AF_SPEC ? 1 : atten == J3D_AF_SPOT ? 3 : 2;
    sDiffuse = atten == J3D_AF_SPEC ? 0 : static_cast< u32 >(diffuse);
    sAmbient = ambient;
    sMaterial = material;
    sAlphaTest = alphaTest;
}

extern "C" void Mii_NativePart(u32 form, NebulaColor8 color, u8 cull, const J3DNativeTexObj* pTexture) {
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_MII_FACE, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                       cull == J3D_CULL_FRONT  ? NEBULA_CULL_FRONT
                       : cull == J3D_CULL_BACK ? NEBULA_CULL_BACK
                                              : NEBULA_CULL_NONE);
    material.blend_op = NEBULA_NATIVE_KEPT;
    material.depth_test = NEBULA_NATIVE_KEPT;
    NativeShader::setColor(material, 0, color.r, color.g, color.b, color.a);
    material.constants[4] = static_cast< f32 >(form);
    material.constants[5] = static_cast< f32 >(sChain);
    material.constants[6] = static_cast< f32 >(sAlphaTest);
    NativeShader::setColor(material, 8, sMaterial.r, sMaterial.g, sMaterial.b, sMaterial.a);
    NativeShader::setColor(material, 12, sAmbient.r, sAmbient.g, sAmbient.b, sAmbient.a);
    material.constants[16] = static_cast< f32 >(sLightMask);
    material.constants[17] = static_cast< f32 >(sDiffuse);
    material.constants[18] = static_cast< f32 >(sAtten);
    if (pTexture != NULL) {
        NativeShader::setTexObj(material, 0, pTexture);
    }
    NativeShader::send(material);
}
