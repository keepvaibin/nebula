#include "Game/Screen/DepthOfFieldBlur.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // static const s32 sFirstBlurCount = _;
    // static const f32 sFirstBlurRadius = _;
    // static const f32 sBlendRate = _;
    static const f32 sViewDistance = 10000.0f;
    static const s8 sScaleExp = 1;

    // The depth-to-alpha pass's material (nebula_game_api.h NebulaMaterialProgram): the vertex colour passed
    // through, no fog, no alpha test, depth neither tested nor written, no dither, no blending, no culling.
    NebulaMaterialProgram sZAlphaProgram;
    bool sZAlphaRegistered = false;

    void sendZAlphaProgram() {
        const NebulaHostServices* pHost = nebula_host();
        if (!sZAlphaRegistered) {
            NebulaMaterialProgram& rProgram = sZAlphaProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            NebulaMaterialTevStage& rStage = rProgram.tev[0];
            rStage.tex_coord = GX_TEXCOORD_NULL;
            rStage.tex_map = GX_TEXMAP_NULL;
            rStage.channel = GX_COLOR0A0;
            rStage.color_in[0] = rStage.color_in[1] = rStage.color_in[2] = GX_CC_ZERO;
            rStage.color_in[3] = GX_CC_RASC;
            rStage.alpha_in[0] = rStage.alpha_in[1] = rStage.alpha_in[2] = GX_CA_ZERO;
            rStage.alpha_in[3] = GX_CA_RASA;
            rStage.color_op = rStage.alpha_op = GX_TEV_ADD;
            rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
            rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
            rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
            rProgram.tev_stages = 1;
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_NONE;
            rProgram.dither = GX_FALSE;
            rProgram.z_compare_location = 1;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_FOG | NEBULA_MATERIAL_LOADS_DITHER;
            rProgram.fog.type = GX_FOG_NONE;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.alpha_compare.op = GX_AOP_OR;
            rProgram.blend.type = GX_BM_NONE;
            rProgram.blend.source = rProgram.blend.destination = GX_BL_ZERO;
            rProgram.blend.logic_op = GX_LO_CLEAR;
            rProgram.z_mode.enable = GX_FALSE;
            rProgram.z_mode.func = GX_ALWAYS;
            rProgram.z_mode.update = GX_FALSE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&sZAlphaProgram), &rProgram);
            sZAlphaRegistered = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&sZAlphaProgram), &values);
    }

    // The final blend's material: the sharp picture's colour (texture 0) taken where the depth texture (2, as an
    // indirect offset into texture 1) leaves it, its alpha from texture 1 times the material alpha, blended over the
    // frame by source alpha; setupDrawTexture's other state.
    NebulaMaterialProgram sFinalProgram;
    bool sFinalRegistered = false;

    void sendFinalProgram(JUTTexture* pTexture0, JUTTexture* pTexture1, JUTTexture* pTexture2, u8 alpha) {
        const NebulaHostServices* pHost = nebula_host();
        if (!sFinalRegistered) {
            NebulaMaterialProgram& rProgram = sFinalProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            for (u32 i = 0; i < 2; i++) {
                NebulaMaterialTevStage& rStage = rProgram.tev[i];
                rStage.tex_coord = static_cast< u8 >(GX_TEXCOORD0 + i);
                rStage.tex_map = static_cast< u8 >(GX_TEXMAP0 + i);
                rStage.channel = GX_COLOR0A0;
                rStage.color_in[0] = rStage.color_in[1] = rStage.color_in[2] = GX_CC_ZERO;
                rStage.color_in[3] = i == 0 ? GX_CC_TEXC : GX_CC_CPREV;
                rStage.alpha_in[0] = GX_CA_ZERO;
                rStage.alpha_in[1] = i == 0 ? GX_CA_ZERO : GX_CA_TEXA;
                rStage.alpha_in[2] = i == 0 ? GX_CA_ZERO : GX_CA_RASA;
                rStage.alpha_in[3] = i == 0 ? GX_CA_KONST : GX_CA_ZERO;
                rStage.color_op = rStage.alpha_op = GX_TEV_ADD;
                rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
                rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
                rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
            }
            NebulaMaterialTevStage& rOffset = rProgram.tev[1];
            rOffset.ind_stage = GX_INDTEXSTAGE0;
            rOffset.ind_format = GX_ITF_8;
            rOffset.ind_bias = GX_ITB_ST;
            rOffset.ind_matrix = GX_ITM_0;
            rOffset.ind_wrap_s = rOffset.ind_wrap_t = GX_ITW_0;
            rOffset.ind_alpha = GX_ITBA_OFF;
            rProgram.ind_order[0].coord = GX_TEXCOORD2;
            rProgram.ind_order[0].map = GX_TEXMAP2;
            rProgram.ind_order[0].scale_s = rProgram.ind_order[0].scale_t = GX_ITS_1;
            for (u32 t = 0; t < 3; t++) {
                rProgram.tex_gen[t].type = GX_TG_MTX2x4;
                rProgram.tex_gen[t].source = GX_TG_TEX0;
                rProgram.tex_gen[t].matrix = GX_IDENTITY;
                rProgram.tex_gen[t].post_matrix = GX_PTIDENTITY;
            }
            for (u32 c = 0; c < 2; c++) {
                rProgram.channel[c].material_source = GX_SRC_REG;
                rProgram.channel[c].ambient_source = GX_SRC_REG;
                rProgram.channel[c].diffuse = GX_DF_NONE;
                rProgram.channel[c].attenuation = GX_AF_NONE;
            }
            rProgram.tev_stages = 2;
            rProgram.tex_gens = 3;
            rProgram.ind_stages = 1;
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_NONE;
            rProgram.dither = GX_FALSE;
            rProgram.z_compare_location = 1;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_INDIRECT | NEBULA_MATERIAL_LOADS_CHANNELS |
                             NEBULA_MATERIAL_LOADS_AMBIENT | NEBULA_MATERIAL_LOADS_FOG | NEBULA_MATERIAL_LOADS_DITHER;
            rProgram.fog.type = GX_FOG_NONE;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.alpha_compare.op = GX_AOP_OR;
            rProgram.blend.type = GX_BM_BLEND;
            rProgram.blend.source = GX_BL_SRCALPHA;
            rProgram.blend.destination = GX_BL_INVSRCALPHA;
            rProgram.blend.logic_op = GX_LO_CLEAR;
            rProgram.z_mode.enable = GX_FALSE;
            rProgram.z_mode.func = GX_ALWAYS;
            rProgram.z_mode.update = GX_FALSE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&sFinalProgram), &rProgram);
            sFinalRegistered = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        values.material_color[0][0] = values.material_color[0][1] = values.material_color[0][2] = 255;
        values.material_color[0][3] = alpha;
        values.ambient_color[0][3] = 255;
        values.ind_matrix[0].m[0][0] = 0.5f;
        values.ind_matrix[0].m[1][1] = 0.5f;
        values.ind_matrix[0].scale = ::sScaleExp;
        JUTTexture* textures[3] = {pTexture0, pTexture1, pTexture2};
        for (u32 t = 0; t < 3; t++) {
            values.tex_coord_scale[t].scale_s = textures[t]->getWidth();
            values.tex_coord_scale[t].scale_t = textures[t]->getHeight();
        }
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&sFinalProgram), &values);
        static const u8 kPlaces[8] = {GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                                      GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kPlaces);
    }
};  // namespace

