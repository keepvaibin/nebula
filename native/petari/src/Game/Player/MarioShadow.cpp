#include "Game/Screen/ImageEffectLocalUtil.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Player/MarioShadow.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/ModelData.hpp"
#include "Game/Render/Model.hpp"
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <cstring>

namespace {
    f32 cCheckOffset0 = 5.0f;
    f32 cDrawOffset0 = -5.0f;
};  // namespace

CollisionShadow::~CollisionShadow() {
}

void CollisionShadow::setMode(u32 mode) {
    if (_C == mode) {
        return;
    }

    switch (mode) {
    case 0:
        _30C = ::cCheckOffset0;
        _310 = ::cDrawOffset0;
        break;
    case 1:
        _30C = 50.0f;
        _310 = -150.0f;
        break;
    case 2:
        _30C = 18.0f;
        _310 = 0.0f;
        break;
    }

    _C = mode;
}

CollisionShadow::CollisionShadow(f32 radius, f32 length) : NameObj("\223\212\211\145\203\126\203\203\203\150\203\105" /* 投影シャドウ */) {
    _2F0.zero();
    _300 = nullptr;
    _305 = false;
    _30C = 0.0f;
    _310 = 0.0f;
    _314.zero();
    _320.zero();
    _32C = nullptr;
    _330 = nullptr;
    _334 = nullptr;
    _338 = 0;
    _33A = 0;
    _33C = 0;
    _308 = 0;
    _E = 0;
    _14 = radius;
    _18 = 0.0f;
    _1C = 0.0f;
    _24.zero();
    _30.zero();
    _3C.zero();
    _48.zero();
    _10 = radius;
    _20 = length;
    _54 = 0;
    _C = 255;
    setMode(0);
    initCaptureTex();
    _58 = 128;
    _5A = 3;
    _60 = new Vec3f[_58 * _5A];
    _64 = new Vec2f[_58 * _5A];
    _68 = new u16[_58 * _5A];
    _5C = new u8[_58];
    _6C = 0;
    _32C = new Triangle[_58];
    _330 = new Triangle[2];
    _334 = new Triangle[2];
    _338 = 0;
    _33A = 0;
    _33C = 0;
    _6E = 0;
    _306 = 0;
    _304 = 0;
    _307 = 1;

    for (u32 i = 0; i < 128; i++) {
        _70[i] = 0;
    }

    for (u32 i = 0; i < 128; i++) {
        _F0[i] = 0;
    }

}

