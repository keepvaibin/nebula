#include "Game/MapObj/WhirlPool.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WhirlPoolAccelerator.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 1500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f1, "movement.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f2, "movement.f2", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f3, "movement.f3", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f4, "movement.f4", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f5, "movement.f5", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f6, "movement.f6", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f7, "movement.f7", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_movement_f8, "movement.f8", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f1, "initPoints.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f2, "initPoints.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f3, "initPoints.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f4, "initPoints.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f5, "initPoints.f5", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f6, "initPoints.f6", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f7, "initPoints.f7", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f8, "initPoints.f8", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f2, "draw.f2", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f3, "draw.f3", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_draw_f4, "draw.f4", 6.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawPlane_f1, "drawPlane.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawPlane_f2, "drawPlane.f2", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawPlane_f3, "drawPlane.f3", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawPlane_f4, "drawPlane.f4", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawPlane_f5, "drawPlane.f5", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadMaterial_f1, "loadMaterial.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadMaterial_f2, "loadMaterial.f2", 0.2f);

NebulaColor8 sWhirlPoolStageColor1 = {0x28, 0x28, 0x28, 0x14};
NebulaColor8 sWhirlPoolStageColor2 = {0x55, 0x96, 0xBE, 0xFF};

WhirlPool::WhirlPool(const char* pName)
    : LiveActor(pName), mRadius(100.0f), mHeight(100.0f), mAxis(0.0f, 1.0f, 0.0f), mPointCount(), mPoints(), mRotation(), mTexOffsetU0(),
      mTexOffsetV0(), mTexOffsetU1(), mTexOffsetV1(), mTexOffsetU2(), mTexOffsetV2(), mTexture(), mIndirectTexture(),
      mClippingCenter(0.0f, 0.0f, 0.0f) {
    lumacore::setClass(this, lumacore::kClassId< WhirlPool >);
}

void WhirlPool::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, -1, -1, MR::DrawType_WhirlPoolAccelerator);
    mPosition.set(*MR::getPlayerPos());
    mPosition.y = cNum_init_f1;
    MR::initDefaultPos(this, rIter);
    mRadius = cNum_init_f2 * mScale.x;
    mHeight = cNum_init_f3 * mScale.y;
    MR::calcActorAxisY(&mAxis, this);
    initPoints();
    mTexture = new GameTexture(MR::loadTexFromArc("WaterWave", "Water.bti"));
    mIndirectTexture = new GameTexture(MR::loadTexFromArc("WaterWave", "WaterIndirect.bti"));
    initSound(4, false);
    mClippingCenter.set(mPosition);
    mClippingCenter.y -= mRadius / cNum_init_f4;
    MR::setClippingTypeSphere(this, mRadius, &mClippingCenter);
    WaterAreaFunction::entryWhirlPool(this);
    makeActorAppeared();
}

void WhirlPool::movement() {
    mRotation = MR::repeat(cNum_movement_f1 + mRotation, 0.0f, cNum_movement_f2);
    mTexOffsetU0 = MR::repeat(cNum_movement_f3 + mTexOffsetU0, 0.0f, 1.0f);
    mTexOffsetV0 = MR::repeat(-cNum_movement_f4 + mTexOffsetV0, 0.0f, 1.0f);
    mTexOffsetU1 = MR::repeat(mTexOffsetU1 - cNum_movement_f5, 0.0f, 1.0f);
    mTexOffsetV1 = MR::repeat(-cNum_movement_f6 + mTexOffsetV1, 0.0f, 1.0f);
    mTexOffsetU2 = MR::repeat(cNum_movement_f7 + mTexOffsetU2, 0.0f, 1.0f);
    mTexOffsetV2 = MR::repeat(-cNum_movement_f8 + mTexOffsetV2, 0.0f, 1.0f);
    MR::startLevelSound(this, "SE_AT_LV_WHIRL_POOL");
}

void WhirlPool::initPoints() {
    mPointCount = static_cast< s32 >(mHeight / cNum_initPoints_f1) + 1;

    // A height of zero or less (a scale of 0 or below in y) keeps the one point, not a negative array.
    if (mPointCount < 1) {
        mPointCount = 1;
    }

    mPoints = new WhirlPoolPoint*[mPointCount];
    Pos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, cNum_initPoints_f2, 0.0f);
    Vec3f normal(0.0f, 0.0f, 1.0f);

    f32 angle = 0.0f;

    for (s32 i = 0; i < mPointCount; i++) {
        Vec3f position(mAxis);
        f32 pointOffset = i;
        position.scale(-cNum_initPoints_f3 * pointOffset);
        position.add(mPosition);

        f32 rate = MR::getEaseInValue(static_cast< f32 >(mPointCount - i) / mPointCount, 0.0f, 1.0f, 1.0f);
        rate = MR::getEaseInValue(rate, 0.0f, 1.0f, 1.0f);
        f32 radius = cNum_initPoints_f4 + rate * (mRadius - cNum_initPoints_f5);
        mPoints[i] = new WhirlPoolPoint(position, mAxis, normal, radius, angle / cNum_initPoints_f6, 0xFF);
        rotation.mult(normal, normal);
        angle = MR::repeat(cNum_initPoints_f7 + angle, 0.0f, cNum_initPoints_f8);
    }
}