DepthOfFieldBlur::DepthOfFieldBlur(const char* pName)
    : ImageEffectBase(pName), _14(), _18(), _1C(), _20(), _24(), _28(), mIntensity(1.0f), mBlurMaxDist(248), mBlurMinDist(242) {
    MR::connectToSceneImageEffect(this);
    MR::createImageEffectSystemHolder();
    MR::getImageEffectResource()->createDOFTexture();
    _14 = MR::getImageEffectResource()->_0;
    _18 = MR::getImageEffectResource()->_10;
    _1C = MR::getImageEffectResource()->_4;
    _20 = MR::getImageEffectResource()->_8;
    _24 = MR::getImageEffectResource()->_C;
    _28 = new JUTTexture(128, 2, GX_TF_I8);
    _28->mWrapS = GX_CLAMP;
    _28->mWrapT = GX_CLAMP;
    _28->mMinType = GX_LINEAR;
    _28->mMagType = GX_LINEAR;
}

void DepthOfFieldBlur::draw() const {
    if (!isSomething()) {
        return;
    }

    ImageEffectLocalUtil::capture(_18, 4, 0, GX_TF_RGB565, false, 0);
    ImageEffectLocalUtil::capture(_1C, 1, 0, GX_TF_RGB565, true, 0);
    drawZAlphaTex();
    ImageEffectLocalUtil::capture(_28, 1, 0, GX_CTF_R8, false, 0);
    ImageEffectLocalUtil::setupDrawTexture();
    createBlurTexture();
    ImageEffectLocalUtil::capture(_20, 1, 0, GX_TF_Z8, true, 0);
    drawFinal();
    MR::loadProjectionMtx();
    MR::loadViewMtx();
}

