#include <JSystem/J3DGraphBase/J3DNativeTarget.hpp>
#include "Game/Player/MarioShadow.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeView.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/J3DModelX.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DirectDraw.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include <JSystem/J3DGraphBase/J3DShape.hpp>
#include <JSystem/J3DGraphBase/J3DSys.hpp>
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
    _2FC = 0;
    _2FE = 0;
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
    _340 = nullptr;
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
    _60 = new (32) TVec3f[_58 * _5A];
    _64 = new (32) TVec2f[_58 * _5A];
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

    createMesh();
}

void CollisionShadow::create(const TVec3f& rPosition, const TVec3f& rDirection, const TVec3f& rUp) {
    TVec3f corners[8];
    TVec3f blended;
    TVec3f vertical(rUp);
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
    TVec3f side;
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

    TVec3f minimum;
    TVec3f maximum;
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
        TVec3f normal(*_32C[i].getNormal(0));
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
            const TVec3f& point = *_32C[_5C[i]].getPos(j);
            u32 k;
            for (k = 0; k < _6C; k++) {
                if (MR::abs(_60[k].x - point.x) < tolerance && MR::abs(_60[k].y - point.y) < tolerance && MR::abs(_60[k].z - point.z) < tolerance) {
                    break;
                }
            }

            if (k == _6C) {
                const TVec3f& normal = *_32C[_5C[i]].getNormal(0);
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
        TVec3f relative(_60[i] - rPosition);
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
    void initShadowMaterial(NebulaNativeMaterial* pMaterial, const JUTTexture* pTexture, u32 test, u8 ref) {
        TDDraw::setupNative(pMaterial, 1, 1, 0);
        pMaterial->shader = NEBULA_SHADER_COLOR_TEX_ALPHA;
        pMaterial->constants[4] = ref;
        pMaterial->constants[5] = static_cast< f32 >(test);
        NativeShader::setTexture(*pMaterial, 0, pTexture);
    }
}  // namespace

void CollisionShadow::draw1() const {
    if (_308 & 8) {
        return;
    }

    NebulaNativeMaterial material;
    initShadowMaterial(&material, _300, 0, 1);
    NativeShader::setDepth(material, true, NEBULA_COMPARE_GEQUAL, false);
    material.alpha_write = 1;
    J3DPass::constantAlpha(true, 64);
    NativeShader::send(material);
    sendVtx();
    J3DPass::constantAlpha(false, 0);
}

void CollisionShadow::sendVtx() const {
    if (!_54) {
        return;
    }

    u16 vertexCount = _54 * _5A;
    if (_5A == 4) {
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, vertexCount, NEBULA_SHAPE_TEXCOORD0);
    } else {
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLES, vertexCount, NEBULA_SHAPE_TEXCOORD0);
    }

    const TVec2f* pTex;
    u32 index = 0;
    for (u32 i = 0; i < _54; i++) {
        for (u32 j = 0; j < _5A; j++) {
            const TVec3f& point = getDrawPos(index);
            pTex = &_64[_68[index]];
            index++;
            J3DNativeDraw::position(point.x, point.y, point.z);
            J3DNativeDraw::texCoord(pTex->x, pTex->y);
        }
    }
}

void CollisionShadow::sendZsortedVtx(bool offset) const {
    u16 vertexCount = _54 * _5A;
    if (_5A == 4) {
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, vertexCount, NEBULA_SHAPE_TEXCOORD0);
    } else {
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLES, vertexCount, NEBULA_SHAPE_TEXCOORD0);
    }

    TVec3f shift(-_24 * 5.0f);
    for (u32 i = 0; i < _54; i++) {
        for (u32 j = 0; j < _5A; j++) {
            const TVec3f& point = getDrawPos(i, j);
            const TVec2f& tex = getDrawTx(i, j);
            if (offset) {
                J3DNativeDraw::position(point.x + shift.x, point.y + shift.y, point.z + shift.z);
            } else {
                J3DNativeDraw::position(point.x, point.y, point.z);
            }

            J3DNativeDraw::texCoord(tex.x, tex.y);
        }
    }
}