void CollisionShadow::create(const Vec3f& rPosition, const Vec3f& rDirection, const Vec3f& rUp) {
    Vec3f corners[8];
    Vec3f blended;
    Vec3f vertical(rUp);
    bool success = MR::vecBlendSphere(_24, rDirection, &blended, 0.1f);
    if (MR::isNearZero(_24) || !success || _305) {
        blended = rDirection;
    }

    _305 = false;
    if (!MR::isNearZero(blended)) {
        _24 = blended;
        MR::vecKillElement(vertical, blended, &vertical);
        MR::normalizeOrZero(&vertical);
        if (!MR::isNearZero(vertical)) {
            _30 = vertical;
        }
    }

    _2F0 = rPosition;
    if (_C == 3) {
        return;
    }

    _E++;
    Vec3f side;
    side.cross(vertical, blended);
    if (MR::isNearZero(side)) {
        return;
    }

    MR::normalize(&side);
    for (u32 i = 0; i < 8; i++) {
        corners[i] = rPosition;
        if (i & 1) {
            corners[i] += vertical * _10;
        } else {
            corners[i] -= vertical * _10;
        }

        if (i & 2) {
            corners[i] += side * _10;
        } else {
            corners[i] -= side * _10;
        }

        if (i & 4) {
            corners[i] -= blended * _30C;
        } else {
            corners[i] += blended * _20;
        }
    }

    Vec3f minimum;
    Vec3f maximum;
    MR::createBoundingBox(corners, 8, &minimum, &maximum);
    _314 = minimum;
    _320 = maximum;
    if (MR::isInitializeStatePlacementSomething()) {
        return;
    }

    u32 count = MR::createAreaPolygonListArray(_32C, _58, corners, 8);
    for (u32 i = 0; i < _338; i++) {
        _32C[count] = _330[i];
        count++;
    }

    _338 = 0;
    for (u32 i = 0; i < _33A; i++) {
        _32C[count] = _334[i];
        count++;
    }

    _33A = 0;
    _33C = count;
    _54 = 0;
    if (!count) {
        _10 = _14;
        return;
    }

    u32 polygonCount = 0;
    _6E = 0;
    for (u32 i = 0; i < count; i++) {
        Vec3f normal(*_32C[i].getNormal(0));
        f32 facing = normal.dot(_24);
        if (facing > 0.0f) {
            continue;
        }

        if (normal.dot(MR::getCamZdir()) > 0.707f) {
            continue;
        }

        if (MR::isNearZero(facing, 0.15f)) {
            _70[_6E] = i;
            _6E++;
        } else {
            const char* code = MR::getFloorCodeString(&_32C[i]);
            if (code == nullptr || (strcmp(code, "PullBack") != 0 && strcmp(code, "Glass") != 0)) {
                _5C[polygonCount] = i;
                polygonCount++;
            }
        }
    }

    _54 = polygonCount;
    f32 tolerance = 2.0f + _310;
    _6C = 0;
    s32 index = 0;
    for (u32 i = 0; i < _54; i++) {
        for (u32 j = 0; j < _5A; j++) {
            const Vec3f& point = *_32C[_5C[i]].getPos(j);
            u32 k;
            for (k = 0; k < _6C; k++) {
                if (MR::abs(_60[k].x - point.x) < tolerance && MR::abs(_60[k].y - point.y) < tolerance && MR::abs(_60[k].z - point.z) < tolerance) {
                    break;
                }
            }

            if (k == _6C) {
                const Vec3f& normal = *_32C[_5C[i]].getNormal(0);
                _60[_6C] = point + normal * _310;
                _6C++;
            }

            _68[index] = k;
            index++;
        }
    }

    // TODO is this legit?
    register f32 texScale = 0.0033333334f;

    for (u32 i = 0; i < _6C; i++) {
        Vec3f relative(_60[i] - rPosition);
        f32 s = relative.dot(side);
        f32 t = relative.dot(_30);

        _64[i].x = 0.5f + s * texScale;
        _64[i].y = 0.5f + t * texScale;
    }

    if (_33C > 40) {
        _10 -= 10.0f;
    } else if (_33C > 10) {
        _10 -= 1.0f;
    } else if (_33C > 20) {
        _10 -= 4.0f;
    } else {
        _10 += 1.0f;
    }

    MR::clampBoth(&_10, 1.0f, _14);
}

namespace {
    // runtime/src/render/materials/color_tex_alpha.hlsl: black at the shadow texture's alpha where it passes `test`
    // (0 above, 1 equal to, 2 not equal to `ref`), blended by alpha unless the site changes it; no culling.
    void initShadowMaterial(SiteMaterial* pMaterial, const JUTTexture* pTexture, u32 test, u8 ref) {
        TDDraw::setupNative(pMaterial, 1, 1, 0);
        pMaterial->mShader = NEBULA_SHADER_COLOR_TEX_ALPHA;
        pMaterial->constants[4] = ref;
        pMaterial->constants[5] = static_cast< f32 >(test);
        pMaterial->setTexture(0, pTexture);
    }
}  // namespace

// On a wall (a Bee's walk): marked where the shadow's texture shows, behind what is there; the screen blend darkens it.
void CollisionShadow::draw1() const {
    if (_308 & 8) {
        return;
    }

    SiteMaterial material;
    initShadowMaterial(&material, _300, 0, 1);
    material.setDepth(true, NEBULA_COMPARE_GEQUAL, false);
    material.mState.color_write = 0;
    material.mState.alpha_write = 0;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    sendVtx(site);
}

