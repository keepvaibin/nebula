#include <revolution.h>
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include <revolution/gx/GXVert.h>

#ifdef __cplusplus
extern "C" {
#endif

static GXTexFilter gTexFilter;

void THPGXRestore(void) {
    J3DNativeState::setZMode(GX_ENABLE, GX_ALWAYS, GX_DISABLE);
    J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_SET);
    J3DNativeState::setNumTexGens(1);
    J3DNativeState::setNumChans(0);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_REPLACE);
    J3DNativeState::setTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    J3DNativeState::setTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
    J3DNativeState::setTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP0, GX_TEV_SWAP0);
    J3DNativeState::setTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP0, GX_TEV_SWAP0);
    J3DNativeState::setTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
    J3DNativeState::setTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
    J3DNativeState::setTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
    J3DNativeState::setTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);
}

void THPGXYuv2RgbSetup(GXRenderModeObj* rmode) {
    s32 scrWidth;
    s32 scrHeight;
    Mtx44 pMtx;
    Mtx mMtx;

    scrWidth = rmode->fbWidth;
    scrHeight = rmode->efbHeight;

    J3DNativeState::setPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
    C_MTXOrtho(pMtx, 0.0f, scrHeight, 0.0f, scrWidth, 0.0f, -1.0f);
    J3DNativeView::setProjection(pMtx, GX_ORTHOGRAPHIC);
    J3DNativeView::setViewport(0.0f, 0.0f, scrWidth, scrHeight, 0.0f, 1.0f);
    J3DNativeView::setScissor(0, 0, scrWidth, scrHeight);
    PSMTXIdentity(mMtx);
    J3DNativeShape::sendPosition(mMtx, GX_PNMTX0);
    J3DNativeState::setCurrentMtx(GX_PNMTX0);
    J3DNativeState::setZMode(GX_ENABLE, GX_ALWAYS, GX_DISABLE);
    J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    J3DNativeState::setColorUpdate(GX_TRUE);
    J3DNativeState::setAlphaUpdate(GX_FALSE);
    J3DNativeState::setDispCopyGamma(GX_GM_1_0);
    J3DNativeState::setNumChans(0);
    J3DNativeState::setNumTexGens(2);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::invalidateTexAll();
    J3DNativeState::setNumTevStages(4);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD1, GX_TEXMAP1, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_C0);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_A0);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
    J3DNativeState::setTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP2, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_TEXA, GX_CA_KONST, GX_CA_APREV);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_SUB, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K1);
    J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE1, GX_TEV_KASEL_K1_A);
    J3DNativeState::setTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP0, GX_TEV_SWAP0);
    J3DNativeState::setTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_ONE, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE2, GX_CA_TEXA, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP0, GX_TEV_SWAP0);
    J3DNativeState::setTevOrder(GX_TEVSTAGE3, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE3, GX_CC_APREV, GX_CC_CPREV, GX_CC_KONST, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevSwapMode(GX_TEVSTAGE3, GX_TEV_SWAP0, GX_TEV_SWAP0);
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE3, GX_TEV_KCSEL_K2);
    J3DNativeState::setTevColorS10(GX_TEVREG0, (GXColorS10){-90, 0, -114, 135});
    J3DNativeState::setTevKColor(GX_KCOLOR0, (GXColor){0, 0, 226, 88});
    J3DNativeState::setTevKColor(GX_KCOLOR1, (GXColor){179, 0, 0, 182});
    J3DNativeState::setTevKColor(GX_KCOLOR2, (GXColor){255, 0, 255, 128});
    J3DNativeState::setTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
}

GXTexFilter THPGXGetTexObjFilter(void) {
    return gTexFilter;
}

void THPGXSetTexObjFilter(GXTexFilter filter) {
    gTexFilter = filter;
}

void THPGXYuv2RgbDraw(u8* y_data, u8* u_data, u8* v_data, s16 x, s16 y, s16 textureWidth, s16 textureHeight, s16 polygonWidth, s16 polygonHeight) {
    GXTexObj tobj0, tobj1, tobj2;
    GXTexFilter filter = THPGXGetTexObjFilter();
    // The frame's planes, decoded into these buffers since the last frame.
    J3DNativeTexture::registerImage(y_data, GX_TF_I8, textureWidth, textureHeight, 1);
    J3DNativeTexture::registerImage(u_data, GX_TF_I8, textureWidth >> 1, textureHeight >> 1, 1);
    J3DNativeTexture::registerImage(v_data, GX_TF_I8, textureWidth >> 1, textureHeight >> 1, 1);
    J3DNativeTexture::init(&tobj0, y_data, textureWidth, textureHeight, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    J3DNativeTexture::initLOD(&tobj0, filter, filter, 0, 0, 0, 0, 0, GX_ANISO_1);
    J3DNativeTexture::load(&tobj0, GX_TEXMAP0);
    J3DNativeTexture::init(&tobj1, u_data, textureWidth >> 1, textureHeight >> 1, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    J3DNativeTexture::initLOD(&tobj1, filter, filter, 0, 0, 0, 0, 0, GX_ANISO_1);
    J3DNativeTexture::load(&tobj1, GX_TEXMAP1);
    J3DNativeTexture::init(&tobj2, v_data, textureWidth >> 1, textureHeight >> 1, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    J3DNativeTexture::initLOD(&tobj2, filter, filter, 0, 0, 0, 0, 0, GX_ANISO_1);
    J3DNativeTexture::load(&tobj2, GX_TEXMAP2);

    J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
    {
        J3DNativeDraw::position(static_cast< f32 >(static_cast< s16 >(x)), static_cast< f32 >(static_cast< s16 >(y)), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 0.0f);
        J3DNativeDraw::position(static_cast< f32 >(static_cast< s16 >(x + polygonWidth)), static_cast< f32 >(static_cast< s16 >(y)), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 0.0f);
        J3DNativeDraw::position(static_cast< f32 >(static_cast< s16 >(x + polygonWidth)), static_cast< f32 >(static_cast< s16 >(y + polygonHeight)), 0.0f);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
        J3DNativeDraw::position(static_cast< f32 >(static_cast< s16 >(x)), static_cast< f32 >(static_cast< s16 >(y + polygonHeight)), 0.0f);
        J3DNativeDraw::texCoord(0.0f, 1.0f);
    }
}

#ifdef __cplusplus
}
#endif
