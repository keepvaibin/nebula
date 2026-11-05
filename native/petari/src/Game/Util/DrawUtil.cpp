#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include <JSystem/J3DGraphBase/J3DNativeTexture.hpp>
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "JSystem/J3DGraphBase/J3DNativeMaterial.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
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
#include "Game/System/NebulaFrameClock.hpp"
#include <NebulaGpuTypes.h>
#include "Game/Util/RenderSignals.hpp"

namespace {
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

    void resetDrawState() {
        j3dSys.resetDrawState();
        J3DPass::writeAlpha(false);
    }

    void loadViewMtxFor2DModel() {
        Mtx viewMtx;
        NebulaMtxIdentity(viewMtx);
        NebulaMtxCopy(viewMtx, j3dSys.mViewMtx);
    }

    void drawInitFor2DModel() {
        MR::resetDrawState();
        J3DPass::depth(false, NEBULA_COMPARE_ALWAYS, false);

        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, 0.0f, -MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), ::cNearZ, ::cFarZ);
        J3DNativeView::setOrthographic(projMtx);

        Mtx viewMtx;
        NebulaMtxIdentity(viewMtx);
        NebulaMtxCopy(viewMtx, j3dSys.mViewMtx);
    }

    void setDefaultViewportAndScissor() {
        J3DNativeView::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        J3DNativeView::setScissor(0.0f, 0.0f, 1.0f, 1.0f);
    }

    // The whole screen, for a fill or a picture over it.
    void setupScreen2D() {
        setDefaultViewportAndScissor();
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, ::getScreenHeightInline(), 0.0f, getScreenWidth(), -30000.0f, 30000.0f);
        J3DNativeView::setOrthographic(projection);
        Mtx identity;
        NebulaMtxIdentity(identity);
        J3DNativeShape::setPosition(0, identity);
        J3DNativeShape::setCurrentIdentity();
        J3DPass::depth(false, NEBULA_COMPARE_LEQUAL, false);
        J3DPass::cull(NEBULA_CULL_NONE);
        J3DPass::positionMatrix(0);
    }

    // runtime/src/render/materials/vertex_color.hlsl: the colour over the screen, blended by its alpha.
    void drawScreenColor(const NebulaColor8& rColor) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_VERTEX_COLOR, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.constants[4] = 0.0f;
        NativeShader::send(material);
        const f32 width = getScreenWidth();
        const f32 height = ::getScreenHeightInline();
        const u32 color = static_cast< u32 >(rColor.r) << 24 | static_cast< u32 >(rColor.g) << 16 |
                          static_cast< u32 >(rColor.b) << 8 | rColor.a;
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0);
        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::color(color);
        J3DNativeDraw::position(width, 0.0f, 0.0f);
        J3DNativeDraw::color(color);
        J3DNativeDraw::position(width, height, 0.0f);
        J3DNativeDraw::color(color);
        J3DNativeDraw::position(0.0f, height, 0.0f);
        J3DNativeDraw::color(color);
    }

    // runtime/src/render/materials/j2d_picture.hlsl: the texture over the screen.
    void drawScreenPicture(const JUTTexture& rTexture) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_J2D_PICTURE, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        NativeShader::setColor(material, 12, 255, 255, 255, 255);
        material.constants[16] = 1.0f;
        material.constants[17] = rTexture.getTransparency() ? 1.0f : 0.0f;
        NativeShader::setTexture(material, 0, &rTexture);
        NativeShader::send(material);
        const f32 width = getScreenWidth();
        const f32 height = ::getScreenHeightInline();
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0);
        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::color(0xFFFFFFFFu);
        J3DNativeDraw::texCoord(0.0f, 0.0f);
        J3DNativeDraw::position(width, 0.0f, 0.0f);
        J3DNativeDraw::color(0xFFFFFFFFu);
        J3DNativeDraw::texCoord(1.0f, 0.0f);
        J3DNativeDraw::position(width, height, 0.0f);
        J3DNativeDraw::color(0xFFFFFFFFu);
        J3DNativeDraw::texCoord(1.0f, 1.0f);
        J3DNativeDraw::position(0.0f, height, 0.0f);
        J3DNativeDraw::color(0xFFFFFFFFu);
        J3DNativeDraw::texCoord(0.0f, 1.0f);
    }

    // Across the game's screen units, wider than the canvas: the star pointer's trail and the cursor it follows.
    void loadProjectionMtxFor2D() {
        MR::setupScreen2D();
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, ::getScreenHeightInline(), 0.0f, MR::getScreenWidth(), -30000.0f, 30000.0f);
        J3DNativeView::setOrthographic(projection);
    }

    void setupDrawForNW4RLayout(f32 scale, bool) {
        f32 height = MR::getScreenHeight();
        f32 width = NebulaFrameClock::cLayoutWidth;
        // The layout canvas widens with the picture; under 16:9 the layouts keep their width and gain height.
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            height *= MR::getDisplay().vertical_fit;
            width = MR::getDisplay().layout_width;
        }
        f32 halfWidth = width / 2.0f;
        height *= 0.5f;
        halfWidth *= scale;
        height *= scale;
        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, height, -height, -halfWidth, halfWidth, -1000.0f, 1000.0f);
        J3DNativeView::setOrthographic(projMtx);
        J3DPass::cull(NEBULA_CULL_NONE);
        J3DPass::depth(false, NEBULA_COMPARE_NEVER, false);
    }

    // The frame's depth cleared to far, with the view, matrix and pixel state the next draws start from.
    void clearZBuffer() {
        const f32 width = MR::getScreenWidth();
        const f32 height = NebulaFrameClock::cScreenLines;
        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, 0.0f, height, 0.0f, width, 0.0f, 1.0f);
        J3DNativeView::setOrthographic(projMtx);
        MR::setDefaultViewportAndScissor();
        TMtx34f mtxImm;
        mtxImm.identity();
        J3DNativeShape::setPosition(0, mtxImm);
        J3DPass::positionMatrix(0);
        J3DPass::noBlend();
        J3DPass::writeAlpha(false);
        J3DPass::depth(true, NEBULA_COMPARE_ALWAYS, true);
        J3DPass::cull(NEBULA_CULL_BACK);
        J3DNativeTarget::clearFrame(0, NEBULA_CLEAR_DEPTH);
        J3DPass::writeColor(true);
    }

    // The stencil zero over the whole screen.
    void clearStencil() {
        const f32 width = MR::getScreenWidth();
        const f32 height = NebulaFrameClock::cScreenLines;
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, ::getScreenHeightInline(), 0.0f, getScreenWidth(), -1.0f, 1.0f);
        J3DNativeView::setOrthographic(projection);
        J3DPass::positionMatrix(0);
        Mtx matrix;
        NebulaMtxIdentity(matrix);
        J3DNativeShape::setPosition(0, matrix);
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_FLAT_COLOR, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.color_write = 0;
        material.alpha_write = 0;
        NativeShader::setColor(material, 0, 0, 0, 0, 0);
        NativeShader::send(material);
        J3DPass::stencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
        J3DNativeShape::setCurrentIdentity();

        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4, 0);
        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::position(width, 0.0f, 0.0f);
        J3DNativeDraw::position(0.0f, height, 0.0f);
        J3DNativeDraw::position(width, height, 0.0f);

        J3DPass::stencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);
    }

    void fillScreenSetup(const NebulaColor8& rColor) {
        TMtx34f mtxImm;
        mtxImm.identity();
        J3DNativeShape::setPosition(0, mtxImm);
        J3DPass::positionMatrix(0);
        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, 0.0f, ::getScreenHeightInline(), 0.0f, MR::getScreenWidth(), -1.0f, 1.0f);
        J3DNativeView::setOrthographic(projMtx);
        // runtime/src/render/materials/flat_color.hlsl in the colour, depth off, no culling; the blending and writes
        // the drawing state has (its callers set them).
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_FLAT_COLOR, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.blend_op = NEBULA_NATIVE_KEPT;
        NativeShader::setColor(material, 0, rColor.r, rColor.g, rColor.b, rColor.a);
        NativeShader::send(material);
        J3DNativeShape::setCurrentIdentity();
    }

    void fillScreenArea(const TVec2s& rMin, const TVec2s& rMax) {
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, 4, 0);
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

    void fillScreen(const NebulaColor8& color) {
        fillScreenSetup(color);
        u16 width = getScreenWidth();
        u16 height = getScreenHeight();
        fillScreenArea(TVec2s(0, 0), TVec2s(width, height));
    }

    void setupShadowVolumeDraw() {
        // The volumes draw in a flat colour each sets (setShadowVolumeColor).
        J3DPass::depth(true, NEBULA_COMPARE_GEQUAL, false);
    }

    void setShadowVolumeColor(const NebulaColor8& rColor) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_FLAT_COLOR, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.blend_op = NEBULA_NATIVE_KEPT;
        material.depth_test = NEBULA_NATIVE_KEPT;
        material.cull = NEBULA_NATIVE_KEPT;
        NativeShader::setColor(material, 0, rColor.r, rColor.g, rColor.b, rColor.a);
        NativeShader::send(material);
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
        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, 0.0f, MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), -1.0f, 1.0f);
        J3DNativeView::setOrthographic(projMtx);
        TMtx34f mtxImm;
        mtxImm.identity();
        J3DNativeShape::setPosition(0, mtxImm);
        // Half black where the volumes and marks left the stencil set, and the stencil cleared there.
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_FLAT_COLOR, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.color_write = 1;
        NativeShader::setColor(material, 0, 0, 0, 0, 0x80);
        NativeShader::send(material);
        J3DPass::stencil(NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);
        J3DNativeShape::setCurrentIdentity();

        const f32 width = MR::getScreenWidth();
        const f32 height = MR::getScreenHeight();
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4, 0);
        J3DNativeDraw::position(0.0f, 0.0f, 0.0f);
        J3DNativeDraw::position(width, 0.0f, 0.0f);
        J3DNativeDraw::position(0.0f, height, 0.0f);
        J3DNativeDraw::position(width, height, 0.0f);

        J3DPass::stencil(NEBULA_STENCIL_OFF, NEBULA_STENCIL_KEEP);
        J3DPass::writeAlpha(false);
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
        TPos3f matrix;
        calcTexProjectionMtx(&matrix);
        J3DNativeShape::setTexture((id - J3D_TEXMTX0) / 3, matrix, 3);
    }

    void calcTexProjectionMtx(TPos3f* pMtx) {
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
        MR::multMtx(*pMtx, matrix, mat);
    }

    void simpleDraw(J3DModel* pModel, J3DMaterial* pMaterial) {
        if (pMaterial != nullptr && pMaterial->mShape != nullptr && !pMaterial->mShape->checkFlag(1)) {
            J3DMatPacket* pMatPacket = pModel->getMatPacket(pMaterial->mIndex);
            J3DShapePacket* pShapePacket = pModel->getShapePacket(pMaterial->mShape->mIndex);
            j3dSys.setMatPacket(pMatPacket);
            J3DNativeMaterial::send(pMatPacket->getMaterial());
            pShapePacket->getShape()->loadPreDrawSetting();

            J3DNativeMaterial::sendOwn(pShapePacket->mpMaterial, pMatPacket->getMaterial());

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

