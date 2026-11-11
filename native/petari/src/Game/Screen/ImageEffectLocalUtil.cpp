#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/NameObj/NameObjAdaptor.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Screen/BloomEffect.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_game_api.h>
#include <cstring>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setupDrawTexture_f1, "setupDrawTexture.f1", 10000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupDrawTexture_f2, "setupDrawTexture.f2", 10000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blurTexture_f1, "blurTexture.f1", 1.333f);
NEBULA_TUNABLE_NAMED(f32, cNum_blurTexture_f2, "blurTexture.f2", 160.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blurTexture_f3, "blurTexture.f3", 114.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blurTexture_f4, "blurTexture.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blurTexture_f5, "blurTexture.f5", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blurTexture_f6, "blurTexture.f6", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_blurTexture_f7, "blurTexture.f7", 6.0f);

extern "C" const NebulaHostServices* nebula_host(void);

void MR::connectToSceneImageEffect(NameObj* pObj) {
    MR::connectToScene(pObj, MR::MovementType_ImageEffect, MR::CalcAnimType_Environment, MR::DrawBufferType_None, MR::DrawType_ImageEffect);
}

void MR::connectToSceneImageEffectMovement(NameObj* pObj) {
    MR::connectToScene(pObj, MR::MovementType_ImageEffect, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}


namespace {
    // What the effects' draws write (setWrites).
    bool sWriteColor = true;
    bool sWriteAlpha = false;

    // The draws' mode (runtime/src/render/materials/image_effect.hlsl params.x) and the colours the fall-out passes give.
    u32 sMode = 0;
    NebulaColor8 sEdge = {0, 0, 0, 0};

    // runtime/src/render/materials/image_effect.hlsl for a screen tile: texture 0 at its own coordinates (moved by the
    // blur's offset) in the mode, the material colour the intensity (draw type 2: its alpha), blended by the draw type
    // (0 replace, 1 added, 2 by its alpha, 3 the frame times it), no depth or culling; the colour and alpha writes the
    // caller set.
    SiteMaterial drawMaterial(GameTexture* pTexture, u8 intensity, u32 type) {
        SiteMaterial material;
        static const SiteMaterial::Blend kBlends[4] = {SiteMaterial::BLEND_OPAQUE, SiteMaterial::BLEND_ADD,
                                                       SiteMaterial::BLEND_ALPHA, SiteMaterial::BLEND_OPAQUE};
        material.init(NEBULA_SHADER_IMAGE_EFFECT, kBlends[type], NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        if (type == ImageEffectLocalUtil::TexDrawType_3) {
            material.setBlend(NEBULA_FACTOR_DST_COLOR, NEBULA_FACTOR_ZERO);
        }
        if (type == ImageEffectLocalUtil::TexDrawType_2) {
            material.setColor(0, 255, 255, 255, intensity);
        } else {
            material.setColor(0, intensity, intensity, intensity, 255);
        }
        material.setColor(4, sEdge.r, sEdge.g, sEdge.b, sEdge.a);
        material.constants[8] = static_cast< f32 >(sMode);
        material.setTexture(0, pTexture);
        material.mState.color_write = sWriteColor;
        material.mState.alpha_write = sWriteAlpha;
        return material;
    }

    SiteMaterial gaussianMaterial(GameTexture* pTexture, f32 axisX, f32 axisY, f32 width, f32 alphaGain) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_GAUSSIAN_BLUR, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.constants[0] = axisX;
        material.constants[1] = axisY;
        material.constants[2] = width;
        material.constants[3] = alphaGain;
        material.setTexture(0, pTexture);
        material.mState.color_write = sWriteColor;
        material.mState.alpha_write = sWriteAlpha;
        return material;
    }
}  // namespace

void ImageEffectLocalUtil::setMode(EMode mode, const NebulaColor8& rEdge) {
    sMode = static_cast< u32 >(mode);
    sEdge = rEdge;
}

void ImageEffectLocalUtil::setupDrawTexture() {
    Mtx44 projectionMtx;
    NebulaMtxOrtho(projectionMtx, 0.0f, 1.0f, 0.0f, 1.0f, -cNum_setupDrawTexture_f1, cNum_setupDrawTexture_f2);
    Frame::setOrthographic(projectionMtx);

    MR::loadViewMtxFor2DModel();

    setWrites(true, false);
    sMode = 0;
}

void ImageEffectLocalUtil::setWrites(bool color, bool alpha) {
    sWriteColor = color;
    sWriteAlpha = alpha;
}

void ImageEffectLocalUtil::drawTexture(GameTexture* pTexture, u8 intensity, ETexDrawType texDrawType) {
    drawQuad(drawMaterial(pTexture, intensity, static_cast< u32 >(texDrawType) & 3u));
}

GameTexture* ImageEffectLocalUtil::createFrameKey() {
    GameTexture* pKey = GameTexture::createTarget(1.0f, 1.0f);
    pKey->setSampling(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP, NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
    return pKey;
}

void ImageEffectLocalUtil::drawFrameDepth(GameTexture* pTarget, GameTexture* pDepthKey) {
    setupDrawTexture();
    Frame::targetBeginOverFrame(pTarget->image(), pTarget->shareAcross(), pTarget->shareDown(), 0, nullptr, NEBULA_TARGET_RGBA,
                                    pDepthKey->image());
    SiteMaterial material;
    material.init(NEBULA_SHADER_FRAME_DEPTH, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                       NEBULA_CULL_NONE);
    material.setTexture(0, pDepthKey);
    material.mState.alpha_write = 0;
    drawQuad(material);
    Frame::targetEnd(pTarget->image(), NEBULA_TARGET_RED);
}

void ImageEffectLocalUtil::drawQuad(const SiteMaterial& rMaterial) {
    TypedDraw site = TypedDraw::eyeSpace(rMaterial, cIdentityMtx);
    sendTextureVertex(site);
}

void ImageEffectLocalUtil::sendTextureVertex(TypedDraw& rDraw) {
    VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
    *vtx++ = {0.0f, 1.0f, 0.0f, 0.0f, 1.0f};

    *vtx++ = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

    *vtx++ = {1.0f, 1.0f, 0.0f, 1.0f, 1.0f};

    *vtx++ = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f};
}

void ImageEffectLocalUtil::blurTexture(GameTexture* pSource, GameTexture* pScratch, GameTexture* pTarget, f32 radius,
                                       NebulaTargetChannels channels, f32 alphaGain) {
    // A ring of `radius` spreads radius^2 / 2 along each axis, and a tap's bilinear footprint at the picture this blur
    // was made for (160 x 114) adds texel^2 / 6: the same width at every resolution.
    const f32 verticalRadius = radius * (MR::isScreen16Per9() ? cNum_blurTexture_f1 : 1.0f);
    const f32 texelX = 1.0f / cNum_blurTexture_f2;
    const f32 texelY = 1.0f / cNum_blurTexture_f3;
    const f32 widthX = __builtin_sqrtf(radius * radius / cNum_blurTexture_f4 + texelX * texelX / cNum_blurTexture_f5);
    const f32 widthY = __builtin_sqrtf(verticalRadius * verticalRadius / cNum_blurTexture_f6 + texelY * texelY / cNum_blurTexture_f7);
    const bool alphaAlone = channels == NEBULA_TARGET_ALPHA;
    Frame::targetBegin(pScratch->image(), pScratch->shareAcross(), pScratch->shareDown(), 0);
    drawQuad(gaussianMaterial(pSource, 1.0f, 0.0f, widthX, alphaAlone ? 1.0f : 0.0f));
    Frame::targetEnd(pScratch->image(), channels);
    Frame::targetBegin(pTarget->image(), pTarget->shareAcross(), pTarget->shareDown(), 0);
    drawQuad(gaussianMaterial(pScratch, 0.0f, 1.0f, widthY, alphaAlone ? alphaGain : 0.0f));
    Frame::targetEnd(pTarget->image(), channels);
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
