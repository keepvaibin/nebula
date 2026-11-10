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
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcGalaxyPos_f1, "calcGalaxyPos.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawFlat_f1, "drawFlat.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawFlat_f2, "drawFlat.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawFlat_f3, "drawFlat.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawFlat_f4, "drawFlat.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSide_f1, "drawSide.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSide_f2, "drawSide.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawSide_f3, "drawSide.f3", 0.5f);

namespace {
    NEBULA_TUNABLE_RECORD(Vec, cRotateOutermost, {20.0f, 45.0f, 0.0f});
    // Orbit radius by galaxy index: five galaxies in a dome, four in the last one.
    NEBULA_TUNABLE_LIST(f32, cRadius, , {4000.0f, 6200.0f, 8100.0f, 10300.0f, 12000.0f});
    NEBULA_TUNABLE_LIST(f32, cRadiusLastDome, , {4000.0f, 6700.0f, 9100.0f, 11800.0f});
    // The fifth ring is tilted.
    NEBULA_TUNABLE(s32, cOutermostIndex, 4);
    const s32 cDivideNum = AstroDomeOrbit::cRingPointNum - 1;
    NEBULA_TUNABLE(f32, cWidth, 100.0f);
    NEBULA_TUNABLE(f32, cHeight, 50.0f);
    NEBULA_TUNABLE(f32, cGalaxyRotateSpeed, -0.05f);
    NEBULA_TUNABLE(f32, cGalaxyRotateCoordOffset, 230.0f);
    NEBULA_TUNABLE(f32, cBloomWidth, 131.0f);
    NEBULA_TUNABLE(f32, cBloomHeight, 60.0f);

    const Color8 cColor(0x13, 0xB1, 0xFF, 0xFF);
    const Color8 cBloomColor(0x00, 0xB4, 0x64, 0xFF);

    // runtime/src/render/materials/flat_color.hlsl: the colour added to the frame, depth tested and not written, back
    // faces culled.
    SiteMaterial loadMaterial(const Color8& rColor) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_FLAT_COLOR, SiteMaterial::BLEND_ADD, NEBULA_COMPARE_LEQUAL, false, NEBULA_CULL_BACK);
        material.setColor(0, rColor.r, rColor.g, rColor.b, rColor.a);
        return material;
    }

    void putVertex(VertexP*& rpVtx, const Vec3f& rPos) {
        *rpVtx++ = {rPos.x, rPos.y, rPos.z};
    }
}  // namespace

AstroDomeOrbit::AstroDomeOrbit() : LiveActor("天文ドームの軌道"), mOrbitRadius(5000.0f), mAngle(), mRingCos(), mRingSin() {
    lumacore::setClass(this, lumacore::kClassId< AstroDomeOrbit >);
}

void AstroDomeOrbit::init(const TableIter& /*rIter*/) {
    MR::connectToScene(this, MR::MovementType_None, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_AstroDomeOrbit);

    MR::invalidateClipping(this);

    MR::createAdaptorAndConnectToDrawBloomModel("天文ドーム軌道ブルーム描画", MR::Functor(this, &AstroDomeOrbit::drawBloom));

    for (s32 i = 0; i < cRingPointNum; i++) {
        f32 angle = TWO_PI * i / ::cDivideNum;
        mRingCos[i] = MR::cos(angle);
        mRingSin[i] = MR::sin(angle);
    }

    makeActorDead();
}

void AstroDomeOrbit::draw() const {
    drawBand(::cColor, ::cWidth, ::cHeight);
}

void AstroDomeOrbit::drawBloom() const {
    drawBand(::cBloomColor, ::cBloomWidth, ::cBloomHeight);
}

