#include "Game/Util/DrawUtil.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Screen/ScreenAlphaCapture.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/J3DGraphAnimator/J3DJoint.hpp>
#include <JSystem/J3DGraphAnimator/J3DModel.hpp>
#include <JSystem/J3DGraphBase/J3DMaterial.hpp>
#include <JSystem/J3DGraphBase/J3DPacket.hpp>
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JUtility/JUTVideo.hpp>
#include <revolution/gx/GXBump.h>
#include <revolution/gx/GXCull.h>
#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXFrameBuf.h>
#include <revolution/gx/GXGeometry.h>
#include <revolution/gx/GXPixel.h>
#include <revolution/gx/GXRegs.h>
#include <revolution/gx/GXStruct.h>
#include <revolution/gx/GXTransform.h>
#include <revolution/gx/GXVert.h>

static u8 sTexImgObj[] ATTRIBUTE_ALIGN(32) = {0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF,
                                              0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF, 0,    0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                              0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void DrawUtil_FORCE_MATCH_SDATA2() {
    (void)-10000.0f;
    (void)10000.0f;
    (void)608.0f;
    (void)-1000.0f;
    (void)1000.0f;
    (void)-0.5f;
    (void)-30000.0f;
    (void)30000.0f;
}

namespace {
    GXTexObj clear_z_tobj;

    const f32 cNearZ = -10000.0f;
    const f32 cFarZ = 10000.0f;

    inline s32 getScreenHeightInline() {
        return MR::getScreenHeight();
    }
};  // namespace

const JUTTexture* mShadowTex;
TVec3f mShadowVec;

namespace MR {
    void drawInit() {
        j3dSys.offFlag(2);
        j3dSys.drawInit();
    }

    void reinitGX() {
        j3dSys.reinitGX();
        J3DNativeState::setAlphaUpdate(GX_FALSE);
    }

    void loadViewMtxFor2DModel() {
        Mtx viewMtx;
        PSMTXIdentity(viewMtx);
        PSMTXCopy(viewMtx, j3dSys.mViewMtx);
    }

    void drawInitFor2DModel() {
        MR::reinitGX();
        J3DNativeState::setZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);

        Mtx44 projMtx;
        C_MTXOrtho(projMtx, 0.0f, -MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), ::cNearZ, ::cFarZ);
        J3DNativeView::setProjection(projMtx, GX_ORTHOGRAPHIC);

        Mtx viewMtx;
        PSMTXIdentity(viewMtx);
        PSMTXCopy(viewMtx, j3dSys.mViewMtx);
    }

    void setDefaultViewportAndScissor() {
        s32 width = MR::getFrameBufferWidth();
        s32 height = ::getScreenHeightInline();
        J3DNativeView::setViewport(0.0f, 0.0f, width, height, 0.0f, 1.0f);
        J3DNativeView::setScissor(0, 0, width, height);
    }

    void loadProjectionMtxFor2D() {
        J2DOrthoGraphSimple orthoGraph;
        orthoGraph.setPort();
    }

    void setupDrawForNW4RLayout(f32 scale, bool) {
        f32 height = MR::getScreenHeight();
        f32 width = 608.0f;
#ifdef TARGET_PC
        // The layout canvas widens with the picture; under 16:9 the layouts keep their width and gain height.
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            height *= MR::getDisplay().vertical_fit;
            width = MR::getDisplay().canvas_width;
        }
