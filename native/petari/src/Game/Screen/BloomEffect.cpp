#include "Game/Screen/BloomEffect.hpp"
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
#ifdef TARGET_PC
#include <nebula_game_api.h>
#include <cstring>
#include "Game/Util/RenderSignals.hpp"
#include <revolution/os.h>
#include "Game/System/NebulaHost.hpp"
#endif

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
    static const GXTexMtx sTexMtxID[] = {
        GX_TEXMTX0, GX_TEXMTX1, GX_TEXMTX2, GX_TEXMTX3, GX_TEXMTX4, GX_TEXMTX5, GX_TEXMTX6, GX_TEXMTX7,
    };
    static const u32 sL1RoundPoints = 6;

    // The blur's programs (nebula_game_api.h NebulaMaterialProgram), by stage count (1-8) and blend: stage i adds the
    // texture through texture coordinate i (its own offset matrix) times the material colour; the first draw of a
    // blur replaces, the later ones add. Registered with the host when first used; the key is the entry.
    struct BlurProgram {
        NebulaMaterialProgram mProgram;
        bool mRegistered;
    };
    BlurProgram sBlurPrograms[8][2];
    bool sBlurAdditive = false;

    // The stage the chain's texture draws use: modulate (texture matrix 0, initDraw's), modulate after a blur (the
    // identity matrix), or the brightness threshold (red above the material colour's: konst alpha).
    enum BloomStage { BloomStage_Modulate, BloomStage_ModulateIdentity, BloomStage_Threshold };
    BloomStage sBloomStage = BloomStage_Modulate;
    struct DrawProgram {
        NebulaMaterialProgram mProgram;
        bool mRegistered;
    };
    DrawProgram sDrawPrograms[3][3];

    // A texture draw's program and values: `type` 0 replaces, 1 adds, 2 multiplies by the source colour.
    void sendDrawProgram(JUTTexture* pTexture, u8 intensity, u32 type) {
        DrawProgram& rEntry = sDrawPrograms[sBloomStage][type];
        const NebulaHostServices* pHost = nebula_host();
        if (!rEntry.mRegistered) {
            NebulaMaterialProgram& rProgram = rEntry.mProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            NebulaMaterialTevStage& rStage = rProgram.tev[0];
            rStage.tex_coord = GX_TEXCOORD0;
            rStage.tex_map = GX_TEXMAP0;
            rStage.channel = GX_COLOR0A0;
            if (sBloomStage == BloomStage_Threshold) {
                rStage.color_in[0] = GX_CC_TEXC;
                rStage.color_in[1] = GX_CC_RASC;
                rStage.color_in[2] = GX_CC_ONE;
                rStage.color_in[3] = GX_CC_ZERO;
                rStage.color_op = GX_TEV_COMP_R8_GT;
                rStage.alpha_in[0] = rStage.alpha_in[1] = rStage.alpha_in[2] = GX_CA_ZERO;
                rStage.alpha_in[3] = GX_CA_KONST;
            } else {
                rStage.color_in[0] = GX_CC_ZERO;
                rStage.color_in[1] = GX_CC_TEXC;
                rStage.color_in[2] = GX_CC_RASC;
                rStage.color_in[3] = GX_CC_ZERO;
                rStage.color_op = GX_TEV_ADD;
                rStage.alpha_in[0] = GX_CA_ZERO;
                rStage.alpha_in[1] = GX_CA_TEXA;
                rStage.alpha_in[2] = GX_CA_RASA;
                rStage.alpha_in[3] = GX_CA_ZERO;
            }
            rStage.alpha_op = GX_TEV_ADD;
            rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
            rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
            rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
            rProgram.tex_gen[0].type = GX_TG_MTX2x4;
            rProgram.tex_gen[0].source = GX_TG_TEX0;
            rProgram.tex_gen[0].matrix = sBloomStage == BloomStage_ModulateIdentity ? GX_IDENTITY : GX_TEXMTX0;
            rProgram.tex_gen[0].post_matrix = GX_PTIDENTITY;
            for (u32 c = 0; c < 2; c++) {
                rProgram.channel[c].material_source = GX_SRC_REG;
                rProgram.channel[c].ambient_source = GX_SRC_REG;
                rProgram.channel[c].diffuse = GX_DF_NONE;
                rProgram.channel[c].attenuation = GX_AF_NONE;
            }
            rProgram.tev_stages = 1;
            rProgram.tex_gens = 1;
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_NONE;
            rProgram.dither = GX_FALSE;
            rProgram.z_compare_location = 1;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_CHANNELS | NEBULA_MATERIAL_LOADS_FOG |
                             NEBULA_MATERIAL_LOADS_DITHER;
            rProgram.fog.type = GX_FOG_NONE;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.alpha_compare.op = GX_AOP_AND;
            static const u8 kBlend[3][3] = {{GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO},
                                            {GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE},
                                            {GX_BM_BLEND, GX_BL_SRCCLR, GX_BL_ZERO}};
            rProgram.blend.type = kBlend[type][0];
            rProgram.blend.source = kBlend[type][1];
            rProgram.blend.destination = kBlend[type][2];
            rProgram.blend.logic_op = GX_LO_CLEAR;
            rProgram.z_mode.enable = GX_FALSE;
            rProgram.z_mode.func = GX_ALWAYS;
            rProgram.z_mode.update = GX_FALSE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&rEntry), &rProgram);
            rEntry.mRegistered = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        values.material_color[0][0] = values.material_color[0][1] = values.material_color[0][2] = intensity;
        values.material_color[0][3] = 255;
        values.tex_coord_scale[0].scale_s = pTexture->getWidth();
        values.tex_coord_scale[0].scale_t = pTexture->getHeight();
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&rEntry), &values);
        J3DNativeMaterial::watch(NULL);
        u8 places[8] = {static_cast< u8 >(rEntry.mProgram.tex_gen[0].matrix), GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                        GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, places);
    }
    u16 sBlurTextureSize[2] = {0, 0};

    void sendBlurProgram(u32 count) {
        BlurProgram& rEntry = sBlurPrograms[count - 1][sBlurAdditive ? 1 : 0];
        const NebulaHostServices* pHost = nebula_host();
        if (!rEntry.mRegistered) {
            NebulaMaterialProgram& rProgram = rEntry.mProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            for (u32 i = 0; i < count; i++) {
                NebulaMaterialTevStage& rStage = rProgram.tev[i];
                rStage.tex_coord = static_cast< u8 >(i);
                rStage.tex_map = GX_TEXMAP0;
                rStage.channel = GX_COLOR0A0;
                rStage.color_in[0] = GX_CC_ZERO;
                rStage.color_in[1] = GX_CC_TEXC;
                rStage.color_in[2] = GX_CC_RASC;
                rStage.color_in[3] = i == 0 ? GX_CC_ZERO : GX_CC_CPREV;
                rStage.color_op = GX_TEV_ADD;
                rStage.color_scale = GX_CS_SCALE_1;
                rStage.color_reg = GX_TEVPREV;
                rStage.alpha_in[0] = rStage.alpha_in[1] = rStage.alpha_in[2] = GX_CA_ZERO;
                rStage.alpha_in[3] = GX_CA_KONST;
                rStage.alpha_op = GX_TEV_ADD;
                rStage.alpha_scale = GX_CS_SCALE_1;
                rStage.alpha_reg = GX_TEVPREV;
                rProgram.tex_gen[i].type = GX_TG_MTX3x4;
                rProgram.tex_gen[i].source = GX_TG_TEX0;
                rProgram.tex_gen[i].matrix = static_cast< u8 >(sTexMtxID[i]);
                rProgram.tex_gen[i].post_matrix = GX_PTIDENTITY;
            }
            rProgram.tev_stages = static_cast< u8 >(count);
            rProgram.tex_gens = static_cast< u8 >(count);
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_NONE;
            rProgram.z_compare_location = 1;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.alpha_compare.op = GX_AOP_AND;
            rProgram.blend.type = sBlurAdditive ? GX_BM_BLEND : GX_BM_NONE;
            rProgram.blend.source = sBlurAdditive ? GX_BL_ONE : GX_BL_ZERO;
            rProgram.blend.destination = sBlurAdditive ? GX_BL_ONE : GX_BL_ZERO;
            rProgram.blend.logic_op = GX_LO_CLEAR;
            rProgram.z_mode.enable = GX_FALSE;
            rProgram.z_mode.func = GX_ALWAYS;
            rProgram.z_mode.update = GX_FALSE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&rEntry), &rProgram);
            rEntry.mRegistered = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        for (u32 i = 0; i < count; i++) {
            values.tex_coord_scale[i].scale_s = sBlurTextureSize[0];
            values.tex_coord_scale[i].scale_t = sBlurTextureSize[1];
        }
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&rEntry), &values);
        J3DNativeMaterial::watch(NULL);
        u8 places[8];
        for (u32 i = 0; i < 8; i++) {
            places[i] = static_cast< u8 >(sTexMtxID[i]);
        }
        J3DNativeShape::setCurrent(GX_PNMTX0, places);
    }
    static const u32 sL2RoundPoints = 12;
    // static const f32 sBloomIntensity = _;
    // static const f32 sMaskThreshold = _;
    // static const ... sBlurType = _;
    // static const s32 sFadeCounterMax = _;

