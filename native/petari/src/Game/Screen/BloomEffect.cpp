#include "Game/Screen/BloomEffect.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "JSystem/JUtility/JUTVideo.hpp"
#include "revolution/gx/GXEnum.h"
#include "revolution/gx/GXStruct.h"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>
#include "Game/Util/RenderSignals.hpp"
#include <revolution/os.h>
#include "Game/System/NebulaHost.hpp"

namespace {
    struct RadAndOfs {
        /* 0x00 */ f32 _0;
        /* 0x04 */ f32 _4;
    };

    static const RadAndOfs sL1RadAndOfs[] = {
        {0.01f, 0.0f},
        {0.02f, 0.52f},
    };
    static const RadAndOfs sL2RadAndOfs[] = {
        {0.04f, 0.0f},
        {0.07f, 0.0f},
        {0.09f, 0.0f},
    };
    static const u32 sL1RoundPoints = 6;

    bool sBlurAdditive = false;

    // The stage the chain's texture draws use: modulate (texture matrix 0, initDraw's), modulate after a blur (the
    // identity matrix), or the brightness threshold (red above the material colour's: konst alpha).
    enum BloomStage { BloomStage_Modulate, BloomStage_ModulateIdentity, BloomStage_Threshold };
    BloomStage sBloomStage = BloomStage_Modulate;
    // runtime/src/render/materials/image_effect.hlsl for a texture draw: modulate (the intensity) or the threshold
    // (white where the texture's red is above it), blended by `type`: 0 replaces, 1 adds, 2 multiplies the frame by
    // it; no depth or culling.
    void sendDrawMaterial(JUTTexture* pTexture, u8 intensity, u32 type) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_IMAGE_EFFECT,
                           type == 1 ? NativeShader::BLEND_ADD : NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        if (type == 2) {
            NativeShader::setBlend(material, NEBULA_FACTOR_DST_COLOR, NEBULA_FACTOR_ZERO);
        }
        NativeShader::setColor(material, 0, intensity, intensity, intensity, 255);
        material.constants[12] = sBloomStage == BloomStage_Threshold ? 1.0f : 0.0f;
        NativeShader::setTexture(material, 0, pTexture);
        NativeShader::send(material);
    }
    // The blur's texture and intensity (initBlur).
    JUTTexture* sBlurTexture = nullptr;
    u8 sBlurIntensity = 0;

    // runtime/src/render/materials/bloom_blur.hlsl: `count` samples of the blur's texture at the matrices' offsets
    // summed, replacing the tile the first time, added after.
    void sendBlurMaterial(u32 count, Mtx* pMtx) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_BLOOM_BLUR,
                           sBlurAdditive ? NativeShader::BLEND_ADD : NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS,
                           false, NEBULA_CULL_NONE);
        NativeShader::setColor(material, 0, sBlurIntensity, sBlurIntensity, sBlurIntensity, 255);
        for (u32 s = 0; s < count; s++) {
            material.constants[4 + s * 2] = pMtx[s][0][3];
            material.constants[4 + s * 2 + 1] = pMtx[s][1][3];
        }
        material.constants[20] = static_cast< f32 >(count);
        NativeShader::setTexture(material, 0, sBlurTexture);
        NativeShader::send(material);
    }
    static const u32 sL2RoundPoints = 12;
    // static const f32 sBloomIntensity = _;
    // static const f32 sMaskThreshold = _;
    // static const ... sBlurType = _;
    // static const s32 sFadeCounterMax = _;

    // How the bloom goes through the renderer (NEBULA_OPTION_BLOOM; galaxy/gx/game_bloom.h): decided at preDraw for
    // the frame's pair of passes, so a change in between never restores a scene that was not kept.
    u32 sBloomRoute;

    u32 floatBits(f32 value) {
        u32 bits;
        __builtin_memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
};  // namespace