void CollisionShadow::sendVtx(TypedDraw& rDraw) const {
    if (!_54) {
        return;
    }

    u16 vertexCount = _54 * _5A;
    VertexPT* vtx = rDraw.vertices< VertexPT >(_5A == 4 ? NEBULA_PRIMITIVE_QUADS : NEBULA_PRIMITIVE_TRIANGLES, vertexCount);

    const Vec2f* pTex;
    u32 index = 0;
    for (u32 i = 0; i < _54; i++) {
        for (u32 j = 0; j < _5A; j++) {
            const Vec3f& point = getDrawPos(index);
            pTex = &_64[_68[index]];
            index++;
            *vtx++ = {point.x, point.y, point.z, pTex->x, pTex->y};
        }
    }
}

void CollisionShadow::sendZsortedVtx(TypedDraw& rDraw, bool offset) const {
    u16 vertexCount = _54 * _5A;
    VertexPT* vtx = rDraw.vertices< VertexPT >(_5A == 4 ? NEBULA_PRIMITIVE_QUADS : NEBULA_PRIMITIVE_TRIANGLES, vertexCount);

    Vec3f shift(-_24 * 5.0f);
    for (u32 i = 0; i < _54; i++) {
        for (u32 j = 0; j < _5A; j++) {
            const Vec3f& point = getDrawPos(i, j);
            const Vec2f& tex = getDrawTx(i, j);
            if (offset) {
                *vtx++ = {point.x + shift.x, point.y + shift.y, point.z + shift.z, tex.x, tex.y};
            } else {
                *vtx++ = {point.x, point.y, point.z, tex.x, tex.y};
            }
        }
    }
}

void CollisionShadow::initCaptureTex() {
    {
        MR::CurrentHeapRestorer restorer(MR::getSceneHeap());
        _300 = JUTTexture::createTarget(0.25f, 0.35f);
        MR::setMarioShadowTex(_300);
    }

    _300->mWrapS = NEBULA_WRAP_CLAMP;
    _300->mWrapT = NEBULA_WRAP_CLAMP;
    _300->mMinType = NEBULA_FILTER_LINEAR;
    _300->mMagType = NEBULA_FILTER_LINEAR;
    _300->init();
}

void CollisionShadow::setViewMtx(const Vec3f& rDirection) {
    Vec3f normal = rDirection;
    MR::normalize(&normal);
    Vec3f eye = _2F0 - normal * 10000.0f;

    Pos3f matrix;
    matrix.identity();
    matrix.setPositionFromLookAt(eye, _30, _2F0);
    NebulaMtxCopy(matrix, ModelSys::mViewMtx);
    MR::setMarioShadowVec(rDirection);
}

void CollisionShadow::setUpdateFlag() {
    if (!MR::isNearZero(_24)) {
        _304 = 1;
    }
}

void CollisionShadow::calcView(PlayerModel* pModel, u32 view, PlayerModel* pReference) {
    if (!_304) {
        return;
    }

    const Vec3f& rDirection = _24;
    _304 = 0;
    _306 = 1;
    if (_C == 3) {
        Mtx matrix;
        NebulaMtxCopy(pReference->getBaseTRMtx(), matrix);
        if (pReference != nullptr) {
            pModel->viewCalcRefPos(view, pReference, _48, _24);
        }
    } else {
        setViewMtx(rDirection);
        if (pReference != nullptr) {
            pModel->viewCalcRef(view, pReference);
        } else {
            pModel->viewCalc3(view, nullptr);
        }
    }

    if (_C == 2) {
        doSortPolygons();
    }
}