#ifdef TARGET_PC
    // How the bloom goes through the renderer (NEBULA_OPTION_BLOOM; galaxy/gx/game_bloom.h): decided at preDraw for
    // the frame's pair of passes, so a change in between never restores a scene that was not kept.
    u32 sBloomRoute;

    u32 floatBits(f32 value) {
        u32 bits;
        __builtin_memcpy(&bits, &value, sizeof(bits));
        return bits;
    }
#endif
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

#ifdef TARGET_PC
    // The renderer keeps the scene in the EFB's own format: the console's copy below is 8 bits, which cuts the ray
    // tracer's HDR scene at 1.
    sBloomRoute = NebulaHost::option(NEBULA_OPTION_BLOOM);
    if (sBloomRoute != 0) {
        NebulaRender::bloomMarker(NebulaRender::kBloomKeep, 0, 0);
    } else
#endif
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

#ifdef TARGET_PC
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
#endif

    ImageEffectLocalUtil::capture(_28, 1, 0, GX_TF_RGB565, true, 0);
    initDraw();
    drawTexture(_28, 4, 0, 0xFF, DrawType_0);

    ImageEffectLocalUtil::capture(_2C, 4, 0, GX_TF_RGB565, false, 0);
    ImageEffectLocalUtil::capture(_40, 4, 0, GX_TF_I8, false, 0);
    drawTexture(_2C, 4, 2, 0xFF, DrawType_0);

    sBloomStage = BloomStage_Threshold;
    drawTexture(_40, 4, 2, _20, DrawType_2);
    sBloomStage = BloomStage_Modulate;

    J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_MODULATE);
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

