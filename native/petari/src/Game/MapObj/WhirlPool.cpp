#include "Game/MapObj/WhirlPool.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WhirlPoolAccelerator.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/System/LumaCore.hpp"

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
    mPosition.y = 1500.0f;
    MR::initDefaultPos(this, rIter);
    mRadius = 100.0f * mScale.x;
    mHeight = 100.0f * mScale.y;
    MR::calcActorAxisY(&mAxis, this);
    initPoints();
    mTexture = new GameTexture(MR::loadTexFromArc("WaterWave", "Water.bti"));
    mIndirectTexture = new GameTexture(MR::loadTexFromArc("WaterWave", "WaterIndirect.bti"));
    initSound(4, false);
    mClippingCenter.set(mPosition);
    mClippingCenter.y -= mRadius / 2.0f;
    MR::setClippingTypeSphere(this, mRadius, &mClippingCenter);
    WaterAreaFunction::entryWhirlPool(this);
    makeActorAppeared();
}

void WhirlPool::movement() {
    mRotation = MR::repeat(2.0f + mRotation, 0.0f, 360.0f);
    mTexOffsetU0 = MR::repeat(0.001f + mTexOffsetU0, 0.0f, 1.0f);
    mTexOffsetV0 = MR::repeat(-0.001f + mTexOffsetV0, 0.0f, 1.0f);
    mTexOffsetU1 = MR::repeat(mTexOffsetU1 - 0.001f, 0.0f, 1.0f);
    mTexOffsetV1 = MR::repeat(-0.001f + mTexOffsetV1, 0.0f, 1.0f);
    mTexOffsetU2 = MR::repeat(0.001f + mTexOffsetU2, 0.0f, 1.0f);
    mTexOffsetV2 = MR::repeat(-0.01f + mTexOffsetV2, 0.0f, 1.0f);
    MR::startLevelSound(this, "SE_AT_LV_WHIRL_POOL");
}

void WhirlPool::initPoints() {
    mPointCount = static_cast< s32 >(mHeight / 50.0f) + 1;

    // A height of zero or less (a scale of 0 or below in y) keeps the one point, not a negative array.
    if (mPointCount < 1) {
        mPointCount = 1;
    }

    mPoints = new WhirlPoolPoint*[mPointCount];
    Pos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, 10.0f, 0.0f);
    Vec3f normal(0.0f, 0.0f, 1.0f);

    f32 angle = 0.0f;

    for (s32 i = 0; i < mPointCount; i++) {
        Vec3f position(mAxis);
        f32 pointOffset = i;
        position.scale(-50.0f * pointOffset);
        position.add(mPosition);

        f32 rate = MR::getEaseInValue(static_cast< f32 >(mPointCount - i) / mPointCount, 0.0f, 1.0f, 1.0f);
        rate = MR::getEaseInValue(rate, 0.0f, 1.0f, 1.0f);
        f32 radius = 100.0f + rate * (mRadius - 100.0f);
        mPoints[i] = new WhirlPoolPoint(position, mAxis, normal, radius, angle / 360.0f, 0xFF);
        rotation.mult(normal, normal);
        angle = MR::repeat(10.0f + angle, 0.0f, 360.0f);
    }
}

void WhirlPool::draw() const {
    const SiteMaterial material = loadMaterial();
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());
    f32 scrollAU = mTexOffsetU0;
    f32 nextScrollAU = 1.0f / 6.0f + scrollAU;
    f32 scrollBU = mTexOffsetU1;
    f32 nextScrollBU = 1.0f / 6.0f + scrollBU;

    for (s32 i = 0; i < 6; i++) {
        drawPlane(site, cWhirlPoolOutline[i][0], cWhirlPoolOutline[i][1], cWhirlPoolOutline[i + 1][0], cWhirlPoolOutline[i + 1][1], scrollAU,
                  nextScrollAU, scrollBU, nextScrollBU);
        scrollAU = nextScrollAU;
        nextScrollAU += 1.0f / 6.0f;
        scrollBU = nextScrollBU;
        nextScrollBU += 1.0f / 6.0f;
    }
}

// Scroll A feeds the first swirl sample and the indirect texture, scroll B the second swirl sample.
void WhirlPool::drawPlane(TypedDraw& rDraw, f32 side0, f32 normal0, f32 side1, f32 normal1, f32 scrollAU0, f32 scrollAU1, f32 scrollBU0,
                          f32 scrollBU1) const {
    f32 v0 = mTexOffsetV0;
    f32 v1 = mTexOffsetV1;
    f32 v2 = mTexOffsetV2;

    Vec3f offset(0.0f, 0.0f, 50.0f);
    Pos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, mRotation, 0.0f);
    rotation.mult(offset, offset);
    MR::makeMtxRotate(rotation, 0.0f, 25.0f, 0.0f);
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
        v0 += 0.01f;
        v1 += 0.01f;
        v2 += 0.01f;
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
    material.constants[20] = 0.2f / screenTexture.getWidth();
    material.constants[21] = 0.2f / screenTexture.getHeight();
    material.setTexture(0, mTexture);
    material.setTexture(1, &screenTexture);
    material.setTexture(2, mIndirectTexture);
    return material;
}