void DepthOfFieldBlur::drawZAlphaTex() const {
    Mtx44 projectionMtx;
    NebulaMtxOrtho(projectionMtx, 0.0f, static_cast< s32 >(JUTVideo::getManager()->getEfbHeight()), 0.0f,
               static_cast< s32 >(JUTVideo::getManager()->getFbWidth()), -::sViewDistance, ::sViewDistance);
    J3DNativeView::setProjection(projectionMtx, GX_ORTHOGRAPHIC);
    MR::loadViewMtxFor2DModel();

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::setPosition(0, posMtx.toMtxPtr());
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeShape::setTexture(0, posMtx.toMtxPtr(), 2);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setLineWidth(12, GX_TO_ZERO);
    sendZAlphaProgram();

    J3DNativeDraw::begin(GX_LINESTRIP, 4, NEBULA_SHAPE_COLOR0);
    {
        u8 blurMinDist;
        u8 blurMaxDist = 248;

        if (mBlurMaxDist >= 0) {
            blurMaxDist = mBlurMaxDist;
        }

        blurMinDist = 242;

        if (mBlurMinDist >= 0) {
            blurMinDist = mBlurMinDist;
        }

        if (blurMaxDist >= blurMinDist) {
            J3DNativeDraw::position(0.0f, 1.0f, 0.0f);
            J3DNativeDraw::color(0, 0, 0, 0xFF);
            J3DNativeDraw::position(blurMinDist * 0.5f, 1.0f, 0.0f);
            J3DNativeDraw::color(0, 0, 0, 0xFF);
            J3DNativeDraw::position(blurMaxDist * 0.5f, 1.0f, 0.0f);
            J3DNativeDraw::color(255, 255, 255, 0xFF);
            J3DNativeDraw::position(128.0f, 1.0f, 0.0f);
            J3DNativeDraw::color(255, 255, 255, 0xFF);
        } else {
            J3DNativeDraw::position(128.0f, 1.0f, 0.0f);
            J3DNativeDraw::color(0, 0, 0, 0xFF);
            J3DNativeDraw::position(blurMinDist * 0.5f, 1.0f, 0.0f);
            J3DNativeDraw::color(0, 0, 0, 0xFF);
            J3DNativeDraw::position(blurMaxDist * 0.5f, 1.0f, 0.0f);
            J3DNativeDraw::color(255, 255, 255, 0xFF);
            J3DNativeDraw::position(0.0f, 1.0f, 0.0f);
            J3DNativeDraw::color(255, 255, 255, 0xFF);
        }
    }
}

void DepthOfFieldBlur::createBlurTexture() const {
    ImageEffectLocalUtil::drawTexture(_1C, 4, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    ImageEffectLocalUtil::capture(_24, 4, 0, GX_TF_RGB565, false, 0);
    ImageEffectLocalUtil::blurTexture(_24, 4, 0, 4, mIntensity * get_10() * 0.005f, 1.0f);
    ImageEffectLocalUtil::capture(_24, 4, 0, GX_TF_RGB565, false, 0);
}

void DepthOfFieldBlur::drawFinal() const {
    ImageEffectLocalUtil::drawTexture(_18, 4, 0, 255, ImageEffectLocalUtil::TexDrawType_0);
    _24->load(GX_TEXMAP0);
    _28->load(GX_TEXMAP1);
    _20->load(GX_TEXMAP2);
    sendFinalProgram(_24, _28, _20, static_cast< u8 >((s32)(mIntensity * get_10() * 255.0f)));
    ImageEffectLocalUtil::sendTextureVertex(1, 0);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    J3DNativeState::setNumIndStages(0);
    J3DNativeState::setTevDirect(GX_TEVSTAGE0);
    J3DNativeState::setTevDirect(GX_TEVSTAGE1);
    J3DNativeState::setTevDirect(GX_TEVSTAGE2);
    J3DNativeState::setTevDirect(GX_TEVSTAGE3);
    ImageEffectLocalUtil::setupDrawTexture();
}
