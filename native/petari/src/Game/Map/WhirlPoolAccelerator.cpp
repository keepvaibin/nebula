#include "Game/Map/WhirlPoolAccelerator.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Render/GameTexture.hpp"
#include <nebula_game_api.h>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcInfo_f1, "calcInfo.f1", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f1, "movement.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f2, "movement.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f3, "movement.f3", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f4, "movement.f4", 0.025f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f1, "initPoints.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f2, "initPoints.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f3, "initPoints.f3", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f4, "initPoints.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f5, "initPoints.f5", 0.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f6, "initPoints.f6", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f7, "initPoints.f7", 205.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f8, "initPoints.f8", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f9, "initPoints.f9", 205.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f10, "initPoints.f10", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f11, "initPoints.f11", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawPlane_f1, "drawPlane.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawPlane_f2, "drawPlane.f2", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawPlane_f3, "drawPlane.f3", 0.02f);

NebulaColor8 sWhirlPoolAcceleratorStageColor1 = {0x00, 0x34, 0x52, 0xFF};
NebulaColor8 sWhirlPoolAcceleratorStageColor2 = {0x7C, 0xA9, 0xBD, 0xFF};

namespace {
    extern const f32 sAlphaPointNum;
};  // namespace

WhirlPoolPoint::WhirlPoolPoint(const Vec3f& rPosition, const Vec3f& rDirection, const Vec3f& rNormal, f32 radius, f32 texCoord, u8 alpha)
    : mPosition(rPosition), mSide(1.0f, 0.0f, 0.0f), mNormal(rNormal), mDirection(0.0f, 0.0f, 1.0f), mRadius(radius), mTexCoord(texCoord),
      mAlpha(alpha) {
    mDirection.set(rDirection);
    mDirection.mult(-1.0f);
    mSide.cross(mNormal, mDirection);
    MR::normalize(&mSide);
    mNormal.cross(mDirection, mSide);
    MR::normalize(&mNormal);
}

WhirlPoolAccelerator::WhirlPoolAccelerator(const char* pName)
    : LiveActor(pName), mRadius(100.0f), mHeight(100.0f), mAxis(0.0f, 1.0f, 0.0f), mPointCount(), mPoints(), mRotation(),
      mTexOffsetU0(MR::getRandom()), mTexOffsetV0(MR::getRandom()), mTexture(), mClippingCenter(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< WhirlPoolAccelerator >);
}

void WhirlPoolAccelerator::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, -1, -1, MR::DrawType_WhirlPoolAccelerator);
    MR::initDefaultPos(this, rIter);
    mRadius = cNum_init_f1 * mScale.x;
    mHeight = cNum_init_f2 * mScale.y;
    MR::calcActorAxisY(&mAxis, this);
    initPoints();
    mTexture = new GameTexture(MR::loadTexFromArc("Whirlpool", "Whirlpool.bti"));
    initSound(4, false);
    mClippingCenter.set(mPosition);
    mClippingCenter.y += mHeight / cNum_init_f3;
    MR::setClippingTypeSphere(this, mHeight / cNum_init_f4, &mClippingCenter);
    WaterAreaFunction::entryWhirlPoolAccelerator(this);
    makeActorAppeared();
}

bool WhirlPoolAccelerator::calcInfo(const Vec3f& rPosition, Vec3f* pAcceleration) const {
    pAcceleration->zero();
    Vec3f relative(rPosition);
    relative.sub(mPosition);
    f32 height = mAxis.dot(relative);
    if (height < 0.0f || height > mHeight) {
        return false;
    }

    MR::vecKillElement(relative, mAxis, &relative);
    if (relative.length() > mRadius) {
        return false;
    }

    pAcceleration->cross(relative, mAxis);
    MR::normalize(pAcceleration);
    pAcceleration->scale(cNum_calcInfo_f1);
    return true;
}

void WhirlPoolAccelerator::movement() {
    mRotation = MR::repeat(-cNum_movement_f1 + mRotation, 0.0f, cNum_movement_f2);
    mTexOffsetU0 = MR::repeat(cNum_movement_f3 + mTexOffsetU0, 0.0f, 1.0f);
    mTexOffsetV0 = MR::repeat(-cNum_movement_f4 + mTexOffsetV0, 0.0f, 1.0f);
    MR::startLevelSound(this, "SE_AT_LV_WHIRL_POOL");
}