#endif
        f32 halfWidth = width / 2.0f;
        height *= 0.5f;
        halfWidth *= scale;
        height *= scale;
        Mtx44 projMtx;
        C_MTXOrtho(projMtx, height, -height, -halfWidth, halfWidth, -1000.0f, 1000.0f);
        J3DNativeView::setProjection(projMtx, GX_ORTHOGRAPHIC);
        J3DNativeState::setCullMode(GX_CULL_NONE);
        J3DNativeState::setZMode(GX_FALSE, GX_NEVER, GX_FALSE);
    }

    void clearZBuffer() {
        JUTVideo* pJUTVideo = JUTVideo::getManager();
        u16 width = pJUTVideo->getFbWidth();
        u16 height = pJUTVideo->getEfbHeight();
        Mtx44 projMtx;
        C_MTXOrtho(projMtx, 0.0f, height, 0.0f, width, 0.0f, 1.0f);
        J3DNativeView::setProjection(projMtx, GX_ORTHOGRAPHIC);
        MR::setDefaultViewportAndScissor();
        TMtx34f mtxImm;
        mtxImm.identity();
        J3DNativeShape::sendPosition(mtxImm, GX_PNMTX0);
        J3DNativeState::setCurrentMtx(GX_PNMTX0);
        J3DNativeState::setNumChans(0);
        J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setNumTexGens(1);
        J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        J3DNativeTexture::registerImage(sTexImgObj, GX_TF_Z24X8, 4, 4, 1);
        J3DNativeTexture::init(&::clear_z_tobj, sTexImgObj, 4, 4, GX_TF_Z24X8, GX_REPEAT, GX_REPEAT, GX_FALSE);
        J3DNativeTexture::initLOD(&::clear_z_tobj, GX_NEAR, GX_NEAR, 0.0f, 0.0f, 0.0f, GX_FALSE, GX_FALSE, GX_ANISO_1);
        J3DNativeTexture::load(&::clear_z_tobj, GX_TEXMAP0);
        J3DNativeState::setNumTevStages(1);
        GXColor tev0;
        tev0.r = 0;
        tev0.g = 0;
        tev0.b = 0;
        tev0.a = 0;
        J3DNativeState::setTevColor(GX_TEVREG0, tev0);
        J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
        J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
        J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
        J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
        J3DNativeState::setZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0);
        J3DNativeState::setZCompLoc(GX_FALSE);
        J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_NOOP);
        J3DNativeState::setColorUpdate(GX_FALSE);
        J3DNativeState::setAlphaUpdate(GX_FALSE);
        J3DNativeState::setZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
        J3DNativeState::setCullMode(GX_CULL_BACK);

#ifdef TARGET_PC
        __NebulaGXClearQuadMarker(0xFFFFFF);