void WhirlPool::draw() const {
    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    f32 scrollAU = mTexOffsetU0;
    f32 nextScrollAU = 1.0f / cNum_draw_f1 + scrollAU;
    f32 scrollBU = mTexOffsetU1;
    f32 nextScrollBU = 1.0f / cNum_draw_f2 + scrollBU;

    for (s32 i = 0; i < 6; i++) {
        drawPlane(site, cWhirlPoolOutline[i][0], cWhirlPoolOutline[i][1], cWhirlPoolOutline[i + 1][0], cWhirlPoolOutline[i + 1][1], scrollAU,
                  nextScrollAU, scrollBU, nextScrollBU);
        scrollAU = nextScrollAU;
        nextScrollAU += 1.0f / cNum_draw_f3;
        scrollBU = nextScrollBU;
        nextScrollBU += 1.0f / cNum_draw_f4;
    }
}

// Scroll A feeds the first swirl sample and the indirect texture, scroll B the second swirl sample.
void WhirlPool::drawPlane(TypedDraw& rDraw, f32 side0, f32 normal0, f32 side1, f32 normal1, f32 scrollAU0, f32 scrollAU1, f32 scrollBU0,
                          f32 scrollBU1) const {
    f32 v0 = mTexOffsetV0;
    f32 v1 = mTexOffsetV1;
    f32 v2 = mTexOffsetV2;

    Vec3f offset(0.0f, 0.0f, cNum_drawPlane_f1);
    Pos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, mRotation, 0.0f);
    rotation.mult(offset, offset);
    MR::makeMtxRotate(rotation, 0.0f, cNum_drawPlane_f2, 0.0f);
    VertexPCT3* vtx = rDraw.vertices< VertexPCT3 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mPointCount * 2);
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

        *vtx++ = {position.x + side.x * side0 + normal.x * normal0, position.y + side.y * side0 + normal.y * normal0, position.z + side.z * side0 + normal.z * normal0, Rgba8(0xFF, 0xFF, 0xFF, pPoint->mAlpha), scrollAU0 + pPoint->mTexCoord, v0, scrollBU0 + pPoint->mTexCoord, v1, scrollAU0 + pPoint->mTexCoord, v2};

        *vtx++ = {position.x + side.x * side1 + normal.x * normal1, position.y + side.y * side1 + normal.y * normal1, position.z + side.z * side1 + normal.z * normal1, Rgba8(0xFF, 0xFF, 0xFF, pPoint->mAlpha), scrollAU1 + pPoint->mTexCoord, v0, scrollBU1 + pPoint->mTexCoord, v1, scrollAU1 + pPoint->mTexCoord, v2};
        v0 += cNum_drawPlane_f3;
        v1 += cNum_drawPlane_f4;
        v2 += cNum_drawPlane_f5;
    }
}

SiteMaterial WhirlPool::loadMaterial() const {
    // runtime/src/render/materials/whirlpool.hlsl: the swirl (texture 0) over the screen behind (texture 1) warped by
    // the indirect texture (texture 2); blended by its alpha where above zero, depth tested and written.
    
    GameTexture screenTexture(MR::getScreenResTIMG());
    SiteMaterial material;
    material.init(NEBULA_SHADER_WHIRLPOOL, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_NONE);
    material.setColor(0, sWhirlPoolStageColor1.r, sWhirlPoolStageColor1.g, sWhirlPoolStageColor1.b,
                           sWhirlPoolStageColor1.a);
    material.setColor(4, sWhirlPoolStageColor2.r, sWhirlPoolStageColor2.g, sWhirlPoolStageColor2.b,
                           sWhirlPoolStageColor2.a);
    Pos3f screen;
    MR::calcTexProjectionMtx(&screen);
    for (u32 row = 0; row < 3; row++) {
        for (u32 column = 0; column < 4; column++) {
            material.constants[8 + row * 4 + column] = screen.mMtx[row][column];
        }
    }
    // The warp: 0.2 screen texels per indirect step.
    material.constants[20] = cNum_loadMaterial_f1 / screenTexture.getWidth();
    material.constants[21] = cNum_loadMaterial_f2 / screenTexture.getHeight();
    material.setTexture(0, mTexture);
    material.setTexture(1, &screenTexture);
    material.setTexture(2, mIndirectTexture);
    return material;
}