void CollisionShadow::initCaptureTex() {
    _2FC = 160;
    _2FE = 160;
    {
        MR::CurrentHeapRestorer restorer(MR::getSceneHeap());
        _300 = new JUTTexture(_2FC, _2FE);
        MR::zeroMemory(_300->mImage, _2FC * _2FE);
        MR::setMarioShadowTex(_300);
    }

    _300->mWrapS = NEBULA_WRAP_CLAMP;
    _300->mWrapT = NEBULA_WRAP_CLAMP;
    _300->mMinType = NEBULA_FILTER_LINEAR;
    _300->mMagType = NEBULA_FILTER_LINEAR;
    _300->init();
}

void CollisionShadow::setViewMtx(const TVec3f& rDirection) {
    TVec3f normal = rDirection;
    MR::normalize(&normal);
    TVec3f eye = _2F0 - normal * 10000.0f;

    TPos3f matrix;
    matrix.identity();
    matrix.setPositionFromLookAt(eye, _30, _2F0);
    NebulaMtxCopy(matrix, j3dSys.mViewMtx);
    TDDraw::setViewMtx(matrix);
    MR::setMarioShadowVec(rDirection);
}

void CollisionShadow::setUpdateFlag() {
    if (!MR::isNearZero(_24)) {
        _304 = 1;
    }
}

