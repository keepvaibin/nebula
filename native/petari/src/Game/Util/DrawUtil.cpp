#include "Game/Util/DrawUtil.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/RenderTexture.hpp"
#include "Game/Render/Material.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Model.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/NebulaFrameClock.hpp"
#include <NebulaGpuTypes.h>

namespace {
    const f32 cNearZ = -10000.0f;
    const f32 cFarZ = 10000.0f;

    inline s32 getScreenHeightInline() {
        return MR::getScreenHeight();
    }
};  // namespace

const GameTexture* mShadowTex;
Vec3f mShadowVec;

namespace MR {
    void resetDrawState() {
        const NebulaColor8 black = {0, 0, 0, 0};
        MaterialAmbient::set(0, black);
        MaterialAmbient::set(1, black);
    }

    void loadViewMtxFor2DModel() {
        Mtx viewMtx;
        NebulaMtxIdentity(viewMtx);
        NebulaMtxCopy(viewMtx, ModelSys::mViewMtx);
        ModelSys::mViewRole = NEBULA_CAMERA_OTHER;
    }

    void drawInitFor2DModel() {
        MR::resetDrawState();

        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, 0.0f, -MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), ::cNearZ, ::cFarZ);
        Frame::setOrthographic(projMtx);

        Mtx viewMtx;
        NebulaMtxIdentity(viewMtx);
        NebulaMtxCopy(viewMtx, ModelSys::mViewMtx);
        ModelSys::mViewRole = NEBULA_CAMERA_OTHER;
    }

    void setDefaultViewportAndScissor() {
        Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        Frame::setScissor(0.0f, 0.0f, 1.0f, 1.0f);
    }

    // The whole screen, for a fill or a picture over it.
    void setupScreen2D() {
        setDefaultViewportAndScissor();
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, ::getScreenHeightInline(), 0.0f, getScreenWidth(), -30000.0f, 30000.0f);
        Frame::setOrthographic(projection);
    }

    // runtime/src/render/materials/vertex_color.hlsl: the colour over the screen, blended by its alpha.
    void drawScreenColor(const NebulaColor8& rColor) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_VERTEX_COLOR, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.constants[0] = 0.0f;
        const f32 width = getScreenWidth();
        const f32 height = ::getScreenHeightInline();
        const u32 color = static_cast< u32 >(rColor.r) << 24 | static_cast< u32 >(rColor.g) << 16 |
                          static_cast< u32 >(rColor.b) << 8 | rColor.a;
        TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
    VertexPC* vtx = site.vertices< VertexPC >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {0.0f, 0.0f, 0.0f, Rgba8(color)};
        *vtx++ = {width, 0.0f, 0.0f, Rgba8(color)};
        *vtx++ = {width, height, 0.0f, Rgba8(color)};
        *vtx++ = {0.0f, height, 0.0f, Rgba8(color)};
    }

    // runtime/src/render/materials/j2d_picture.hlsl: the texture over the screen.
    void drawScreenPicture(const GameTexture& rTexture) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_J2D_PICTURE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.setColor(12, 255, 255, 255, 255);
        material.constants[16] = 1.0f;
        material.constants[17] = rTexture.info()->mTransparency != 0 ? 1.0f : 0.0f;
        material.setTexture(0, &rTexture);
        const f32 width = getScreenWidth();
        const f32 height = ::getScreenHeightInline();
        TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
    VertexPCT* vtx = site.vertices< VertexPCT >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {0.0f, 0.0f, 0.0f, Rgba8(0xFFFFFFFFu), 0.0f, 0.0f};
        *vtx++ = {width, 0.0f, 0.0f, Rgba8(0xFFFFFFFFu), 1.0f, 0.0f};
        *vtx++ = {width, height, 0.0f, Rgba8(0xFFFFFFFFu), 1.0f, 1.0f};
        *vtx++ = {0.0f, height, 0.0f, Rgba8(0xFFFFFFFFu), 0.0f, 1.0f};
    }

    // Across the game's screen units, wider than the canvas: the star pointer's trail and the cursor it follows.
    void loadProjectionMtxFor2D() {
        MR::setupScreen2D();
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, ::getScreenHeightInline(), 0.0f, MR::getScreenWidth(), -30000.0f, 30000.0f);
        Frame::setOrthographic(projection);
    }

    void setupDrawForLayout() {
        f32 height = MR::getScreenHeight();
        f32 width = NebulaFrameClock::cLayoutWidth;
        // The layout canvas widens with the picture; under 16:9 the layouts keep their width and gain height.
        if (MR::isScreen16Per9() && MR::getDisplay().wide) {
            height *= MR::getDisplay().vertical_fit;
            width = MR::getDisplay().layout_width;
        }
        f32 halfWidth = width / 2.0f;
        height *= 0.5f;
        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, height, -height, -halfWidth, halfWidth, -1000.0f, 1000.0f);
        Frame::setOrthographic(projMtx);
    }

    // The frame's depth cleared to far, with the view, matrix and pixel state the next draws start from.
    void clearZBuffer() {
        const f32 width = MR::getScreenWidth();
        const f32 height = NebulaFrameClock::cScreenLines;
        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, 0.0f, height, 0.0f, width, 0.0f, 1.0f);
        Frame::setOrthographic(projMtx);
        MR::setDefaultViewportAndScissor();
        Frame::clearFrame(0, NEBULA_CLEAR_DEPTH);
    }

    // The stencil zero over the whole screen.
    void clearStencil() {
        const f32 width = MR::getScreenWidth();
        const f32 height = NebulaFrameClock::cScreenLines;
        Mtx44 projection;
        NebulaMtxOrtho(projection, 0.0f, ::getScreenHeightInline(), 0.0f, getScreenWidth(), -1.0f, 1.0f);
        Frame::setOrthographic(projection);
        Mtx matrix;
        NebulaMtxIdentity(matrix);
        SiteMaterial material;
        material.init(NEBULA_SHADER_FLAT_COLOR, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.mState.color_write = 0;
        material.mState.alpha_write = 0;
        material.setColor(0, 0, 0, 0, 0);
        material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
        TypedDraw site = TypedDraw::eyeSpace(material, matrix);

        VertexP* vtx = site.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
        *vtx++ = {0.0f, 0.0f, 0.0f};
        *vtx++ = {width, 0.0f, 0.0f};
        *vtx++ = {0.0f, height, 0.0f};
        *vtx++ = {width, height, 0.0f};
    }

    SiteMaterial fillScreenSetup(const NebulaColor8& rColor, bool opaque) {
        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, 0.0f, ::getScreenHeightInline(), 0.0f, MR::getScreenWidth(), -1.0f, 1.0f);
        Frame::setOrthographic(projMtx);
        // runtime/src/render/materials/flat_color.hlsl in the colour, depth off, no culling; the blending and writes
        // the drawing state has (its callers set them).
        SiteMaterial material;
        material.init(NEBULA_SHADER_FLAT_COLOR, opaque ? SiteMaterial::BLEND_OPAQUE : SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.setColor(0, rColor.r, rColor.g, rColor.b, rColor.a);
        return material;
    }

    void fillScreenArea(TypedDraw& rDraw, const Vec2f& rMin, const Vec2f& rMax) {
        VertexP* vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_QUADS, 4);
        *vtx++ = {rMin.x, rMin.y, 0.0f};
        *vtx++ = {rMax.x, rMin.y, 0.0f};
        *vtx++ = {rMax.x, rMax.y, 0.0f};
        *vtx++ = {rMin.x, rMax.y, 0.0f};
    }

    void fillScreen(const NebulaColor8& color, bool opaque) {
        const SiteMaterial material = fillScreenSetup(color, opaque);
        TypedDraw site = TypedDraw::eyeSpace(material, cIdentityMtx);
        fillScreenArea(site, Vec2f(0.0f, 0.0f), Vec2f(getScreenWidth(), getScreenHeight()));
    }

    // The material of a shadow volume: a flat colour behind what is drawn, nothing but the stencil written.
    void initShadowVolumeMaterial(SiteMaterial& rMaterial, const NebulaColor8& rColor) {
        rMaterial.init(NEBULA_SHADER_FLAT_COLOR, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_GEQUAL, false, NEBULA_CULL_NONE);
        rMaterial.mState.depth_test = 1;
        rMaterial.mState.blend_src = 0;
        rMaterial.mState.blend_dst = 0;
        rMaterial.mState.color_write = 0;
        rMaterial.mState.alpha_write = 0;
        rMaterial.setColor(0, rColor.r, rColor.g, rColor.b, rColor.a);
    }

    void fillSilhouetteColor() {
        Mtx44 projMtx;
        NebulaMtxOrtho(projMtx, 0.0f, MR::getScreenHeight(), 0.0f, MR::getScreenWidth(), -1.0f, 1.0f);
        Frame::setOrthographic(projMtx);
        Mtx34f mtxImm;
        mtxImm.identity();
        // Half black where the volumes and marks left the stencil set, and the stencil cleared there.
        SiteMaterial material;
        material.init(NEBULA_SHADER_FLAT_COLOR, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_ALWAYS, false,
                           NEBULA_CULL_NONE);
        material.mState.color_write = 1;
        material.setColor(0, 0, 0, 0, 0x80);
        material.setStencil(NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);
        TypedDraw site = TypedDraw::eyeSpace(material, mtxImm);

        const f32 width = MR::getScreenWidth();
        const f32 height = MR::getScreenHeight();
        VertexP* vtx = site.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 4);
        *vtx++ = {0.0f, 0.0f, 0.0f};
        *vtx++ = {width, 0.0f, 0.0f};
        *vtx++ = {0.0f, height, 0.0f};
        *vtx++ = {width, height, 0.0f};

    }

    const GameTexture* getMarioShadowTex() {
        return mShadowTex;
    }

    const GameTexture* getMarioShadowTexForLoad() {
        return mShadowTex;
    }

    const Vec3f& getMarioShadowVec() {
        return mShadowVec;
    }

    void setMarioShadowTex(const GameTexture* pShadowTex) {
        mShadowTex = pShadowTex;
    }

    void setMarioShadowVec(const Vec3f& rVec) {
        mShadowVec = rVec;
    }

    void calcTexProjectionMtx(Pos3f* pMtx) {
        Proj3f cameraProjection = MR::getCameraProjectionMtx();
        cameraProjection.mMtx[2][0] = 0.0f;
        cameraProjection.mMtx[2][1] = 0.0f;
        cameraProjection.mMtx[2][2] = -1.0f;
        cameraProjection.mMtx[2][3] = 0.0f;
        cameraProjection.mMtx[3][0] = 0.0f;
        cameraProjection.mMtx[3][1] = 0.0f;
        cameraProjection.mMtx[3][2] = 0.0f;
        cameraProjection.mMtx[3][3] = 1.0f;
        Pos3f matrix;
        MR::multMtx(matrix, MR::getCameraViewMtx(), cameraProjection);
        Pos3f mat;
        mat.identity();
        mat[0][0] = 0.5f;
        mat[0][2] = 0.5f;
        mat[1][1] = -0.5f;
        mat[1][2] = 0.5f;
        MR::multMtx(*pMtx, matrix, mat);
    }

    void simpleDraw(Model* pModel, Material* pMaterial) {
        if (pMaterial != nullptr && pMaterial->getMesh() != nullptr && !pMaterial->getMesh()->isHidden()) {
            MaterialEntry* pMatPacket = pModel->getMaterialEntry(pMaterial->getIndex());
            MeshEntry* pShapePacket = pModel->getMeshEntry(pMaterial->getMesh()->getIndex());
            MaterialDraw draw(pMatPacket->getMaterial());
            draw.sendOwn(pShapePacket->mMaterial, pMatPacket->getMaterial());
            if (!pShapePacket->isHidden()) {
                draw.draw(pModel, pShapePacket->getMesh());
            }
        }
    }

    void activateGameSceneDraw3D() {
        GameSceneFunction::activateDraw3D();
    }

    void deactivateGameSceneDraw3D() {
        GameSceneFunction::deactivateDraw3D();
    }
};  // namespace MR