#endif
        J3DNativeDraw::begin(GX_QUADS, 4, NEBULA_SHAPE_TEXCOORD0);
        {
            J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
            J3DNativeDraw::texCoord(0.0f, 0.0f);

            J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(width)), 0.0f, 0.0f);
            J3DNativeDraw::texCoord(1.0f, 0.0f);

            J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(width)), static_cast< f32 >(static_cast< u16 >(height)), 0.0f);
            J3DNativeDraw::texCoord(1.0f, 1.0f);

            J3DNativeDraw::position(0.0f, static_cast< f32 >(static_cast< u16 >(height)), 0.0f);
            J3DNativeDraw::texCoord(0.0f, 1.0f);
        }

        J3DNativeState::setZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);
        J3DNativeState::setZCompLoc(GX_TRUE);
        J3DNativeState::setColorUpdate(GX_TRUE);
    }

    void clearAlphaBuffer(u8 alpha) {
        JUTVideo* pVideo = JUTVideo::getManager();
        f32 height = pVideo->getEfbHeight();
        f32 width = pVideo->getFbWidth();
        clearAlphaBuffer(alpha, TVec2f(0.0f, 0.0f), TVec2f(width, height));
    }

    void clearAlphaBuffer(u8 alpha, const TVec2f& rPosition, const TVec2f& rSize) {
        u16 width = (s32)(rSize.x);
        u16 height = (s32)(rSize.y);
        Mtx44 projection;
        C_MTXOrtho(projection, 0.0f, ::getScreenHeightInline(), 0.0f, getFrameBufferWidth(), -1.0f, 1.0f);
        J3DNativeView::setProjection(projection, GX_ORTHOGRAPHIC);
        J3DNativeState::setCurrentMtx(GX_PNMTX0);
        Mtx matrix;
        PSMTXIdentity(matrix);
        J3DNativeShape::sendPosition(matrix, GX_PNMTX0);
        J3DNativeState::setNumTexGens(0);
        J3DNativeState::setNumTevStages(1);
        J3DNativeState::setTevDirect(GX_TEVSTAGE0);
        J3DNativeState::setTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        J3DNativeState::setNumChans(1);
        J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setCoPlanar(GX_FALSE);
        J3DNativeState::setClipMode(GX_CLIP_DISABLE);
        J3DNativeState::setCullMode(GX_CULL_NONE);
        J3DNativeState::setZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
        J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        J3DNativeState::setBlendMode(GX_BM_NONE, GX_BL_ZERO, GX_BL_ZERO, GX_LO_COPY);
        J3DNativeState::setColorUpdate(GX_FALSE);
        J3DNativeState::setAlphaUpdate(GX_TRUE);
        J3DNativeState::setDstAlpha(GX_TRUE, alpha);

        J3DNativeDraw::begin(GX_TRIANGLES, 6, 0);
        J3DNativeDraw::position(rPosition.x, rPosition.y, 0.0f);
        J3DNativeDraw::position(width + rPosition.x, rPosition.y, 0.0f);
        J3DNativeDraw::position(width + rPosition.x, height + rPosition.y, 0.0f);
        J3DNativeDraw::position(rPosition.x, rPosition.y, 0.0f);
        J3DNativeDraw::position(width + rPosition.x, height + rPosition.y, 0.0f);
        J3DNativeDraw::position(rPosition.x, height + rPosition.y, 0.0f);

        J3DNativeState::setDstAlpha(GX_FALSE, 0);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    }

    void fillScreenSetup(const GXColor& rColor) {
        TMtx34f mtxImm;
        mtxImm.identity();
        J3DNativeShape::sendPosition(mtxImm, GX_PNMTX0);
        J3DNativeState::setCurrentMtx(GX_PNMTX0);
        Mtx44 projMtx;
        C_MTXOrtho(projMtx, 0.0f, ::getScreenHeightInline(), 0.0f, MR::getFrameBufferWidth(), -1.0f, 1.0f);
        J3DNativeView::setProjection(projMtx, GX_ORTHOGRAPHIC);
        J3DNativeState::setNumChans(1);
        J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
        J3DNativeState::setZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
        J3DNativeState::setZCompLoc(GX_FALSE);
        J3DNativeState::setCullMode(GX_CULL_NONE);
        J3DNativeState::setDither(GX_FALSE);
        J3DNativeState::setNumTexGens(0);
        J3DNativeState::setNumTevStages(1);
        J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
        J3DNativeState::setTevColor(GX_TEVREG0, rColor);
        J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
        J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_A0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
        J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    }

    void fillScreenArea(const TVec2s& rMin, const TVec2s& rMax) {
        J3DNativeDraw::begin(GX_QUADS, 4, 0);
        {
            u16 maxX = rMax.x;
            u16 minX = rMin.x;
            u16 minY = rMin.y;
            u16 maxY = rMax.y;

            J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(minX)), static_cast< f32 >(static_cast< u16 >(minY)), 0.0f);
            J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(maxX)), static_cast< f32 >(static_cast< u16 >(minY)), 0.0f);
            J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(maxX)), static_cast< f32 >(static_cast< u16 >(maxY)), 0.0f);
            J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(minX)), static_cast< f32 >(static_cast< u16 >(maxY)), 0.0f);
        }
    }

    void fillScreen(const GXColor& color) {
        fillScreenSetup(color);
        u16 width = getFrameBufferWidth();
        u16 height = getScreenHeight();
        fillScreenArea(TVec2s(0, 0), TVec2s(width, height));
    }

    void setupShadowVolumeDraw() {
        J3DNativeState::setTevDirect(GX_TEVSTAGE0);
        J3DNativeState::setNumIndStages(0);
        J3DNativeState::setNumTexGens(0);
        J3DNativeState::setNumChans(1);
        J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanMatColor(GX_COLOR0A0, Color8(0));
        J3DNativeState::setNumTevStages(1);
        J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
        J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_A0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
        J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
        J3DNativeState::setZCompLoc(GX_FALSE);
        J3DNativeState::setZMode(GX_TRUE, GX_GEQUAL, GX_FALSE);
        J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    }

    void drawSimpleModel(J3DModelData* pModelData) {
        J3DMaterial* pMaterial = pModelData->getJointNodePointer(0)->mMesh;
        j3dSys.setVtxPos(pModelData->mVertexData.mVtxPosArray);
        j3dSys.setVtxNrm(pModelData->mVertexData.mVtxNrmArray);
        j3dSys.setVtxCol(pModelData->mVertexData.mVtxColorArray[0]);

        while (pMaterial != nullptr) {
            pMaterial->mShape->simpleDraw();
            pMaterial = pMaterial->mNext;
        }
    }

    void fillSilhouetteColor() {
        MR::captureScreenAlpha(0);
        MR::loadScreenAlphaTexture(0, GX_TEXMAP0);
        Mtx44 projMtx;
        C_MTXOrtho(projMtx, 0.0f, MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), -1.0f, 1.0f);
        J3DNativeView::setProjection(projMtx, GX_ORTHOGRAPHIC);
        J3DNativeState::setTevDirect(GX_TEVSTAGE0);
        J3DNativeState::setNumIndStages(0);
        TMtx34f mtxImm;
        mtxImm.identity();
        J3DNativeShape::sendPosition(mtxImm, GX_PNMTX0);
        J3DNativeState::setCurrentMtx(GX_PNMTX0);
        J3DNativeState::setNumChans(1);
        J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
        J3DNativeState::setNumTexGens(1);
        J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
        J3DNativeState::setNumTevStages(1);
        J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
        J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
        J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        J3DNativeState::setTevKColor(GX_KCOLOR0, Color8(0, 0, 0, 1));
        J3DNativeState::setTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_K0_A);
        J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_TEXA, GX_CA_KONST, GX_CA_A0, GX_CA_ZERO);
        J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_COMP_RGB8_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        J3DNativeState::setAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
        J3DNativeState::setZMode(GX_TRUE, GX_ALWAYS, GX_FALSE);
        J3DNativeState::setZCompLoc(GX_FALSE);
        J3DNativeState::setCullMode(GX_CULL_NONE);
        J3DNativeState::setDither(GX_FALSE);
        J3DNativeState::setColorUpdate(GX_TRUE);
        J3DNativeState::setAlphaUpdate(GX_TRUE);
        J3DNativeState::setDstAlpha(GX_TRUE, 0);
        J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
        J3DNativeState::setTevColor(GX_TEVREG0, Color8(0x00000080));

        J3DNativeDraw::begin(GX_TRIANGLESTRIP, 4, NEBULA_SHAPE_TEXCOORD0);
        {
            u16 width = MR::getScreenWidth();
            u16 height = MR::getScreenHeight();

            J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
            J3DNativeDraw::texCoord(0.0f, 0.0f);

            J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(width)), 0.0f, 0.0f);
            J3DNativeDraw::texCoord(1.0f, 0.0f);

            J3DNativeDraw::position(0.0f, static_cast< f32 >(static_cast< u16 >(height)), 0.0f);
            J3DNativeDraw::texCoord(0.0f, 1.0f);

            J3DNativeDraw::position(static_cast< f32 >(static_cast< u16 >(width)), static_cast< f32 >(static_cast< u16 >(height)), 0.0f);
            J3DNativeDraw::texCoord(1.0f, 1.0f);
        }

        J3DNativeState::setDstAlpha(GX_FALSE, 0);
        J3DNativeState::setAlphaUpdate(GX_FALSE);
    }

    const JUTTexture* getMarioShadowTex() {
        return mShadowTex;
    }

    const JUTTexture* getMarioShadowTexForLoad() {
        return mShadowTex;
    }

    const TVec3f& getMarioShadowVec() {
        return mShadowVec;
    }

    void setMarioShadowTex(const JUTTexture* pShadowTex) {
        mShadowTex = pShadowTex;
    }

    void setMarioShadowVec(const TVec3f& rVec) {
        mShadowVec = rVec;
    }

    void loadTexProjectionMtx(u32 id) {
        TProj3f cameraProjection = MR::getCameraProjectionMtx();
        cameraProjection.mMtx[2][0] = 0.0f;
        cameraProjection.mMtx[2][1] = 0.0f;
        cameraProjection.mMtx[2][2] = -1.0f;
        cameraProjection.mMtx[2][3] = 0.0f;
        cameraProjection.mMtx[3][0] = 0.0f;
        cameraProjection.mMtx[3][1] = 0.0f;
        cameraProjection.mMtx[3][2] = 0.0f;
        cameraProjection.mMtx[3][3] = 1.0f;
        TPos3f matrix;
        MR::multMtx(matrix, MR::getCameraViewMtx(), cameraProjection);
        TPos3f mat;
        mat.identity();
        mat[0][0] = 0.5f;
        mat[0][2] = 0.5f;
        mat[1][1] = -0.5f;
        mat[1][2] = 0.5f;
        MR::multMtx(matrix, matrix, mat);
        J3DNativeShape::sendTexture(matrix, id, GX_MTX3x4);
    }

    void nonFilteredCapture(JUTTexture* pTexture, s16 width, s16 height) {
        GXRenderModeObj* pRenderModeObj = JUTVideo::getManager()->getRenderMode();
        J3DNativeState::setCopyFilter(GX_FALSE, pRenderModeObj->sample_pattern, GX_FALSE, pRenderModeObj->vfilter);
        pTexture->capture(width, height, (GXTexFmt)pTexture->getFormat(), false, 0);
        J3DNativeState::setCopyFilter(GX_FALSE, pRenderModeObj->sample_pattern, GX_TRUE, pRenderModeObj->vfilter);
    }

    void simpleDraw(J3DModel* pModel, J3DMaterial* pMaterial) {
        if (pMaterial != nullptr && pMaterial->mShape != nullptr && !pMaterial->mShape->checkFlag(1)) {
            J3DMatPacket* pMatPacket = pModel->getMatPacket(pMaterial->mIndex);
            J3DShapePacket* pShapePacket = pModel->getShapePacket(pMaterial->mShape->mIndex);
            j3dSys.setMatPacket(pMatPacket);
            J3DNativeMaterial::send(pMatPacket->getMaterial(), pMatPacket->mpTexture, pMatPacket->getDisplayListObj());
            pShapePacket->getShape()->loadPreDrawSetting();

            J3DNativeMaterial::sendDiffered(pShapePacket->getDisplayListObj());

            pShapePacket->drawFast();
        }
    }

    void activateGameSceneDraw3D() {
        GameSceneFunction::activateDraw3D();
    }

    void deactivateGameSceneDraw3D() {
        GameSceneFunction::deactivateDraw3D();
    }
};  // namespace MR

J2DOrthoGraphSimple::J2DOrthoGraphSimple() : J2DOrthoGraph(0.0f, 0.0f, MR::getFrameBufferWidth(), ::getScreenHeightInline(), -30000.0f, 30000.0f) {
    TBox2f bounds(0.0f, 0.0f, MR::getScreenWidth(), MR::getScreenHeight());
    setOrtho(0.0f, 0.0f, bounds.getWidth(), bounds.getHeight(), -30000.0f, 30000.0f);
}

void J2DOrthoGraphSimple::setPort() {
    J2DOrthoGraph::setPort();
}
