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

namespace {
    void setTextureTrans(f32 x, f32 y) {
        TPos3f transMtx;
        transMtx.makeTrans(x, y, 0.0f);
        J3DNativeShape::sendTexture(transMtx.toMtxPtr(), GX_TEXMTX0, GX_MTX2x4);
    }

};  // namespace

void MR::connectToSceneImageEffect(NameObj* pObj) {
    MR::connectToScene(pObj, MR::MovementType_ImageEffect, MR::CalcAnimType_Environment, MR::DrawBufferType_None, MR::DrawType_ImageEffect);
}

void MR::connectToSceneImageEffectMovement(NameObj* pObj) {
    MR::connectToScene(pObj, MR::MovementType_ImageEffect, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void ImageEffectLocalUtil::capture(JUTTexture* pTexture, s32 divisions, s32 tile, GXTexFmt format, bool clear, u8 filter) {
    // FallOutFieldDraw::draw passes 0 divisions each frame it draws: the capture then starts at the origin.
    if (divisions == 0) {
        pTexture->capture(0, 0, format, clear, filter);
        return;
    }
    pTexture->capture((tile % divisions) * MR::getFrameBufferWidth() / divisions, (tile / divisions) * MR::getFrameBufferHeight() / divisions, format,
                      clear, filter);
}

void ImageEffectLocalUtil::setupDrawTexture() {
    Mtx44 projectionMtx;
    C_MTXOrtho(projectionMtx, 0.0f, MR::getFrameBufferHeight(), 0.0f, MR::getFrameBufferWidth(), -10000.0f, 10000.0f);
    J3DNativeView::setProjection(projectionMtx, GX_ORTHOGRAPHIC);

    MR::loadViewMtxFor2DModel();

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::sendPosition(posMtx.toMtxPtr(), GX_PNMTX0);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);

    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(1);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
    J3DNativeShape::sendTexture(posMtx.toMtxPtr(), GX_TEXMTX0, GX_MTX2x4);
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
}

void ImageEffectLocalUtil::drawTexture(JUTTexture* pTexture, s32 divisions, s32 tile, u8 intensity, ETexDrawType texDrawType) {
    pTexture->load(GX_TEXMAP0);

    if (texDrawType == TexDrawType_2) {
        J3DNativeState::setChanMatColor(GX_COLOR0A0, Color8(255, 255, 255, intensity));
        J3DNativeState::setChanAmbColor(GX_COLOR0A0, Color8(0, 0, 0, 255));
    } else {
        J3DNativeState::setChanMatColor(GX_COLOR0A0, Color8(intensity, intensity, intensity, 255));
        J3DNativeState::setChanAmbColor(GX_COLOR0A0, Color8(0, 0, 0, 255));
    }

    switch (texDrawType) {
    case TexDrawType_0:
        J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
        break;
    case TexDrawType_1:
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ONE, GX_LO_CLEAR);
        break;
    case TexDrawType_2:
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        break;
    case TexDrawType_3:
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCCLR, GX_BL_ZERO, GX_LO_CLEAR);
        break;
    }

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