#ifdef TARGET_PC
    if (sBloomRoute != 0) {
        // The scene the renderer kept, drawn back as _24 would be (its RGB; the alpha update is off).
        NebulaRender::bloomMarker(NebulaRender::kBloomRestore, 0, 0);
    } else
#endif
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
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(1);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
    J3DNativeShape::setTexture(0, posMtx.toMtxPtr(), 2);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_MODULATE);
    J3DNativeState::setNumIndStages(0);
    J3DNativeState::setTevDirect(GX_TEVSTAGE0);
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, Color8(0, 0, 0, 0));
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setDither(GX_FALSE);
    sBloomStage = BloomStage_Modulate;
}

void BloomEffect::drawTexture(JUTTexture* pTexture, s32 param2, s32 param3, u8 intensity, BLOOM_TEX_DRAW_TYPE param5) const {
    pTexture->load(GX_TEXMAP0);
    sendDrawProgram(pTexture, intensity, static_cast< u32 >(param5) < 3 ? static_cast< u32 >(param5) : 0);

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

    J3DNativeState::setNumTexGens(1);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_MODULATE);
}

void BloomEffect::initBlurMtx(Mtx* pMtx, u32 count, f32 radius, f32 offset) const {
    f32 invAspect = 1.0f / MR::getAspect();
    radius = -radius;

    for (u32 i = 0; i < count; i++) {
        NebulaMtxTrans(pMtx[i], invAspect * (radius * JMACosRadian(offset + i * JMath::TAngleConstant_< f32 >::RADIAN_DEG360() / count)),
                   radius * JMASinRadian(offset + i * JMath::TAngleConstant_< f32 >::RADIAN_DEG360() / count), 0.0f);
    }

}

// The blur's texture and colour; its stages are the blur programs drawBlur sends (sendBlurProgram).
void BloomEffect::initBlur(JUTTexture* pTexture, u32 param2, f32 intensity) const {
    (void)param2;
    J3DNativeState::setChanMatColor(GX_COLOR0A0, Color8((s32)(intensity * 255.0f), (s32)(intensity * 255.0f), (s32)(intensity * 255.0f), 255));
    pTexture->load(GX_TEXMAP0);
    sBlurTextureSize[0] = pTexture->getWidth();
    sBlurTextureSize[1] = pTexture->getHeight();
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
        sendBlurProgram(stageCount);

        for (u32 j = 0; j < stageCount; j++) {
            J3DNativeShape::setTexture((::sTexMtxID[j] - GX_TEXMTX0) / 3, pMtx[i + j], 3);
        }

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