void CollisionShadow::calcView(J3DModelX* pModel, u32 view, J3DModelX* pReference) {
    if (!_304) {
        return;
    }

    const TVec3f& rDirection = _24;
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

void CollisionShadow::drawAndCaptureTex(J3DModelX* pModel, const TVec3f& rPosition) {
    if (!_306) {
        return;
    }

    if (_C == 3) {
        _340 = pModel;
        return;
    }

    // Mario's silhouette, a pass into the shadow texture.
    const ResTIMG* image = _300->getTexInfo();
    const void* destination = image->image();
    J3DNativeTarget::begin(destination, _2FC, _2FE, 0);

    if (!(_308 & 2)) {
        setViewMtx(_24);
        // The model draws with its own materials (the flat alpha pass, list 1 with 14); setup's view only.
        NebulaNativeMaterial view;
        TDDraw::setupNative(&view, 0, 0, 1);
        Mtx44 projection;
        NebulaMtxOrtho(projection, -150.0f, 150.0f, 150.0f, -150.0f, 10.0f, 100000.0f);
        J3DNativeView::setOrthographic(projection);
        if (_307) {
            J3DNativeView::setViewport(-32.0f, -32.0f, 64.0f + _2FC, 64.0f + _2FE, 0.0f, 1.0f);
            J3DNativeView::setScissor(1, 1, _2FC - 2, _2FE - 2);
            pModel->setDrawView(2);
            pModel->mFlags.clear();
            pModel->mFlags._11 = true;
            pModel->mFlags._1E = true;
            pModel->directDraw(nullptr);
        }

        J3DNativeView::setViewport(0.0f, 0.0f, _2FC, _2FE, 0.0f, 1.0f);
        J3DNativeView::setScissor(1, 1, _2FC - 2, _2FE - 2);
        pModel->setDrawView(2);
        pModel->mFlags.clear();
        pModel->mFlags._1E = true;
        pModel->directDraw(nullptr);
        pModel->mFlags._1E = false;
    }

    if (!(_308 & 4)) {
        MR::beginDesignScreenWidth();
        // runtime/src/render/materials/vertex_color.hlsl into the alpha: the shadow faded to the edge of a circle (the
        // alpha times the circle's).
        NebulaNativeMaterial circle;
        TDDraw::setupNative(&circle, 0, 1, 2);
        MR::endDesignScreenWidth();
        circle.color_write = 0;
        circle.alpha_write = 1;
        NativeShader::setBlend(circle, NEBULA_FACTOR_DST_ALPHA, NEBULA_FACTOR_ZERO);
        J3DPass::constantAlpha(false, 0);
        NativeShader::send(circle);
        f32 y = _2FE / 2;
        f32 x = _2FC / 2;
        TVec3f center(x, y, 0.0f);
        MR::beginDesignScreenWidth();
        TDDraw::fix2Dpos(&center);
        MR::endDesignScreenWidth();
        TDDraw::drawFillCircle(center, _2FC / 2, 0xFFFFFFC0, 0, 16);
    }

    J3DNativeTarget::end(destination, NEBULA_TARGET_ALPHA);

    J3DPass::writeAlpha(false);
    J3DPass::writeColor(true);
    MR::setDefaultViewportAndScissor();
    TDDraw::cameraInit3D();
    J3DPass::constantAlpha(false, 0);
}

void CollisionShadow::drawVolumeBox(const TVec3f& rPosition, const TVec3f& rDepth) const {
    TVec3f up(_3C);
    TVec3f side;
    side.cross(_24, up);
    MR::normalizeOrZero(&side);
    // runtime/src/render/materials/flat_color.hlsl into the alpha buffer: the box's front faces added, its back faces
    // subtracted (a full alpha each), depth tested.
    NebulaNativeMaterial material;
    TDDraw::setupNative(&material, 0, 1, 0);
    material.shader = NEBULA_SHADER_FLAT_COLOR;
    NativeShader::setColor(material, 0, 255, 255, 255, 255);
    NativeShader::setDepth(material, true, NEBULA_COMPARE_LEQUAL, false);
    material.color_write = 0;
    material.alpha_write = 1;
    J3DPass::constantAlpha(false, 0);
    NativeShader::setBlend(material, NEBULA_FACTOR_ONE, NEBULA_FACTOR_ONE);
    material.cull = NEBULA_CULL_BACK;
    NativeShader::send(material);
    TDDraw::drawFillBox3D(rPosition, rDepth * 2.0f, side * _1C, up * _18, 0xFFFFFF01);
    material.blend_op = NEBULA_BLEND_SUBTRACT;
    material.cull = NEBULA_CULL_FRONT;
    NativeShader::send(material);
    TDDraw::drawFillBox3D(rPosition, rDepth * 2.0f, side * _1C, up * _18, 0xFFFFFF01);
}

void CollisionShadow::draw() const {
    MR::loadViewMtx();
    MR::loadProjectionMtx();
    switch (_C) {
    case 0:
        draw1();
        break;
    case 1:
        draw2();
        break;
    case 2:
        draw3();
        break;
    case 3:
        drawVolume();
        break;
    }

    J3DPass::writeColor(true);
    J3DPass::writeAlpha(false);
}

void CollisionShadow::draw2() const {
    if (_308 & 8) {
        return;
    }

    TVec3f position(_2F0 - _24 * _30C);
    drawVolumeBox(position, _24 * _20);
    NebulaNativeMaterial material;
    initShadowMaterial(&material, _300, 0, 1);
    material.alpha_write = 1;
    NativeShader::setBlend(material, NEBULA_FACTOR_DST_ALPHA, NEBULA_FACTOR_INV_DST_ALPHA);
    J3DPass::constantAlpha(true, 0);
    NativeShader::send(material);
    sendVtx();
    J3DPass::constantAlpha(false, 0);
}

void CollisionShadow::draw3() const {
    if (_308 & 8) {
        return;
    }

    TVec3f position(_2F0 - _24 * _30C);
    drawVolumeBox(position, _24 * _20);
    // Where the shadow texture is clear, the alpha buffer cleared (depth tested, alpha only, unblended); the faces in
    // front of the volume too (alpha 0); then the shadow over the screen by the destination alpha where it is set.
    NebulaNativeMaterial material;
    initShadowMaterial(&material, _300, 1, 0);
    NativeShader::setDepth(material, true, NEBULA_COMPARE_LEQUAL, false);
    material.color_write = 0;
    material.alpha_write = 1;
    material.blend_op = NEBULA_BLEND_REPLACE;
    material.blend_src = NEBULA_FACTOR_ONE;
    material.blend_dst = NEBULA_FACTOR_ZERO;
    J3DPass::constantAlpha(true, 0);
    NativeShader::send(material);
    sendZsortedVtx(true);
    if (_6E) {
        NebulaNativeMaterial clear = material;
        clear.shader = NEBULA_SHADER_FLAT_COLOR;
        NativeShader::setColor(clear, 0, 0, 0, 0, 0);
        NativeShader::send(clear);
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLES, _6E * 3, NEBULA_SHAPE_TEXCOORD0);
        for (u32 i = 0; i < _6E; i++) {
            for (u32 j = 0; j < 3; j++) {
                const TVec3f& point = *_32C[_70[i]].getPos(j);
                J3DNativeDraw::position(point.x, point.y, point.z);
                J3DNativeDraw::texCoord(-1.0f, -1.0f);
            }
        }
    }

    material.constants[5] = 2.0f;
    NativeShader::setDepth(material, false, NEBULA_COMPARE_ALWAYS, false);
    NativeShader::setBlend(material, NEBULA_FACTOR_DST_ALPHA, NEBULA_FACTOR_INV_DST_ALPHA);
    for (u32 i = 0; i < _54; i++) {
        material.color_write = 0;
        J3DPass::constantAlpha(false, 0);
        NativeShader::send(material);
        if (_5A == 4) {
            J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, _5A, NEBULA_SHAPE_TEXCOORD0);
        } else {
            J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLES, _5A, NEBULA_SHAPE_TEXCOORD0);
        }

        for (u32 j = 0; j < _5A; j++) {
            const TVec3f& point = getDrawPos(i, j);
            const TVec2f& tex = getDrawTx(i, j);
            J3DNativeDraw::position(point.x, point.y, point.z);
            J3DNativeDraw::texCoord(tex.x, tex.y);
        }

        material.color_write = 1;
        J3DPass::constantAlpha(true, 0);
        NativeShader::send(material);
        if (_5A == 4) {
            J3DNativeDraw::begin(NEBULA_PRIMITIVE_QUADS, _5A, NEBULA_SHAPE_TEXCOORD0);
        } else {
            J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLES, _5A, NEBULA_SHAPE_TEXCOORD0);
        }

        for (u32 j = 0; j < _5A; j++) {
            const TVec3f& point = getDrawPos(i, j);
            const TVec2f& tex = getDrawTx(i, j);
            J3DNativeDraw::position(point.x, point.y, point.z);
            J3DNativeDraw::texCoord(tex.x, tex.y);
        }
    }

    J3DPass::constantAlpha(false, 0);
}