void CollisionShadow::drawAndCaptureTex(PlayerModel* pModel, const Vec3f& rPosition) {
    if (!_306) {
        return;
    }

    // Mario's silhouette, a pass into the shadow texture.
    const ResTIMG* image = _300->getTexInfo();
    const void* destination = image->image();
    Frame::targetBegin(destination, _300->mShareAcross, _300->mShareDown, 0);
    // A pixel of the picture's edge stays clear for the texture's clamp.
    const f32 cEdge = 1.0f / 160.0f;

    if (!(_308 & 2)) {
        setViewMtx(_24);
        // The model draws with its own materials (the flat alpha pass, list 1 with 14); setup's view only.
        SiteMaterial view;
        TDDraw::setupNative(&view, 0, 0, 1);
        Mtx44 projection;
        NebulaMtxOrtho(projection, -150.0f, 150.0f, 150.0f, -150.0f, 10.0f, 100000.0f);
        Frame::setOrthographic(projection);
        if (_307) {
            Frame::setViewport(-0.2f, -0.2f, 1.4f, 1.4f, 0.0f, 1.0f);
            Frame::setScissor(cEdge, cEdge, 1.0f - 2.0f * cEdge, 1.0f - 2.0f * cEdge);
            pModel->setDrawView(2);
            pModel->mFlags.clear();
            pModel->mFlags._11 = true;
            pModel->mFlags._1E = true;
            pModel->directDraw();
        }

        Frame::setViewport(0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
        Frame::setScissor(cEdge, cEdge, 1.0f - 2.0f * cEdge, 1.0f - 2.0f * cEdge);
        pModel->setDrawView(2);
        pModel->mFlags.clear();
        pModel->mFlags._1E = true;
        pModel->directDraw();
        pModel->mFlags._1E = false;
    }

    if (!(_308 & 4)) {
        // runtime/src/render/materials/vertex_color.hlsl into the alpha: the shadow faded to the edge (its alpha times the circle's).
        SiteMaterial circle;
        TDDraw::setupNative(&circle, 0, 1, 2);
        ImageEffectLocalUtil::setupDrawTexture();
        circle.mState.color_write = 0;
        circle.mState.alpha_write = 1;
        circle.setBlend(NEBULA_FACTOR_DST_ALPHA, NEBULA_FACTOR_ZERO);
        TypedDraw site = TypedDraw::eyeSpace(circle, cIdentityMtx);
    VertexPC* vtx = site.vertices< VertexPC >(NEBULA_PRIMITIVE_TRIANGLE_FAN, 18);
        *vtx++ = {0.5f, 0.5f, 0.0f, Rgba8(0xFFFFFFC0u)};
        for (u32 i = 0; i <= 16; i++) {
            const f32 angle = 2.0f * (static_cast< f32 >(i) / 16.0f) * PI;
            *vtx++ = {0.5f - 0.385f * MR::cos(angle), 0.5f + 0.5f * MR::sin(angle), 0.0f, Rgba8(0u)};
        }
    }

    Frame::targetEnd(destination, NEBULA_TARGET_ALPHA);

    MR::setDefaultViewportAndScissor();
    TDDraw::cameraInit3D();
}

void CollisionShadow::drawVolumeBox(const Vec3f& rPosition, const Vec3f& rDepth) const {
    Vec3f up(_3C);
    Vec3f side;
    side.cross(_24, up);
    MR::normalizeOrZero(&side);
    // The box's faces in the stencil, depth tested: what its front faces reach and its back faces do not.
    SiteMaterial material;
    TDDraw::setupNative(&material, 0, 1, 0);
    material.mShader = NEBULA_SHADER_FLAT_COLOR;
    material.setColor(0, 255, 255, 255, 255);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    material.mState.color_write = 0;
    material.mState.alpha_write = 0;
    material.mState.blend_op = NEBULA_BLEND_REPLACE;
    material.mState.blend_src = NEBULA_FACTOR_ONE;
    material.mState.blend_dst = NEBULA_FACTOR_ZERO;
    material.mState.cull = NEBULA_CULL_BACK;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ADD);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    TDDraw::drawFillBox3D(site, rPosition, rDepth * 2.0f, side * _1C, up * _18, 0xFFFFFF01);
    material.mState.cull = NEBULA_CULL_FRONT;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_TAKE);
    site.setMaterial(material);
    TDDraw::drawFillBox3D(site, rPosition, rDepth * 2.0f, side * _1C, up * _18, 0xFFFFFF01);
}

void CollisionShadow::draw() const {
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    if (_C == 0) {
        draw1();
    } else {
        draw3();
    }
}

