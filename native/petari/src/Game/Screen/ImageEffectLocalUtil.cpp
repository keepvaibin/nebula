#include "Game/Screen/ImageEffectLocalUtil.hpp"
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
    void setTextureTrans(f32 x, f32 y) {
        TPos3f transMtx;
        transMtx.makeTrans(x, y, 0.0f);
        J3DNativeShape::setTexture(0, transMtx.toMtxPtr(), 2);
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
    ImageEffectLocalUtil::EStage sStage = ImageEffectLocalUtil::Stage_Modulate;

    // The texture draws' programs (nebula_game_api.h NebulaMaterialProgram), by stage and draw type, registered when
    // first used: setupDrawTexture's state (no fog, alpha test or depth, no dither, no culling, the material colour
    // from its register) with the stage and the draw type's blend.
    struct DrawProgram {
        NebulaMaterialProgram mProgram;
        bool mRegistered;
    };
    DrawProgram sDrawPrograms[2][4];
    // Caller-described stages (setCallerStage): the programs of each, by draw type.
    struct CallerPrograms {
        const NebulaMaterialTevStage* mStage;
        DrawProgram mPrograms[4];
    };
    CallerPrograms sCallerPrograms[8];
    const NebulaMaterialTevStage* sCallerStage = nullptr;

    DrawProgram& drawProgram(u32 type) {
        if (sCallerStage == nullptr) {
            return sDrawPrograms[sStage][type];
        }
        for (u32 i = 0; i < 8; i++) {
            if (sCallerPrograms[i].mStage == sCallerStage || sCallerPrograms[i].mStage == nullptr) {
                sCallerPrograms[i].mStage = sCallerStage;
                return sCallerPrograms[i].mPrograms[type];
            }
        }
        return sCallerPrograms[7].mPrograms[type];
    }

    void sendDrawProgram(JUTTexture* pTexture, u8 intensity, u32 type) {
        DrawProgram& rEntry = drawProgram(type);
        const NebulaHostServices* pHost = nebula_host();
        if (!rEntry.mRegistered) {
            NebulaMaterialProgram& rProgram = rEntry.mProgram;
            memset(&rProgram, 0, sizeof(rProgram));
            NebulaMaterialTevStage& rStage = rProgram.tev[0];
            rStage.tex_coord = GX_TEXCOORD0;
            rStage.tex_map = GX_TEXMAP0;
            rStage.channel = GX_COLOR0A0;
            if (sCallerStage != nullptr) {
                rStage = *sCallerStage;
            } else if (sStage == ImageEffectLocalUtil::Stage_Threshold) {
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
            if (sCallerStage == nullptr) {
                rStage.alpha_op = GX_TEV_ADD;
                rStage.color_scale = rStage.alpha_scale = GX_CS_SCALE_1;
                rStage.color_clamp = rStage.alpha_clamp = GX_TRUE;
                rStage.color_reg = rStage.alpha_reg = GX_TEVPREV;
            }
            rProgram.tex_gen[0].type = GX_TG_MTX2x4;
            rProgram.tex_gen[0].source = GX_TG_TEX0;
            rProgram.tex_gen[0].matrix = GX_TEXMTX0;
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
            rProgram.loads = NEBULA_MATERIAL_LOADS_TEV | NEBULA_MATERIAL_LOADS_CHANNELS | NEBULA_MATERIAL_LOADS_AMBIENT |
                             NEBULA_MATERIAL_LOADS_FOG | NEBULA_MATERIAL_LOADS_DITHER;
            rProgram.fog.type = GX_FOG_NONE;
            rProgram.alpha_compare.comp0 = rProgram.alpha_compare.comp1 = GX_ALWAYS;
            rProgram.alpha_compare.op = GX_AOP_OR;
            static const u8 kBlend[4][3] = {{GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO},
                                            {GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE},
                                            {GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA},
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
        if (type == ImageEffectLocalUtil::TexDrawType_2) {
            values.material_color[0][0] = values.material_color[0][1] = values.material_color[0][2] = 255;
            values.material_color[0][3] = intensity;
        } else {
            values.material_color[0][0] = values.material_color[0][1] = values.material_color[0][2] = intensity;
            values.material_color[0][3] = 255;
        }
        values.ambient_color[0][3] = 255;
        values.tex_coord_scale[0].scale_s = pTexture->getWidth();
        values.tex_coord_scale[0].scale_t = pTexture->getHeight();
        J3DNativeShape::flushState();
        pHost->material(pHost->user, reinterpret_cast< uintptr_t >(&rEntry), &values);
        static const u8 kPlaces[8] = {GX_TEXMTX0, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY,
                                      GX_IDENTITY, GX_IDENTITY, GX_IDENTITY, GX_IDENTITY};
        J3DNativeShape::setCurrent(GX_PNMTX0, kPlaces);
    }
}  // namespace

void ImageEffectLocalUtil::setStage(EStage stage) {
    sStage = stage;
    sCallerStage = nullptr;
}

void ImageEffectLocalUtil::setCallerStage(const NebulaMaterialTevStage* pStage) {
    sStage = Stage_Modulate;
    sCallerStage = pStage;
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
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(1);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
    J3DNativeShape::setTexture(0, posMtx.toMtxPtr(), 2);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_MODULATE);
    J3DNativeState::setZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);
    J3DNativeState::setNumIndStages(0);
    J3DNativeState::setTevDirect(GX_TEVSTAGE0);
    J3DNativeState::setTevDirect(GX_TEVSTAGE1);
    J3DNativeState::setTevDirect(GX_TEVSTAGE2);
    J3DNativeState::setTevDirect(GX_TEVSTAGE3);
    J3DNativeState::setTevDirect(GX_TEVSTAGE4);
    J3DNativeState::setTevDirect(GX_TEVSTAGE5);
    J3DNativeState::setTevDirect(GX_TEVSTAGE6);
    J3DNativeState::setTevDirect(GX_TEVSTAGE7);
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, Color8(0, 0, 0, 0));
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setDither(GX_FALSE);
    sStage = Stage_Modulate;
    sCallerStage = nullptr;
}

void ImageEffectLocalUtil::drawTexture(JUTTexture* pTexture, s32 divisions, s32 tile, u8 intensity, ETexDrawType texDrawType) {
    pTexture->load(GX_TEXMAP0);
    sendDrawProgram(pTexture, intensity, static_cast< u32 >(texDrawType) & 3u);
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
