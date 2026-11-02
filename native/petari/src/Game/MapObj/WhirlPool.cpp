#include "Game/MapObj/WhirlPool.hpp"
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WhirlPoolAccelerator.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>

NebulaColor8 sWhirlPoolTevReg1 = {0x28, 0x28, 0x28, 0x14};
NebulaColor8 sWhirlPoolTevReg2 = {0x55, 0x96, 0xBE, 0xFF};

WhirlPool::WhirlPool(const char* pName)
    : LiveActor(pName), mRadius(100.0f), mHeight(100.0f), mAxis(0.0f, 1.0f, 0.0f), mPointCount(), mPoints(), mRotation(), mTexOffsetU0(),
      mTexOffsetV0(), mTexOffsetU1(), mTexOffsetV1(), mTexOffsetU2(), mTexOffsetV2(), mTexture(), mIndirectTexture(),
      mClippingCenter(0.0f, 0.0f, 0.0f) {
}

void WhirlPool::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, -1, -1, MR::DrawType_WhirlPoolAccelerator);
    mPosition.set(*MR::getPlayerPos());
    mPosition.y = 1500.0f;
    MR::initDefaultPos(this, rIter);
    mRadius = 100.0f * mScale.x;
    mHeight = 100.0f * mScale.y;
    MR::calcActorAxisY(&mAxis, this);
    initPoints();
    mTexture = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "Water.bti"));
    mIndirectTexture = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "WaterIndirect.bti"));
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
    mPoints = new WhirlPoolPoint*[mPointCount];
    TPos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, 10.0f, 0.0f);
    TVec3f normal(0.0f, 0.0f, 1.0f);

    f32 angle = 0.0f;

    for (s32 i = 0; i < mPointCount; i++) {
        TVec3f position(mAxis);
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
    loadMaterial();
    f32 u0 = mTexOffsetU0;
    f32 nextU0 = 1.0f / 6.0f + u0;
    f32 u1 = mTexOffsetU1;
    f32 nextU1 = 1.0f / 6.0f + u1;

    drawPlane(0.5f, 0.70710677f, -0.5f, 0.70710677f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(-0.5f, 0.70710677f, -1.0f, 0.0f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(-1.0f, 0.0f, -0.5f, -0.70710677f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(-0.5f, -0.70710677f, 0.5f, -0.70710677f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(0.5f, -0.70710677f, 1.0f, 0.0f, u0, nextU0, u1, nextU1);
    u0 = nextU0;
    nextU0 += 1.0f / 6.0f;
    u1 = nextU1;
    nextU1 += 1.0f / 6.0f;

    drawPlane(1.0f, 0.0f, 0.5f, 0.70710677f, u0, nextU0, u1, nextU1);
}

void WhirlPool::drawPlane(f32 x0, f32 y0, f32 x1, f32 y1, f32 u0, f32 u1, f32 u2, f32 u3) const {
    f32 v0 = mTexOffsetV0;
    f32 v1 = mTexOffsetV1;
    f32 v2 = mTexOffsetV2;

    TVec3f offset(0.0f, 0.0f, 50.0f);
    TPos3f rotation;
    rotation.identity();
    MR::makeMtxRotate(rotation, 0.0f, mRotation, 0.0f);
    rotation.mult(offset, offset);
    MR::makeMtxRotate(rotation, 0.0f, 25.0f, 0.0f);
    J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mPointCount * 2, NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1 | NEBULA_SHAPE_TEXCOORD0 << 2);
    const WhirlPoolPoint* pPoint;

    for (s32 i = 0; i < mPointCount; i++) {
        pPoint = mPoints[i];
        TVec3f side(pPoint->mSide);
        TVec3f normal(pPoint->mNormal);
        side.scale(pPoint->mRadius);
        normal.scale(pPoint->mRadius);
        TVec3f position(pPoint->mPosition);
        position.add(offset);
        rotation.mult(offset, offset);

        J3DNativeDraw::position(position.x + side.x * x0 + normal.x * y0, position.y + side.y * x0 + normal.y * y0, position.z + side.z * x0 + normal.z * y0);
        J3DNativeDraw::color(0xFF, 0xFF, 0xFF, mPoints[i]->mAlpha);
        J3DNativeDraw::texCoord(u0 + pPoint->mTexCoord, v0);
        J3DNativeDraw::texCoord(u2 + pPoint->mTexCoord, v1);
        J3DNativeDraw::texCoord(u0 + pPoint->mTexCoord, v2);

        J3DNativeDraw::position(position.x + side.x * x1 + normal.x * y1, position.y + side.y * x1 + normal.y * y1, position.z + side.z * x1 + normal.z * y1);
        J3DNativeDraw::color(0xFF, 0xFF, 0xFF, mPoints[i]->mAlpha);
        J3DNativeDraw::texCoord(u1 + pPoint->mTexCoord, v0);
        v0 += 0.01f;
        J3DNativeDraw::texCoord(u3 + pPoint->mTexCoord, v1);
        v1 += 0.01f;
        J3DNativeDraw::texCoord(u1 + pPoint->mTexCoord, v2);
        v2 += 0.01f;
    }
}

void WhirlPool::loadMaterial() const {
    // runtime/src/render/materials/whirlpool.hlsl: the swirl (texture 0) over the screen behind (texture 1) warped by
    // the indirect texture (texture 2); blended by its alpha where above zero, depth tested and written.
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    J3DNativeShape::setCurrentIdentity();
    JUTTexture screenTexture(MR::getScreenResTIMG());
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_WHIRLPOOL, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_NONE);
    NativeShader::setColor(material, 0, sWhirlPoolTevReg1.r, sWhirlPoolTevReg1.g, sWhirlPoolTevReg1.b,
                           sWhirlPoolTevReg1.a);
    NativeShader::setColor(material, 4, sWhirlPoolTevReg2.r, sWhirlPoolTevReg2.g, sWhirlPoolTevReg2.b,
                           sWhirlPoolTevReg2.a);
    TPos3f screen;
    MR::calcTexProjectionMtx(&screen);
    for (u32 row = 0; row < 3; row++) {
        for (u32 column = 0; column < 4; column++) {
            material.constants[8 + row * 4 + column] = screen.mMtx[row][column];
        }
    }
    // The warp: 0.2 screen texels per indirect step.
    material.constants[20] = 0.2f / screenTexture.getWidth();
    material.constants[21] = 0.2f / screenTexture.getHeight();
    NativeShader::setTexture(material, 0, mTexture);
    NativeShader::setTexture(material, 1, &screenTexture);
    NativeShader::setTexture(material, 2, mIndirectTexture);
    NativeShader::send(material);
}

WhirlPool::~WhirlPool() {
}
