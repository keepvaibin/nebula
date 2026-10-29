#include "Game/Screen/DepthOfFieldBlur.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Screen/CopyFilterNegater.hpp"
#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Screen/ImageEffectResource.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>

namespace {
    // static const s32 sFirstBlurCount = _;
    // static const f32 sFirstBlurRadius = _;
    // static const f32 sBlendRate = _;
    static const f32 sViewDistance = 10000.0f;
    static const s8 sScaleExp = 1;
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

    CopyFilterNegater negator;
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
    C_MTXOrtho(projectionMtx, 0.0f, static_cast< s32 >(JUTVideo::getManager()->getEfbHeight()), 0.0f,
               static_cast< s32 >(JUTVideo::getManager()->getFbWidth()), -::sViewDistance, ::sViewDistance);
    J3DNativeView::setProjection(projectionMtx, GX_ORTHOGRAPHIC);
    MR::loadViewMtxFor2DModel();

    TMtx34f posMtx;
    posMtx.identity();
    J3DNativeShape::sendPosition(posMtx.toMtxPtr(), GX_PNMTX0);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(0);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);
    J3DNativeShape::sendTexture(posMtx.toMtxPtr(), GX_TEXMTX0, GX_MTX2x4);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    J3DNativeState::setNumIndStages(0);
    J3DNativeState::setTevDirect(GX_TEVSTAGE0);
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, GXColor());
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setDither(GX_FALSE);
    J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_CLEAR);
    J3DNativeState::setLineWidth(12, GX_TO_ZERO);

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
    J3DNativeState::setNumTexGens(3);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setNumTevStages(2);
    J3DNativeState::setNumIndStages(1);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR0A0);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevIndirect(GX_TEVSTAGE1, GX_INDTEXSTAGE0, GX_ITF_8, GX_ITB_ST, GX_ITM_0, GX_ITW_0, GX_ITW_0, GX_FALSE, GX_FALSE, GX_ITBA_OFF);
    J3DNativeState::setIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD2, GX_TEXMAP2);
    f32 offsetMtx[][3] = {{0.5f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}};
    J3DNativeState::setIndTexMtx(GX_ITM_0, offsetMtx, ::sScaleExp);
    J3DNativeState::setIndTexCoordScale(GX_INDTEXSTAGE0, GX_ITS_1, GX_ITS_1);
    _28->load(GX_TEXMAP1);
    _20->load(GX_TEXMAP2);
    ImageEffectLocalUtil::drawTexture(_24, 1, 0, (s32)(mIntensity * get_10() * 255.0f), ImageEffectLocalUtil::TexDrawType_2);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    J3DNativeState::setNumIndStages(0);
    J3DNativeState::setTevDirect(GX_TEVSTAGE0);
    J3DNativeState::setTevDirect(GX_TEVSTAGE1);
    J3DNativeState::setTevDirect(GX_TEVSTAGE2);
    J3DNativeState::setTevDirect(GX_TEVSTAGE3);
    ImageEffectLocalUtil::setupDrawTexture();
}
