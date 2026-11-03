#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
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

void MR::connectToSceneImageEffect(NameObj* pObj) {
    MR::connectToScene(pObj, MR::MovementType_ImageEffect, MR::CalcAnimType_Environment, MR::DrawBufferType_None, MR::DrawType_ImageEffect);
}

void MR::connectToSceneImageEffectMovement(NameObj* pObj) {
    MR::connectToScene(pObj, MR::MovementType_ImageEffect, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
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
        NativeShader::setTexture(material, 0, pTexture);
        NativeShader::send(material);
    }

    void sendGaussianMaterial(JUTTexture* pTexture, f32 axisX, f32 axisY, f32 width, f32 alphaGain) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_GAUSSIAN_BLUR, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.constants[0] = axisX;
        material.constants[1] = axisY;
        material.constants[2] = width;
        material.constants[3] = alphaGain;
        NativeShader::setTexture(material, 0, pTexture);
        NativeShader::send(material);
    }
}  // namespace

void ImageEffectLocalUtil::setMode(EMode mode, const NebulaColor8& rKonst, const NebulaColor8& rEdge) {
    sMode = static_cast< u32 >(mode);
    sKonst = rKonst;
    sEdge = rEdge;
}

void ImageEffectLocalUtil::setupDrawTexture() {
    Mtx44 projectionMtx;
    NebulaMtxOrtho(projectionMtx, 0.0f, MR::getScreenUnitsDown(), 0.0f, MR::getScreenUnitsAcross(), -10000.0f, 10000.0f);
    J3DNativeView::setOrthographic(projectionMtx);

    MR::loadViewMtxFor2DModel();

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::setPosition(0, posMtx.toMtxPtr());
    J3DPass::positionMatrix(0);

    J3DPass::cull(NEBULA_CULL_NONE);
    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
    J3DPass::depth(false, NEBULA_COMPARE_ALWAYS, false);
    J3DPass::noAlphaTest();
    sMode = 0;
}

void ImageEffectLocalUtil::drawTexture(JUTTexture* pTexture, s32 divisions, s32 tile, u8 intensity, ETexDrawType texDrawType) {
    sendDrawMaterial(pTexture, intensity, static_cast< u32 >(texDrawType) & 3u);
    sendTextureVertex(divisions, tile);
}

JUTTexture* ImageEffectLocalUtil::createFrameKey() {
    JUTTexture* pKey = new JUTTexture(4, 4);
    pKey->mWrapS = NEBULA_WRAP_CLAMP;
    pKey->mWrapT = NEBULA_WRAP_CLAMP;
    pKey->mMinType = NEBULA_FILTER_LINEAR;
    pKey->mMagType = NEBULA_FILTER_LINEAR;
    return pKey;
}

void ImageEffectLocalUtil::drawFrameDepth(JUTTexture* pTarget, JUTTexture* pDepthKey) {
    setupDrawTexture();
    J3DNativeTarget::beginOverFrame(pTarget->mImage, pTarget->getWidth(), pTarget->getHeight(), 0, nullptr, NEBULA_TARGET_RGBA,
                                    pDepthKey->mImage);
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_FRAME_DEPTH, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                       NEBULA_CULL_NONE);
    NativeShader::setTexture(material, 0, pDepthKey);
    NativeShader::send(material);
    sendTextureVertex(2, 0);
    J3DNativeTarget::end(pTarget->mImage, NEBULA_TARGET_RED);
}

void ImageEffectLocalUtil::sendTextureVertex(s32 divisions, s32 tile) {
    s32 row = tile / divisions;
    s32 column = tile % divisions;
    f32 width = static_cast< f32 >(MR::getScreenUnitsAcross()) / divisions;
    f32 height = static_cast< f32 >(MR::getScreenUnitsDown()) / divisions;
    f32 left = column * width;
    f32 right = (column + 1) * width;
    f32 bottom = (row + 1) * height;
    f32 top = row * height;

    J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4, NEBULA_SHAPE_TEXCOORD0);
    J3DNativeDraw::position(left, bottom, 0.0f);
    J3DNativeDraw::texCoord(0.0f, 1.0f);

    J3DNativeDraw::position(left, top, 0.0f);
    J3DNativeDraw::texCoord(0.0f, 0.0f);

    J3DNativeDraw::position(right, bottom, 0.0f);
    J3DNativeDraw::texCoord(1.0f, 1.0f);

    J3DNativeDraw::position(right, top, 0.0f);
    J3DNativeDraw::texCoord(1.0f, 0.0f);
}

void ImageEffectLocalUtil::blurTexture(JUTTexture* pSource, JUTTexture* pScratch, JUTTexture* pTarget, f32 radius,
                                       NebulaTargetChannels channels, f32 alphaGain) {
    // A ring of `radius` spreads radius^2 / 2 along each axis, and a tap's bilinear footprint at the picture this blur
    // was made for (160 x 114) adds texel^2 / 6: the same width at every resolution.
    const f32 verticalRadius = radius * (MR::isScreen16Per9() ? 1.333f : 1.0f);
    const f32 texelX = 1.0f / 160.0f;
    const f32 texelY = 1.0f / 114.0f;
    const f32 widthX = __builtin_sqrtf(radius * radius / 2.0f + texelX * texelX / 6.0f);
    const f32 widthY = __builtin_sqrtf(verticalRadius * verticalRadius / 2.0f + texelY * texelY / 6.0f);
    const bool alphaAlone = channels == NEBULA_TARGET_ALPHA;
    J3DNativeTarget::begin(pScratch->mImage, pScratch->getWidth(), pScratch->getHeight(), 0);
    sendGaussianMaterial(pSource, 1.0f, 0.0f, widthX, alphaAlone ? 1.0f : 0.0f);
    sendTextureVertex(4, 0);
    J3DNativeTarget::end(pScratch->mImage, channels);
    J3DNativeTarget::begin(pTarget->mImage, pTarget->getWidth(), pTarget->getHeight(), 0);
    sendGaussianMaterial(pScratch, 0.0f, 1.0f, widthY, alphaAlone ? alphaGain : 0.0f);
    sendTextureVertex(4, 0);
    J3DNativeTarget::end(pTarget->mImage, channels);
}

void MR::connectToSceneNormalBloom(BloomEffect* pBloomEffect) {
    MR::connectToScene(pBloomEffect, MR::MovementType_None, MR::CalcAnimType_Environment, MR::DrawBufferType_None, MR::DrawType_None);

    NameObjAdaptor* pPreDrawAdaptor = new NameObjAdaptor("BloomEffect::preDraw");

    pPreDrawAdaptor->connectToDraw(MR::Functor(pBloomEffect, &BloomEffect::preDraw));
    MR::connectToScene(pPreDrawAdaptor, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_BloomEffectPreDraw);

    NameObjAdaptor* pPostDrawAdaptor = new NameObjAdaptor("BloomEffect::postDraw");

    pPostDrawAdaptor->connectToDraw(MR::Functor(pBloomEffect, &BloomEffect::postDraw));
    MR::connectToScene(pPostDrawAdaptor, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_BloomEffectPostDraw);
}