void WhirlPoolAccelerator::initPoints() {
    mPointCount = static_cast< s32 >(mHeight / cNum_initPoints_f1) + 1;
    // A height under zero still has the one point.
    if (mPointCount < 1) {
        mPointCount = 1;
    }
    mPoints = new WhirlPoolPoint*[mPointCount];
    Pos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, -cNum_initPoints_f2, 0.0f);
    Vec3f normal(0.0f, 0.0f, 1.0f);

    f32 angle = cNum_initPoints_f3;

    for (s32 i = 0; i < mPointCount; i++) {
        Vec3f position(mAxis);
        position.scale(mHeight - cNum_initPoints_f4 * i);
        position.add(mPosition);

        f32 rate = MR::getEaseInValue(static_cast< f32 >(mPointCount - i) / mPointCount, 0.0f, 1.0f, 1.0f);
        f32 radius = mRadius * (rate + cNum_initPoints_f5 * (1.0f - rate));
        u8 alpha = 0xFF;
        if (i < ::sAlphaPointNum) {
            alpha = (s32)(cNum_initPoints_f6 + cNum_initPoints_f7 * (i / ::sAlphaPointNum));
        }

        if (i >= mPointCount - ::sAlphaPointNum) {
            alpha = (s32)(cNum_initPoints_f8 + cNum_initPoints_f9 * ((mPointCount - 1 - i) / ::sAlphaPointNum));
        }

        mPoints[i] = new WhirlPoolPoint(position, mAxis, normal, radius, angle / cNum_initPoints_f10, alpha);
        rotation.mult(normal, normal);
        angle -= cNum_initPoints_f11;
    }
}

void WhirlPoolAccelerator::draw() const {
    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    f32 u0 = mTexOffsetU0;
    f32 nextU0 = 1.0f / cNum_draw_f1 + u0;

    for (s32 i = 0; i < 6; i++) {
        drawPlane(site, cWhirlPoolOutline[i][0], cWhirlPoolOutline[i][1], cWhirlPoolOutline[i + 1][0], cWhirlPoolOutline[i + 1][1], u0, nextU0);
        u0 = nextU0;
        nextU0 += 1.0f / cNum_draw_f2;
    }
}

void WhirlPoolAccelerator::drawPlane(TypedDraw& rDraw, f32 side0, f32 normal0, f32 side1, f32 normal1, f32 u0, f32 u1) const {
    f32 v0 = mTexOffsetV0;

    Vec3f offset(0.0f, 0.0f, cNum_drawPlane_f1);
    Pos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, mRotation, 0.0f);
    rotation.mult(offset, offset);
    MR::makeMtxRotate(rotation, 0.0f, cNum_drawPlane_f2, 0.0f);
    VertexPCT* vtx = rDraw.vertices< VertexPCT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mPointCount * 2);
    const WhirlPoolPoint* pPoint;

    for (s32 i = 0; i < mPointCount; i++) {
        pPoint = mPoints[i];
        Vec3f side(pPoint->mSide);
        Vec3f normal(pPoint->mNormal);
        side.scale(pPoint->mRadius);
        normal.scale(pPoint->mRadius);
        Vec3f position(pPoint->mPosition);
        position.add(offset);
        rotation.mult(offset, offset);

        *vtx++ = {position.x + side.x * side0 + normal.x * normal0, position.y + side.y * side0 + normal.y * normal0, position.z + side.z * side0 + normal.z * normal0, Rgba8(0xFF, 0xFF, 0xFF, pPoint->mAlpha), u0 + pPoint->mTexCoord, v0};

        *vtx++ = {position.x + side.x * side1 + normal.x * normal1, position.y + side.y * side1 + normal.y * normal1, position.z + side.z * side1 + normal.z * normal1, Rgba8(0xFF, 0xFF, 0xFF, pPoint->mAlpha), u1 + pPoint->mTexCoord, v0};
        v0 += cNum_drawPlane_f3;
    }
}

SiteMaterial WhirlPoolAccelerator::loadMaterial() const {
    // runtime/src/render/materials/whirlpool_accelerator.hlsl: the two colours mixed by the texture at the vertex
    // alpha; drawn where above zero and blended by it, depth tested and written, no culling.
    SiteMaterial material;
    material.init(NEBULA_SHADER_WHIRLPOOL_ACCELERATOR, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL,
                       true, NEBULA_CULL_NONE);
    const NebulaColor8& rLow = sWhirlPoolAcceleratorStageColor1;
    const NebulaColor8& rHigh = sWhirlPoolAcceleratorStageColor2;
    material.setColor(0, rLow.r, rLow.g, rLow.b, rLow.a);
    material.setColor(4, rHigh.r, rHigh.g, rHigh.b, rHigh.a);
    material.setTexture(0, mTexture);
    return material;
}

namespace {
    const f32 sAlphaPointNum = 2.0f;
};  // namespace