void AstroDomeOrbit::setup(s32 galaxyIndex) {
    const bool isFullDome = MiniatureGalaxyFunction::getMiniatureGalaxyNum() == ARRAY_SIZE(::cRadius);
    const f32* pRadii = isFullDome ? ::cRadius : ::cRadiusLastDome;
    const s32 radiusNum = isFullDome ? ARRAY_SIZE(::cRadius) : ARRAY_SIZE(::cRadiusLastDome);

    // A dome with more galaxies than rings puts the extra ones on the outermost ring.
    mOrbitRadius = pRadii[MR::clamp(galaxyIndex, 0, radiusNum - 1)];
    mAngle = ::cGalaxyRotateCoordOffset * galaxyIndex;

    if (galaxyIndex >= ::cOutermostIndex) {
        mRotation.set(::cRotateOutermost);
    }
}

void AstroDomeOrbit::moveCoord() {
    mAngle = MR::repeatDegree(::cGalaxyRotateSpeed + mAngle);
}

void AstroDomeOrbit::calcGalaxyPos(Vec3f* pPos) const {
    f32 angle = MR::repeatDegree(mAngle) * TWO_PI / cNum_calcGalaxyPos_f1;
    pPos->set< f32 >(MR::cos(angle), 0.0f, MR::sin(angle));
    pPos->mult(mOrbitRadius);
    pPos->add(*pPos, SphereSelectorFunction::getHandleTrans());

    Pos3f rotateMtx;
    SphereSelectorFunction::calcHandledRotateMtx(mRotation, &rotateMtx);
    rotateMtx.mult(*pPos, *pPos);
}

// Top and bottom faces, then the outer and inner walls.
void AstroDomeOrbit::drawBand(const Color8& rColor, f32 width, f32 height) const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    Pos3f rotateMtx;
    SphereSelectorFunction::calcHandledRotateMtx(mRotation, &rotateMtx);
    const SiteMaterial material = ::loadMaterial(rColor);
    TypedDraw site(material, rotateMtx, MR::getCameraViewMtx());

    drawFlat(site, width, true, height);
    drawFlat(site, width, false, height);

    drawSide(site, width, true, height);
    drawSide(site, width, false, height);
}

// The strip's vertex order picks the face culling keeps.
void AstroDomeOrbit::drawFlat(TypedDraw& rDraw, f32 width, bool isTop, f32 height) const {
    Vec3f center;
    center.set(SphereSelectorFunction::getHandleTrans());

    if (isTop) {
        center.y += (cNum_drawFlat_f1 * height);
    } else {
        center.y -= (cNum_drawFlat_f2 * height);
    }

    VertexP* pVtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, cRingPointNum * 2);

    for (s32 i = 0; i < cRingPointNum; i++) {
        Vec3f dir;
        dir.set< f32 >(mRingCos[i], 0.0f, mRingSin[i]);

        Vec3f inner;
        inner.scale(mOrbitRadius - (cNum_drawFlat_f3 * width), dir);
        inner.add(center);

        Vec3f outer;
        outer.scale(mOrbitRadius + (cNum_drawFlat_f4 * width), dir);
        outer.add(center);

        if (isTop) {
            ::putVertex(pVtx, inner);
            ::putVertex(pVtx, outer);
        } else {
            ::putVertex(pVtx, outer);
            ::putVertex(pVtx, inner);
        }
    }
}

void AstroDomeOrbit::drawSide(TypedDraw& rDraw, f32 width, bool isOuter, f32 height) const {
    Vec3f center;
    center.set(SphereSelectorFunction::getHandleTrans());
    center.y += cNum_drawSide_f1 * height;

    f32 radius = isOuter ? mOrbitRadius + (cNum_drawSide_f2 * width) : mOrbitRadius - (cNum_drawSide_f3 * width);

    VertexP* pVtx = rDraw.vertices< VertexP >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, cRingPointNum * 2);

    for (s32 i = 0; i < cRingPointNum; i++) {
        Vec3f dir;
        dir.set< f32 >(mRingCos[i], 0.0f, mRingSin[i]);

        Vec3f top;
        top.scale(radius, dir);
        top.add(center);

        Vec3f bottom;
        bottom.set(top);
        bottom.y -= height;

        if (isOuter) {
            ::putVertex(pVtx, top);
            ::putVertex(pVtx, bottom);
        } else {
            ::putVertex(pVtx, bottom);
            ::putVertex(pVtx, top);
        }
    }
}