void CollisionShadow::createMesh() {
}

void CollisionShadow::drawVolume() const {
    if (_308 & 8) {
        return;
    }

    _340->setDrawView(2);
    TVec3f position(_2F0 - _24 * _30C);
    drawVolumeBox(position, _24 * _20);
    _340->mFlags.clear();
    _340->mFlags._15 = true;
    _340->directDraw(nullptr);
    J3DPass::constantAlpha(false, 0);
}

void CollisionShadow::doSortPolygons() {
    f32 distances[128];
    f32 inverseVertexCount = 0.33333334f;
    for (u32 i = 0; i < _54; i++) {
        TVec3f center(getDrawPos(0));
        center += getDrawPos(1);
        center += getDrawPos(2);
        center.scale(inverseVertexCount);
        TVec3f relative(center);
        relative.sub(_2F0);
        distances[i] = relative.dot(_24);
    }

    MR::sortSmall(_54, distances, _F0);
}

const TVec3f& CollisionShadow::getDrawPos(u32 index) const {
    return _60[_68[index]];
}

const TVec3f& CollisionShadow::getDrawPos(u32 polygon, u32 vertex) const {
    return _60[_68[vertex + _5A * _F0[polygon]]];
}

const TVec2f& CollisionShadow::getDrawTx(u32 polygon, u32 vertex) const {
    return _64[_68[vertex + _5A * _F0[polygon]]];
}