// On the ground: the shadow's polygons inside its box, each pixel darkened once.
void CollisionShadow::draw3() const {
    if (_308 & 8) {
        return;
    }

    Vec3f position(_2F0 - _24 * _30C);
    drawVolumeBox(position, _24 * _20);
    // Where the shadow's texture is clear, unmarked; and the faces in front of the volume.
    SiteMaterial material;
    initShadowMaterial(&material, _300, 1, 0);
    material.setDepth(true, NEBULA_COMPARE_LEQUAL, false);
    material.mState.color_write = 0;
    material.mState.alpha_write = 0;
    material.mState.blend_op = NEBULA_BLEND_REPLACE;
    material.mState.blend_src = NEBULA_FACTOR_ONE;
    material.mState.blend_dst = NEBULA_FACTOR_ZERO;
    material.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    sendZsortedVtx(site, true);
    if (_6E) {
        SiteMaterial clear = material;
        clear.mShader = NEBULA_SHADER_FLAT_COLOR;
        clear.setColor(0, 0, 0, 0, 0);
        site.setMaterial(clear);
        VertexPT* vtx = site.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLES, _6E * 3);
        for (u32 i = 0; i < _6E; i++) {
            for (u32 j = 0; j < 3; j++) {
                const Vec3f& point = *_32C[_70[i]].getPos(j);
                *vtx++ = {point.x, point.y, point.z, -1.0f, -1.0f};
            }
        }
    }

    // The shadow's colour at its texture's alpha where the mark stands, nearest polygon first; the mark goes as it is drawn.
    material.constants[5] = 2.0f;
    material.mState.color_write = 1;
    material.setDepth(false, NEBULA_COMPARE_ALWAYS, false);
    material.setBlend(NEBULA_FACTOR_SRC_ALPHA, NEBULA_FACTOR_INV_SRC_ALPHA);
    material.setStencil(NEBULA_STENCIL_IF_SET, NEBULA_STENCIL_ZERO);
    site.setMaterial(material);
    for (u32 i = 0; i < _54; i++) {
        VertexPT* vtx = site.vertices< VertexPT >(_5A == 4 ? NEBULA_PRIMITIVE_QUADS : NEBULA_PRIMITIVE_TRIANGLES, _5A);

        for (u32 j = 0; j < _5A; j++) {
            const Vec3f& point = getDrawPos(i, j);
            const Vec2f& tex = getDrawTx(i, j);
            *vtx++ = {point.x, point.y, point.z, tex.x, tex.y};
        }
    }

    // What the polygons did not cover.
    SiteMaterial wipe = material;
    wipe.mShader = NEBULA_SHADER_FLAT_COLOR;
    wipe.mState.color_write = 0;
    wipe.mState.blend_op = NEBULA_BLEND_REPLACE;
    wipe.setStencil(NEBULA_STENCIL_ALWAYS, NEBULA_STENCIL_ZERO);
    site.setMaterial(wipe);
    Vec3f up(_3C);
    Vec3f side;
    side.cross(_24, up);
    MR::normalizeOrZero(&side);
    TDDraw::drawFillBox3D(site, position, _24 * _20 * 2.0f, side * _1C, up * _18, 0xFFFFFF01);
}

void CollisionShadow::doSortPolygons() {
    f32 distances[128];
    f32 inverseVertexCount = 0.33333334f;
    for (u32 i = 0; i < _54; i++) {
        Vec3f center(getDrawPos(0));
        center += getDrawPos(1);
        center += getDrawPos(2);
        center.scale(inverseVertexCount);
        Vec3f relative(center);
        relative.sub(_2F0);
        distances[i] = relative.dot(_24);
    }

    MR::sortSmall(_54, distances, _F0);
}

const Vec3f& CollisionShadow::getDrawPos(u32 index) const {
    return _60[_68[index]];
}

const Vec3f& CollisionShadow::getDrawPos(u32 polygon, u32 vertex) const {
    return _60[_68[vertex + _5A * _F0[polygon]]];
}

const Vec2f& CollisionShadow::getDrawTx(u32 polygon, u32 vertex) const {
    return _64[_68[vertex + _5A * _F0[polygon]]];
}
