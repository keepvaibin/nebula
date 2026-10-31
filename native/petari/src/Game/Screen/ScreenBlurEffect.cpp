#include "Game/Screen/ScreenBlurEffect.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/MathUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <nebula_game_api.h>
#include <cstring>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    // The blur's material (nebula_game_api.h NebulaMaterialProgram): stage 0 the blurred picture (texture 0) with the
    // material alpha, stage 1 keeping it where the depth texture (1) is above konst 0; depth tested, not written,
    // blended by source alpha.
    NebulaMaterialProgram sBlurProgram;
    bool sBlurRegistered = false;

    void sendBlurProgram(JUTTexture* pPicture, JUTTexture* pDepth, u8 alpha) {
        const NebulaHostServices* pHost = nebula_host();
        if (!sBlurRegistered) {
            NebulaMaterialProgram& rProgram = sBlurProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            NebulaMaterialTevStage& rStage0 = rProgram.tev[0];
            rStage0.tex_coord = GX_TEXCOORD0;
            rStage0.tex_map = GX_TEXMAP0;
            rStage0.channel = GX_COLOR0A0;
            rStage0.color_in[0] = rStage0.color_in[1] = rStage0.color_in[2] = GX_CC_ZERO;
            rStage0.color_in[3] = GX_CC_TEXC;
            rStage0.alpha_in[0] = rStage0.alpha_in[1] = rStage0.alpha_in[2] = GX_CA_ZERO;
            rStage0.alpha_in[3] = GX_CA_RASA;
            rStage0.color_op = rStage0.alpha_op = GX_TEV_ADD;
            NebulaMaterialTevStage& rStage1 = rProgram.tev[1];
            rStage1.tex_coord = GX_TEXCOORD1;
            rStage1.tex_map = GX_TEXMAP1;
            rStage1.channel = GX_COLOR_NULL;
            rStage1.color_in[0] = GX_CC_TEXC;
            rStage1.color_in[1] = GX_CC_KONST;
            rStage1.color_in[2] = GX_CC_CPREV;
            rStage1.color_in[3] = GX_CC_ZERO;
            rStage1.alpha_in[0] = GX_CA_TEXA;
            rStage1.alpha_in[1] = GX_CA_KONST;
            rStage1.alpha_in[2] = GX_CA_APREV;
            rStage1.alpha_in[3] = GX_CA_ZERO;
            rStage1.color_op = rStage1.alpha_op = GX_TEV_COMP_R8_GT;
            for (u32 i = 0; i < 2; i++) {
                NebulaMaterialTevStage& rStage = rProgram.tev[i];
                rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
                rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
                rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
                rProgram.tex_gen[i].type = GX_TG_MTX2x4;
                rProgram.tex_gen[i].source = GX_TG_TEX0;
                rProgram.tex_gen[i].matrix = GX_IDENTITY;
                rProgram.tex_gen[i].post_matrix = GX_PTIDENTITY;
                rProgram.channel[i].material_source = GX_SRC_REG;
                rProgram.channel[i].ambient_source = GX_SRC_REG;
                rProgram.channel[i].diffuse = GX_DF_NONE;
                rProgram.channel[i].attenuation = GX_AF_NONE;
            }
            rProgram.tev_stages = 2;
            rProgram.tex_gens = 2;
            rProgram.channels = 1;
            rProgram.cull = GX_CULL_NONE;
            rProgram.dither = GX_FALSE;
            rProgram.z_compare_location = 1;
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_CHANNELS | NEBULA_MATERIAL_LOADS_AMBIENT |
                             NEBULA_MATERIAL_LOADS_FOG | NEBULA_MATERIAL_LOADS_DITHER;
            rProgram.fog.type = GX_FOG_NONE;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.alpha_compare.op = GX_AOP_AND;
            rProgram.blend.type = GX_BM_BLEND;
            rProgram.blend.source = GX_BL_SRCALPHA;
            rProgram.blend.destination = GX_BL_INVSRCALPHA;
            rProgram.blend.logic_op = GX_LO_CLEAR;
            rProgram.z_mode.enable = GX_TRUE;
            rProgram.z_mode.func = GX_LEQUAL;
            rProgram.z_mode.update = GX_FALSE;
            pHost->material_program(pHost->user, reinterpret_cast< uintptr_t >(&sBlurProgram), &rProgram);
            sBlurRegistered = true;
        }
        NebulaMaterialValues values;
        memset(&values, 0, sizeof(values));
        values.material_color[0][0] = values.material_color[0][1] = values.material_color[0][2] = 255;
        values.material_color[0][3] = alpha;
        memset(values.ambient_color[0], 255, sizeof(values.ambient_color[0]));
        values.tex_coord_scale[0].scale_s = pPicture->getWidth();
        values.tex_coord_scale[0].scale_t = pPicture->getHeight();
        values.tex_coord_scale[1].scale_s = pDepth->getWidth();
        values.tex_coord_scale[1].scale_t = pDepth->getHeight();
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&sBlurProgram), &values);
        static const u8 kPlaces[8] = {GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                                      GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kPlaces);
    }
}  // namespace
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>

