#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/NameObj/NameObjAdaptor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/BloomEffect.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The texture draws' coordinate offset (the blur's samples).
    f32 sTransX = 0.0f;
    f32 sTransY = 0.0f;

    void setTextureTrans(f32 x, f32 y) {
        sTransX = x;
        sTransY = y;
    }

};  // namespace

void MR::connectToSceneImageEffect(NameObj* pObj) {
    MR::connectToScene(pObj, MR::MovementType_ImageEffect, MR::CalcAnimType_Environment, MR::DrawBufferType_None, MR::DrawType_ImageEffect);
}

void MR::connectToSceneImageEffectMovement(NameObj* pObj) {
    MR::connectToScene(pObj, MR::MovementType_ImageEffect, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void ImageEffectLocalUtil::capture(JUTTexture* pTexture, s32 divisions, s32 tile, GXTexFmt format, bool half, u8 clear,
                                   bool filtered) {
    // FallOutFieldDraw::draw passes 0 divisions each frame it draws: the capture then starts at the origin.
    if (divisions == 0) {
        pTexture->capture(0, 0, format, half, clear, filtered);
        return;
    }
    pTexture->capture((tile % divisions) * MR::getFrameBufferWidth() / divisions, (tile / divisions) * MR::getFrameBufferHeight() / divisions, format,
                      half, clear, filtered);
}


namespace {
    // The draws' mode (runtime/src/render/materials/image_effect.hlsl params.x) and the colours the fall-out passes give.
    u32 sMode = 0;
    NebulaColor8 sKonst = {0, 0, 0, 0};
    NebulaColor8 sEdge = {0, 0, 0, 0};

    // runtime/src/render/materials/image_effect.hlsl for a screen tile: texture 0 at its own coordinates (moved by the
    // blur's offset) in the mode, the material colour the intensity (draw type 2: its alpha), blended by the draw type
    // (0 replace, 1 added, 2 by its alpha, 3 the frame times it), no depth or culling; the colour and alpha writes the
    // caller set.
    void sendDrawMaterial(JUTTexture* pTexture, u8 intensity, u32 type) {
        NebulaNativeMaterial material;
        static const NativeShader::Blend kBlends[4] = {NativeShader::BLEND_OPAQUE, NativeShader::BLEND_ADD,
                                                       NativeShader::BLEND_ALPHA, NativeShader::BLEND_OPAQUE};
        NativeShader::init(material, NEBULA_SHADER_IMAGE_EFFECT, kBlends[type], NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        if (type == ImageEffectLocalUtil::TexDrawType_3) {
            NativeShader::setBlend(material, NEBULA_FACTOR_DST_COLOR, NEBULA_FACTOR_ZERO);
        }
        if (type == ImageEffectLocalUtil::TexDrawType_2) {
            NativeShader::setColor(material, 0, 255, 255, 255, intensity);
        } else {
            NativeShader::setColor(material, 0, intensity, intensity, intensity, 255);
        }
        NativeShader::setColor(material, 4, sKonst.r, sKonst.g, sKonst.b, sKonst.a);
        NativeShader::setColor(material, 8, sEdge.r, sEdge.g, sEdge.b, sEdge.a);
        material.constants[12] = static_cast< f32 >(sMode);
        material.constants[14] = sTransX;
        material.constants[15] = sTransY;
        NativeShader::setTexture(material, 0, pTexture);
        NativeShader::send(material);
    }
}  // namespace

void ImageEffectLocalUtil::setStage(EStage stage) {
    sMode = stage == Stage_Threshold ? 1 : 0;
}

void ImageEffectLocalUtil::setMode(EMode mode, const NebulaColor8& rKonst, const NebulaColor8& rEdge) {
    sMode = static_cast< u32 >(mode);
    sKonst = rKonst;
    sEdge = rEdge;
}

void ImageEffectLocalUtil::setupDrawTexture() {
    Mtx44 projectionMtx;
    NebulaMtxOrtho(projectionMtx, 0.0f, MR::getFrameBufferHeight(), 0.0f, MR::getFrameBufferWidth(), -10000.0f, 10000.0f);
    J3DNativeView::setProjection(projectionMtx, GX_ORTHOGRAPHIC);

    MR::loadViewMtxFor2DModel();

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::setPosition(0, posMtx.toMtxPtr());
    J3DNativeState::setCurrentMtx(GX_PNMTX0);

    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setDither(GX_FALSE);
    sMode = 0;
}

void ImageEffectLocalUtil::drawTexture(JUTTexture* pTexture, s32 divisions, s32 tile, u8 intensity, ETexDrawType texDrawType) {
    sendDrawMaterial(pTexture, intensity, static_cast< u32 >(texDrawType) & 3u);
    sendTextureVertex(divisions, tile);
}

void ImageEffectLocalUtil::sendTextureVertex(s32 divisions, s32 tile) {
    s32 row = tile / divisions;
    s32 column = tile % divisions;
    f32 width = static_cast< f32 >(MR::getFrameBufferWidth()) / divisions;
    f32 height = static_cast< f32 >(MR::getFrameBufferHeight()) / divisions;
    f32 left = column * width;
    f32 right = (column + 1) * width;
    f32 bottom = (row + 1) * height;
    f32 top = row * height;

    J3DNativeDraw::begin(GX_TRIANGLESTRIP, 4, NEBULA_SHAPE_TEXCOORD0);
    J3DNativeDraw::position(left, bottom, 0.0f);
    J3DNativeDraw::texCoord(0.0f, 1.0f);

    J3DNativeDraw::position(left, top, 0.0f);
    J3DNativeDraw::texCoord(0.0f, 0.0f);

    J3DNativeDraw::position(right, bottom, 0.0f);
    J3DNativeDraw::texCoord(1.0f, 1.0f);

    J3DNativeDraw::position(right, top, 0.0f);
    J3DNativeDraw::texCoord(1.0f, 0.0f);
}

#pragma push
#pragma global_optimizer off
void ImageEffectLocalUtil::blurTexture(JUTTexture* pTexture, s32 divisions, s32 tile, u32 sampleCount, f32 radius, f32 intensity) {
    f32 aspect = MR::isScreen16Per9() ? 1.333f : 1.0f;
    f32 verticalRadius = radius * aspect;
    s32 sampleIntensity = 255.0f * intensity / sampleCount;

    for (u32 i = 0; i < sampleCount; i++) {
        f32 angle = 2.0f * (i * JGeometry::TUtil< f32 >::PI()) / sampleCount;
        ::setTextureTrans(radius * JMACosRadian(angle), verticalRadius * JMASinRadian(angle));
        ETexDrawType type;
        if (i == 0) {
            type = TexDrawType_0;
        } else {
            type = TexDrawType_1;
        }

        drawTexture(pTexture, divisions, tile, sampleIntensity, type);
    }

    ::setTextureTrans(0.0f, 0.0f);
}
#pragma pop

void MR::connectToSceneNormalBloom(BloomEffect* pBloomEffect) {
    MR::connectToScene(pBloomEffect, MR::MovementType_None, MR::CalcAnimType_Environment, MR::DrawBufferType_None, MR::DrawType_None);

    NameObjAdaptor* pPreDrawAdaptor = new NameObjAdaptor("BloomEffect::preDraw");

    pPreDrawAdaptor->connectToDraw(MR::Functor(pBloomEffect, &BloomEffect::preDraw));
    MR::connectToScene(pPreDrawAdaptor, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_BloomEffectPreDraw);

    NameObjAdaptor* pPostDrawAdaptor = new NameObjAdaptor("BloomEffect::postDraw");

    pPostDrawAdaptor->connectToDraw(MR::Functor(pBloomEffect, &BloomEffect::postDraw));
    MR::connectToScene(pPostDrawAdaptor, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_BloomEffectPostDraw);
}