BloomEffect::BloomEffect(const char* pName) : ImageEffectBase(pName), _14(255), _18(1.0f / ::sL1RoundPoints), _1C(1.0f / ::sL2RoundPoints), _20(128) {
    MR::connectToSceneNormalBloom(this);
    MR::createImageEffectSystemHolder();
    MR::getImageEffectResource()->createBloomTexture();

    _24 = MR::getImageEffectResource()->_0;
    _28 = MR::getImageEffectResource()->_4;
    _2C = MR::getImageEffectResource()->_C;
    _30 = MR::getImageEffectResource()->_10;
    _34 = MR::getImageEffectResource()->_14;
    _38 = MR::getImageEffectResource()->_18;
    _3C = _2C;
    _40 = MR::getImageEffectResource()->_1C;
    _44 = MR::getImageEffectResource()->_20;
    _48 = new Mtx[::sL1RoundPoints * ARRAY_SIZE(::sL1RadAndOfs)];
    _4C = new Mtx[::sL2RoundPoints * ARRAY_SIZE(::sL2RadAndOfs)];

    for (u32 i = 0; i < ARRAY_SIZE(::sL1RadAndOfs); i++) {
        initBlurMtx(&_48[i * ::sL1RoundPoints], ::sL1RoundPoints, ::sL1RadAndOfs[i]._0, ::sL1RadAndOfs[i]._4);
    }

    for (u32 i = 0; i < ARRAY_SIZE(::sL2RadAndOfs); i++) {
        initBlurMtx(&_4C[i * ::sL2RoundPoints], ::sL2RoundPoints, ::sL2RadAndOfs[i]._0, ::sL2RadAndOfs[i]._4);
    }
}

void BloomEffect::preDraw() const {
    if (!isSomething()) {
        return;
    }

    // The renderer keeps the scene in the scene target's own format: the console's copy below is 8 bits, which cuts the ray
    // tracer's HDR scene at 1.
    sBloomRoute = NebulaHost::option(NEBULA_OPTION_BLOOM);
    if (sBloomRoute != 0) {
        NebulaRender::bloomMarker(NebulaRender::kBloomKeep, 0, 0);
    } else
    {
        ImageEffectLocalUtil::capture(_24, 1, 0, GX_TF_RGBA8, false, 0);
    }

    MR::fillScreen(Color8(0, 0, 0, 0xFF).mGXColor);
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}

void BloomEffect::postDraw() const {
    if (!isSomething()) {
        return;
    }

    if (sBloomRoute == 2) {
        // The chain below, drawn by the renderer at the internal resolution: the threshold, both blurs' tap weights (as
        // initBlur loads them), the intensity and the rings' horizontal radius per unit; then the GX state the chain
        // leaves.
        const u8 intensity = _14 * get_10();
        const u32 tap1 = static_cast< u8 >(static_cast< s32 >(_18 * 255.0f));
        const u32 tap2 = static_cast< u8 >(static_cast< s32 >(_1C * 255.0f));
        NebulaRender::bloomMarker(NebulaRender::kBloomNormal, _20 | tap1 << 8 | tap2 << 16 | static_cast< u32 >(intensity) << 24,
                              floatBits(1.0f / MR::getAspect()));
        initDraw();
        J3DNativeState::setNumTexGens(1);
        J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        J3DNativeState::setNumTevStages(1);
        J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_MODULATE);
        _3C->load(GX_TEXMAP0);
        J3DNativeState::setChanMatColor(GX_COLOR0A0, Color8(0xFF, 0xFF, 0xFF, 0xFF));
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
        return;
    }

    ImageEffectLocalUtil::capture(_28, 1, 0, GX_TF_RGB565, true, 0);
    initDraw();
    drawTexture(_28, 4, 0, 0xFF, DrawType_0);

    ImageEffectLocalUtil::capture(_2C, 4, 0, GX_TF_RGB565, false, 0);
    ImageEffectLocalUtil::capture(_40, 4, 0, GX_TF_I8, false, 0);
    drawTexture(_2C, 4, 2, 0xFF, DrawType_0);

    sBloomStage = BloomStage_Threshold;
    drawTexture(_40, 4, 2, _20, DrawType_2);
    sBloomStage = BloomStage_Modulate;

    ImageEffectLocalUtil::capture(_44, 4, 2, GX_TF_RGB565, false, 0);
    blurTexture(_44, _48, 4, 3, 6, _18, 2, &::sL1RadAndOfs[0]._0);

    ImageEffectLocalUtil::capture(_30, 4, 3, GX_TF_RGB565, false, 0);
    ImageEffectLocalUtil::capture(_34, 4, 3, GX_TF_RGB565, true, 0);
    blurTexture(_34, _4C, 8, 20, 12, _1C, 3, &::sL2RadAndOfs[0]._0);

    ImageEffectLocalUtil::capture(_38, 8, 20, GX_TF_RGB565, false, 0);
    const u8 intensity = _14 * get_10();
    drawTexture(_30, 4, 7, intensity, DrawType_0);
    drawTexture(_38, 4, 7, intensity, DrawType_1);

    ImageEffectLocalUtil::capture(_3C, 4, 7, GX_TF_RGB565, false, 0);

    if (sBloomRoute != 0) {
        // The scene the renderer kept, drawn back as _24 would be (its RGB; the alpha update is off).
        NebulaRender::bloomMarker(NebulaRender::kBloomRestore, 0, 0);
    } else
    {
        drawTexture(_24, 1, 0, 0xFF, DrawType_0);
    }
    drawTexture(_3C, 1, 0, 0xFF, DrawType_1);
}

