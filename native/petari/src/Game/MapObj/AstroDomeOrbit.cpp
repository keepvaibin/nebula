#include "Game/MapObj/AstroDomeOrbit.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Map/SphereSelector.hpp"
#include "Game/MapObj/MiniatureGalaxyHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include <nebula_game_api.h>

namespace {
    const static Vec cRotateOutermost = {20.0f, 45.0f, 0.0f};
    const static f32 cRadius[] = {4000.0f, 6200.0f, 8100.0f, 10300.0f, 12000.0f};
    const static f32 cRadiusLastDome[] = {4000.0f, 6700.0f, 9100.0f, 11800.0f};
    const static s32 cDevideNum = 64;
    const static f32 cWidth = 100.0f;
    const static f32 cHeight = 50.0f;
    const static f32 cGalaxyRotateSpeed = -0.05f;
    const static f32 cGalaxyRotateCoordOffset = 230.0f;
    const static f32 cBloomWidth = 131.0f;
    const static f32 cBloomHeight = 60.0f;

    const static Color8 cColor(0x13, 0xB1, 0xFF, 0xFF);
    const static Color8 cBloomColor(0x00, 0xB4, 0x64, 0xFF);
};  // namespace

AstroDomeOrbit::AstroDomeOrbit() : LiveActor("天文ドームの軌道"), mOrbitRadius(5000.0f), mAngle() {
}

void AstroDomeOrbit::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_AstroDomeOrbit);

    MR::invalidateClipping(this);

    MR::createAdaptorAndConnectToDrawBloomModel("天文ドーム軌道ブルーム描画", MR::Functor(this, &AstroDomeOrbit::drawBloom));

    makeActorDead();
}

void AstroDomeOrbit::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    Pos3f rotateMtx;
    SphereSelectorFunction::calcHandledRotateMtx(mRotation, &rotateMtx);
    const SiteMaterial material = loadMaterial(::cColor);
    TypedDraw site(material, rotateMtx, MR::getCameraViewMtx());

    drawCelling(site, ::cWidth, true, ::cHeight);
    drawCelling(site, ::cWidth, false, ::cHeight);

    drawSide(site, ::cWidth, true, ::cHeight);
    drawSide(site, ::cWidth, false, ::cHeight);
}

void AstroDomeOrbit::drawBloom() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    Pos3f rotateMtx;
    SphereSelectorFunction::calcHandledRotateMtx(mRotation, &rotateMtx);
    const SiteMaterial material = loadMaterial(::cBloomColor);
    TypedDraw site(material, rotateMtx, MR::getCameraViewMtx());

    drawCelling(site, ::cBloomWidth, true, ::cBloomHeight);
    drawCelling(site, ::cBloomWidth, false, ::cBloomHeight);

    drawSide(site, ::cBloomWidth, true, ::cBloomHeight);
    drawSide(site, ::cBloomWidth, false, ::cBloomHeight);
}

void AstroDomeOrbit::setup(s32 radiusIdx) {
    s32 miniNum = MiniatureGalaxyFunction::getMiniatureGalaxyNum();
    const f32* radii = ::cRadiusLastDome;

    if (miniNum == 5) {
        radii = ::cRadius;
    }

    mOrbitRadius = radii[radiusIdx];
    mAngle = ::cGalaxyRotateCoordOffset * radiusIdx;

    if (radiusIdx >= 4) {
        mRotation.set(::cRotateOutermost);
    }
}

void AstroDomeOrbit::moveCoord() {
    mAngle = calcRepeatedRotateCoord(::cGalaxyRotateSpeed + mAngle);
}

void AstroDomeOrbit::calcGalaxyPos(Vec3f* pPos) const {
    f32 angle = calcRepeatedRotateCoord(mAngle) * TWO_PI / 360.0f;
    pPos->set< f32 >(MR::cos(angle), 0.0f, MR::sin(angle));
    pPos->mult(mOrbitRadius);
    pPos->add(*pPos, SphereSelectorFunction::getHandleTrans());

    Pos3f rotateMtx;
    SphereSelectorFunction::calcHandledRotateMtx(mRotation, &rotateMtx);
    rotateMtx.mult(*pPos, *pPos);
}

SiteMaterial AstroDomeOrbit::loadMaterial(const Color8& rColor) const {
    // runtime/src/render/materials/flat_color.hlsl: the colour added to the frame, depth tested and not written, back
    // faces culled.
    SiteMaterial material;
    material.init(NEBULA_SHADER_FLAT_COLOR, SiteMaterial::BLEND_ADD, NEBULA_COMPARE_LEQUAL, false,
                       NEBULA_CULL_BACK);
    material.setColor(0, rColor.r, rColor.g, rColor.b, rColor.a);
    return material;
}

void AstroDomeOrbit::drawCelling(TypedDraw& rDraw, f32 width, bool isAddHeight, f32 height) const {
    Vec3f handleTrans;
    handleTrans.set(SphereSelectorFunction::getHandleTrans());

    if (isAddHeight) {
        handleTrans.y += (0.5f * height);
    } else {
        handleTrans.y -= (0.5f * height);
    }

    VertexP* vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 130);

    for (s32 i = 0; i < ::cDevideNum + 1; i++) {
        f32 angle = TWO_PI * i / ::cDevideNum;

        Vec3f rotateVec;
        rotateVec.set< f32 >(MR::cos(angle), 0.0f, MR::sin(angle));

        Vec3f negVec;
        negVec.scale(mOrbitRadius - (0.5f * width), rotateVec);
        negVec.add(handleTrans);

        Vec3f posVec;
        posVec.scale(mOrbitRadius + (0.5f * width), rotateVec);
        posVec.add(handleTrans);

        if (isAddHeight) {
            *vtx++ = {negVec.x, negVec.y, negVec.z};
            *vtx++ = {posVec.x, posVec.y, posVec.z};
        } else {
            *vtx++ = {posVec.x, posVec.y, posVec.z};
            *vtx++ = {negVec.x, negVec.y, negVec.z};
        }
    }
}

void AstroDomeOrbit::drawSide(TypedDraw& rDraw, f32 width, bool isAddWidth, f32 height) const {
    Vec3f handleTrans;
    handleTrans.set(SphereSelectorFunction::getHandleTrans());
    handleTrans.y += 0.5f * height;

    f32 scale;
    if (isAddWidth) {
        scale = mOrbitRadius + (0.5f * width);
    } else {
        scale = mOrbitRadius - (0.5f * width);
    }

    VertexP* vtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, (::cDevideNum + 1) * 2);

    for (s32 i = 0; i < ::cDevideNum + 1; i++) {
        f32 angle = TWO_PI * i / ::cDevideNum;

        Vec3f rotateVec;
        rotateVec.set< f32 >(MR::cos(angle), 0.0f, MR::sin(angle));

        Vec3f v16;
        v16.scale(scale, rotateVec);
        v16.add(handleTrans);

        Vec3f v15;
        v15.set(v16);
        v15.y -= height;

        if (isAddWidth) {
            *vtx++ = {v16.x, v16.y, v16.z};
            *vtx++ = {v15.x, v15.y, v15.z};
        } else {
            *vtx++ = {v15.x, v15.y, v15.z};
            *vtx++ = {v16.x, v16.y, v16.z};
        }
    }
}

f32 AstroDomeOrbit::calcRepeatedRotateCoord(f32 coord) const {
    return MR::repeatDegree(coord);
}

AstroDomeOrbit::~AstroDomeOrbit() {
}