namespace {
    static const f32 sScale = 10000.0f;
};  // namespace

ScreenBlurEffect::ScreenBlurEffect(const char* pName) : ImageEffectBase(pName), _14(), _18(), mScale(::sScale), _20(true), mIntensity(1.0f) {
    MR::connectToSceneImageEffect(this);
    MR::createImageEffectSystemHolder();
    MR::getImageEffectResource()->createBlurTexture();

    _14 = MR::getImageEffectResource()->_0;
    _18 = MR::getImageEffectResource()->_8;
}

void ScreenBlurEffect::calcAnimSub() {
    _20 = false;
}

void ScreenBlurEffect::notifyTurnOn() {
    _20 = true;
}

void ScreenBlurEffect::draw() const {
    if (isSomething()) {
        drawBlur();
        captureBlur();
    }
}

void ScreenBlurEffect::drawBlur() const {
    if (_20) {
        return;
    }

    TProj3f projectionMtx;
    projectionMtx.makePerspective(MR::getRenderFovy(), MR::getAspect(), MR::getNearZ(), MR::getFarZ());
    J3DNativeView::setProjection(projectionMtx, GX_PERSPECTIVE);

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::setPosition(0, (MtxPtr)&posMtx);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);

    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    sendBlurProgram(_14, _18, static_cast< u8 >(static_cast< s32 >(get_10() * 220.0f * mIntensity)));
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
    J3DNativeState::setTevKColor(GX_KCOLOR0, Color8(240, 240, 240, 240));

    _14->load(GX_TEXMAP0);
    _18->load(GX_TEXMAP1);

    f32 y = MR::tanDegree(MR::getRenderFovy() * 0.5f) * mScale;
    f32 x = MR::getAspect() * y;

    J3DNativeDraw::begin(GX_TRIANGLESTRIP, 4, NEBULA_SHAPE_TEXCOORD0);
    {
        f32 z = -mScale;

        J3DNativeDraw::position(-x, -y, z);
        J3DNativeDraw::texCoord(0.0f, 1.0f);

        J3DNativeDraw::position(-x, y, z);
        J3DNativeDraw::texCoord(0.0f, 0.0f);

        J3DNativeDraw::position(x, -y, z);
        J3DNativeDraw::texCoord(1.0f, 1.0f);

        J3DNativeDraw::position(x, y, z);
        J3DNativeDraw::texCoord(1.0f, 0.0f);
    }
}

void ScreenBlurEffect::captureBlur() const {
    _14->capture(0, 0, GX_TF_RGBA8, false, 0);
    _18->capture(0, 0, GX_TF_Z8, true, 0);
}