u8 BloomEffect::getIntensity1Default() const {
    return 42;
}

u8 BloomEffect::getIntensity2Default() const {
    return 21;
}

bool BloomEffect::isForEachModel() const {
    return true;
}

void BloomEffect::initDraw() const {
    MR::reinitGX();
    J3DNativeState::setZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);

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
    J3DNativeState::setDither(GX_FALSE);
    sBloomStage = BloomStage_Modulate;
}

void BloomEffect::drawTexture(JUTTexture* pTexture, s32 param2, s32 param3, u8 intensity, BLOOM_TEX_DRAW_TYPE param5) const {
    sendDrawMaterial(pTexture, intensity, static_cast< u32 >(param5) < 3 ? static_cast< u32 >(param5) : 0);

    s32 row = param3 / param2;
    s32 column = param3 % param2;
    f32 width = static_cast< f32 >(MR::getFrameBufferWidth()) / param2;
    f32 height = static_cast< f32 >(MR::getFrameBufferHeight()) / param2;
    f32 left = column * width;
    f32 right = (column + 1) * width;
    f32 bottom = (row + 1) * height;
    f32 top = row * height;

    J3DNativeDraw::begin(GX_TRIANGLESTRIP, 4, NEBULA_SHAPE_TEXCOORD0);
    {
        J3DNativeDraw::position(left, bottom, -1.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        J3DNativeDraw::position(left, top, -1.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(right, bottom, -1.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);

        J3DNativeDraw::position(right, top, -1.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);
    }
}

void BloomEffect::blurTexture(JUTTexture* pTexture, Mtx* pMtx, s32 param3, s32 param4, u32 param5, f32 param6, u32 param7, const f32* pParam8) const {
    initBlur(pTexture, 8, param6);

    for (s32 i = 0; i < param7; i++) {
        drawBlur(param3, param4, &pMtx[i * param5], 8, param5);
    }
    sBloomStage = BloomStage_ModulateIdentity;
}

void BloomEffect::initBlurMtx(Mtx* pMtx, u32 count, f32 radius, f32 offset) const {
    f32 invAspect = 1.0f / MR::getAspect();
    radius = -radius;

    for (u32 i = 0; i < count; i++) {
        NebulaMtxTrans(pMtx[i], invAspect * (radius * JMACosRadian(offset + i * JMath::TAngleConstant_< f32 >::RADIAN_DEG360() / count)),
                   radius * JMASinRadian(offset + i * JMath::TAngleConstant_< f32 >::RADIAN_DEG360() / count), 0.0f);
    }

}

// The blur's texture and intensity (drawBlur's materials, sendBlurMaterial).
void BloomEffect::initBlur(JUTTexture* pTexture, u32 param2, f32 intensity) const {
    (void)param2;
    sBlurIntensity = static_cast< u8 >(static_cast< s32 >(intensity * 255.0f));
    sBlurTexture = pTexture;
    sBlurAdditive = false;
}

void BloomEffect::drawBlur(s32 divisions, s32 tile, Mtx* pMtx, u32 stageCount, u32 matrixCount) const {
    s32 row = tile / divisions;
    s32 column = tile % divisions;
    f32 width = static_cast< f32 >(MR::getFrameBufferWidth()) / divisions;
    f32 height = static_cast< f32 >(MR::getFrameBufferHeight()) / divisions;
    f32 left = column * width;
    f32 right = (column + 1) * width;
    f32 bottom = (row + 1) * height;
    f32 top = row * height;

    for (u32 i = 0; i < matrixCount; i += stageCount) {
        u32 remaining = matrixCount - i;
        if (remaining < stageCount) {
            stageCount = remaining;
        }
        sendBlurMaterial(stageCount, &pMtx[i]);

        J3DNativeDraw::begin(GX_TRIANGLESTRIP, 4, NEBULA_SHAPE_TEXCOORD0);
        J3DNativeDraw::position(left, bottom, -1.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        J3DNativeDraw::position(left, top, -1.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(right, bottom, -1.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);

        J3DNativeDraw::position(right, top, -1.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);

        sBlurAdditive = true;
    }
}
