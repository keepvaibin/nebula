#include "Game/Map/OceanBowl.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/Map/OceanBowlBloomDrawer.hpp"
#include "Game/Map/OceanBowlPoint.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "JSystem/JUtility/JUTTexture.hpp"
#include "Game/Util/NativeShader.hpp"

// These are from the debug symbol map
namespace {
    const f32 sPointInterval = 200.0f;
    const f32 sPointWaveRateDistMax = 500.0f;
    const f32 sTexRate0 = 0.05f;
    const f32 sTexRate1 = 0.05f;
    const f32 sTexRate2 = 0.1f;
    const f32 sWaveSpeed0 = -0.04f;
    const f32 sWaveSpeed1 = -0.04f;
    const f32 sTexSpeedU0 = -0.0008f;
    const f32 sTexSpeedV0 = -0.0008f;
    const f32 sTexSpeedU1 = -0.001f;
    const f32 sTexSpeedV1 = 0.0008f;
    const f32 sTexSpeedU2 = -0.003f;
    const f32 sTexSpeedV2 = -0.001f;
    const f32 sIndirectScale = 0.1f;
    const f32 sClippingDistance = 1000.0f;
    const f32 sPointIntervalHalf = ::sPointInterval / 2.0f;

    static NebulaColor8 sOceanBowlStageColor0 = {40, 40, 40, 20};
    static NebulaColor8 sOceanBowlStageColor1 = {200, 230, 210, 255};
    static u8 sOceanBowlBloomStageColor0V = 95;
    static u8 sOceanBowlBloomStageColor1V = 50;
};  // namespace

OceanBowl::OceanBowl(const char* pName)
    : LiveActor(pName), mSide(1.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mRadius(), mWaveX(), mWaveZ(), mTexU0(), mTexV0(),
      mTexU1(), mTexV1(), mTexU2(), mTexV2(), _A98(5.0f / 120.0f), mVtxTex0Array(), mVtxTex1Array(), mVtxTex2Array(), mVtxTex3Array(), mWaterTex(),
      mWaterIndirectTex(), mMaskTex(), mBloomDrawer(), mIsClipped() {
    for (s32 x = 0; x < 25; x++) {
        for (s32 y = 0; y < 25; y++) {
            setPoint(x, y, nullptr);
        }
    }
}

void OceanBowl::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanBowl);

    MR::initDefaultPos(this, rIter);

    MR::calcActorAxis(&mSide, &mUp, &mFront, this);

    mRadius = 100.0f * mScale.x;
    initPoints();
    WaterAreaFunction::entryOceanBowl(this);

    mWaterTex = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "Water.bti"));
    mWaterIndirectTex = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "WaterIndirect.bti"));
    mMaskTex = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "Mask.bti"));

    mBloomDrawer = new OceanBowlBloomDrawer(this);
    mBloomDrawer->initWithoutIter();

    MR::invalidateClipping(this);

    makeActorAppeared();
}

bool OceanBowl::isInWater(const TVec3f& rPos) const {
    if (rPos.distance(mPosition) > mRadius) {
        return false;
    }

    return !(mUp.dot(rPos - mPosition) > 0.0f);
}

bool OceanBowl::calcWaterInfo(const TVec3f& rPos, const TVec3f& rGravity, WaterInfo* pInfo) const {
    const TVec3f* pPosition = &mPosition;
    f32 B0 = mRadius;
    TVec3f v(rPos);
    v.sub(*pPosition);
    f32 fa = MR::vecKillElement(v, mUp, &v);
    f32 fc = v.length() / B0;
    f32 fb = fc;
    fb *= PI;
    fb *= 0.5f;

    pInfo->_4 = MR::cos(fb) * B0;
    pInfo->_4 += fa;

    pInfo->mCamWaterDepth = -fa;
    pInfo->mWaveHeight = OceanBowlPoint::calcHeightStatic(mWaveX, mWaveZ, rPos.x, rPos.z);

    TVec3f v2(rPos), v3;
    v2.sub(*pPosition);
    MR::normalizeOrZero(&v2);

    pInfo->mSurfaceNormal.set(mUp);

    MR::vecKillElement(rPos - *pPosition, mUp, &v3);
    pInfo->mSurfacePos.set(*pPosition + v3);

    TVec3f v5(rPos);
    const TVec3f& rPosition1 = mPosition;
    v5.sub(rPosition1);

    if (v5.y > 0.0f) {
        v5.y = 0.0f;
    }

    if (MR::isNearZero(v5)) {
        pInfo->mEdgeDistance = B0;
        TVec3f v6(rGravity);
        v6.scale(B0);
        const TVec3f& rPosition2 = mPosition;
        v6.add(rPosition2);
        pInfo->mEdgePos.set(v6);
    } else {
        MR::normalize(&v5);
        v5.scale(B0);
        const TVec3f& rPosition3 = mPosition;
        v5.add(rPosition3);
        pInfo->mEdgeDistance = v5.distance(rPos);
        pInfo->mEdgePos.set(v5);
    }

    return true;
}

void OceanBowl::movement() {
    const TVec3f* pPosition = &getPoint(12, 12)->mPosition;
    f32 distToPlayer = MR::calcDistanceToPlayer(mPosition);

    if ((MR::isCameraInWater() && WaterAreaFunction::getCameraWaterInfo()->mOceanBowl != this) || distToPlayer > ::sClippingDistance + mRadius) {
        mIsClipped = true;
        return;
    }

    mIsClipped = false;
    TVec3f playerpos(*MR::getPlayerPos());
    playerpos.sub(*pPosition);
    f32 dotA = mSide.dot(playerpos);
    f32 dotB = mFront.dot(playerpos);

    if (dotA < -::sPointIntervalHalf && dotA > -mRadius) {
        moveToLeft();
    } else if (dotA > ::sPointIntervalHalf && dotA < mRadius) {
        moveToRight();
    } else if (dotB < -::sPointIntervalHalf && dotB > -mRadius) {
        moveToUpper();
    } else if (dotB > ::sPointIntervalHalf && dotB < mRadius) {
        moveToLower();
    }

    mWaveX += ::sWaveSpeed0;
    mWaveZ += ::sWaveSpeed1;

    updatePoints();

    mTexU0 = MR::repeat(mTexU0 + ::sTexSpeedU0, 0.0f, 1.0f);
    mTexV0 = MR::repeat(mTexV0 + ::sTexSpeedV0, 0.0f, 1.0f);
    mTexU1 = MR::repeat(mTexU1 + ::sTexSpeedU1, 0.0f, 1.0f);
    mTexV1 = MR::repeat(mTexV1 + ::sTexSpeedV1, 0.0f, 1.0f);
    mTexU2 = MR::repeat(mTexU2 + ::sTexSpeedU2, 0.0f, 1.0f);
    mTexV2 = MR::repeat(mTexV2 + ::sTexSpeedV2, 0.0f, 1.0f);
}

void OceanBowl::initPoints() {
    for (s32 x = 0; x < 25; x++) {
        for (s32 y = 0; y < 25; y++) {
            TVec3f vecA(mSide);
            TVec3f vecB(mFront);
            vecA.scale(((static_cast< f32 >(y) * ::sPointInterval) - 2500.0f) + ::sPointIntervalHalf);
            vecB.scale(((static_cast< f32 >(x) * ::sPointInterval) - 2500.0f) + ::sPointIntervalHalf);
            TVec3f finalpos(mPosition);
            finalpos.add(vecA);
            finalpos.add(vecB);

            setPoint(x, y, new OceanBowlPoint(finalpos));
        }
    }

    mVtxTex0Array = new f32[24 * 25 * 4];
    mVtxTex1Array = new f32[24 * 25 * 4];
    mVtxTex2Array = new f32[24 * 25 * 4];
    mVtxTex3Array = new f32[24 * 25 * 4];

    s32 index = 0;

    f32 f5 = ::sTexRate0;
    f32 f6 = 0.0f;

    f32 f7 = ::sTexRate1;
    f32 f8 = 0.0f;

    f32 f9 = ::sTexRate2;
    f32 f10 = 0.0f;

    f32 f11 = 0.0f;
    f32 f12 = _A98;

    for (s32 x = 0; x < 24; x++) {
        f32 r13 = 0.0f;
        f32 r28 = 0.0f;
        f32 r29 = 0.0f;
        f32 r30 = 0.0f;
        for (s32 y = 0; y < 25; y++) {
            mVtxTex0Array[index + 0] = f5;
            mVtxTex0Array[index + 1] = r13;
            mVtxTex0Array[index + 2] = f6;
            mVtxTex0Array[index + 3] = r13;
            r13 += ::sTexRate0;

            mVtxTex1Array[index + 0] = f7;
            mVtxTex1Array[index + 1] = r28;
            mVtxTex1Array[index + 2] = f8;
            mVtxTex1Array[index + 3] = r28;
            r28 += ::sTexRate1;

            mVtxTex2Array[index + 0] = f9;
            mVtxTex2Array[index + 1] = r29;
            mVtxTex2Array[index + 2] = f10;
            mVtxTex2Array[index + 3] = r29;
            r29 += ::sTexRate2;

            mVtxTex3Array[index + 0] = f12;
            mVtxTex3Array[index + 1] = r30;
            mVtxTex3Array[index + 2] = f11;
            mVtxTex3Array[index + 3] = r30;
            r30 += _A98;

            index += 4;
        }

        f6 = f5;
        f8 = f7;
        f10 = f9;
        f11 = f12;
        f5 += ::sTexRate0;
        f7 += ::sTexRate1;
        f9 += ::sTexRate2;
        f12 += _A98;
    }
}

void OceanBowl::updatePoints() {
    for (s32 x = 0; x < 25; x++) {
        for (s32 y = 0; y < 25; y++) {
            getPoint(x, y)->updatePos(mWaveX, mWaveZ);
        }
    }
}

void OceanBowl::moveToLeft() {
    OceanBowlPoint* mLastPoint;

    for (s32 x = 0; x < 25; x++) {
        mLastPoint = getPoint(x, 24);

        for (s32 y = 24; y > 0; y--) {
            setPoint(x, y, getPoint(x, y - 1));
        }

        setPoint(x, 0, mLastPoint);

        TVec3f resetvec(mSide);
        resetvec.scale(-200.0f);
        resetvec.add(getPoint(x, 1)->mPosition);
        mLastPoint->reset(resetvec, MR::clamp((mRadius - getPosition()->distance(resetvec)) / ::sPointWaveRateDistMax, 0.0f, 1.0f));
    }

    mTexV0 -= ::sTexRate0;
    mTexV1 -= ::sTexRate1;
    mTexV2 -= ::sTexRate2;
}

void OceanBowl::moveToRight() {
    OceanBowlPoint* mLastPoint;

    for (s32 x = 0; x < 25; x++) {
        mLastPoint = getPoint(x, 0);

        for (s32 y = 0; y < 24; y++) {
            setPoint(x, y, getPoint(x, y + 1));
        }

        setPoint(x, 24, mLastPoint);

        TVec3f resetvec(mSide);
        resetvec.scale(::sPointInterval);
        resetvec.add(getPoint(x, 23)->mPosition);
        mLastPoint->reset(resetvec, MR::clamp((mRadius - getPosition()->distance(resetvec)) / ::sPointWaveRateDistMax, 0.0f, 1.0f));
    }

    mTexV0 += ::sTexRate0;
    mTexV1 += ::sTexRate1;
    mTexV2 += ::sTexRate2;
}

void OceanBowl::moveToUpper() {
    OceanBowlPoint* mLastPoint;

    for (s32 y = 0; y < 25; y++) {
        mLastPoint = getPoint(24, y);

        for (s32 x = 24; x > 0; x--) {
            setPoint(x, y, getPoint(x - 1, y));
        }

        setPoint(0, y, mLastPoint);

        TVec3f resetvec(mFront);
        resetvec.scale(-::sPointInterval);
        resetvec.add(getPoint(1, y)->mPosition);
        mLastPoint->reset(resetvec, MR::clamp((mRadius - getPosition()->distance(resetvec)) / ::sPointWaveRateDistMax, 0.0f, 1.0f));
    }

    mTexU0 -= ::sTexRate0;
    mTexU1 -= ::sTexRate1;
    mTexU2 -= ::sTexRate2;
}

void OceanBowl::moveToLower() {
    OceanBowlPoint* mLastPoint;

    for (s32 y = 0; y < 25; y++) {
        mLastPoint = getPoint(0, y);

        for (s32 x = 0; x < 24; x++) {
            setPoint(x, y, getPoint(x + 1, y));
        }

        setPoint(24, y, mLastPoint);

        TVec3f resetvec(mFront);
        resetvec.scale(::sPointInterval);
        resetvec.add(getPoint(23, y)->mPosition);
        mLastPoint->reset(resetvec, MR::clamp((mRadius - getPosition()->distance(resetvec)) / ::sPointWaveRateDistMax, 0.0f, 1.0f));
    }

    mTexU0 += ::sTexRate0;
    mTexU1 += ::sTexRate1;
    mTexU2 += ::sTexRate2;
}

void OceanBowl::draw() const {
    if (mIsClipped || !MR::isValidDraw(this)) {
        return;
    }

    loadMaterial();

    u16 even = 0;
    u16 odd = 1;
    for (s32 x = 0; x < 24; x++) {
        // Texture coordinates 0-3 are entries of the bowl's arrays (two floats each), the vertex's index in all four.
        J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, 50,
                             NEBULA_SHAPE_COLOR0 | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1 |
                                 NEBULA_SHAPE_TEXCOORD0 << 2 | NEBULA_SHAPE_TEXCOORD0 << 3);

        for (s32 y = 0; y < 25; y++) {
            OceanBowlPoint* pPoint = getPoint(x + 1, y);
            OceanBowlPoint* pPoint2 = getPoint(x, y);

            J3DNativeDraw::position(pPoint->mVertexPosition.x, pPoint->mVertexPosition.y, pPoint->mVertexPosition.z);
            J3DNativeDraw::color(255, 255, 255, getPoint(x + 1, y)->mAlpha);
            J3DNativeDraw::texCoord(mVtxTex0Array[2 * even], mVtxTex0Array[2 * even + 1]);
            J3DNativeDraw::texCoord(mVtxTex1Array[2 * even], mVtxTex1Array[2 * even + 1]);
            J3DNativeDraw::texCoord(mVtxTex2Array[2 * even], mVtxTex2Array[2 * even + 1]);
            J3DNativeDraw::texCoord(mVtxTex3Array[2 * even], mVtxTex3Array[2 * even + 1]);

            J3DNativeDraw::position(pPoint2->mVertexPosition.x, pPoint2->mVertexPosition.y, pPoint2->mVertexPosition.z);
            J3DNativeDraw::color(255, 255, 255, getPoint(x, y)->mAlpha);
            J3DNativeDraw::texCoord(mVtxTex0Array[2 * odd], mVtxTex0Array[2 * odd + 1]);
            J3DNativeDraw::texCoord(mVtxTex1Array[2 * odd], mVtxTex1Array[2 * odd + 1]);
            J3DNativeDraw::texCoord(mVtxTex2Array[2 * odd], mVtxTex2Array[2 * odd + 1]);
            J3DNativeDraw::texCoord(mVtxTex3Array[2 * odd], mVtxTex3Array[2 * odd + 1]);

            even += 2;
            odd += 2;
        }
    }
}

void OceanBowl::loadMaterial() const {
    // runtime/src/render/materials/ocean_bowl.hlsl: the water (texture 0) and, above water, the screen behind (texture
    // 1) warped by the indirect texture (texture 2); alpha the vertex alpha times the shore mask (texture 3). Blended by
    // its alpha (added under water), depth tested, not written.
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    J3DNativeShape::setCurrentIdentity();
    const bool inWater = MR::isCameraInWater();
    JUTTexture screenTexture(MR::getScreenResTIMG());
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_OCEAN_BOWL,
                       inWater ? NativeShader::BLEND_ADD_ALPHA : NativeShader::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL,
                       false, NEBULA_CULL_NONE);
    NativeShader::setColor(material, 0, ::sOceanBowlStageColor0.r, ::sOceanBowlStageColor0.g, ::sOceanBowlStageColor0.b,
                           ::sOceanBowlStageColor0.a);
    NativeShader::setColor(material, 4, ::sOceanBowlStageColor1.r, ::sOceanBowlStageColor1.g, ::sOceanBowlStageColor1.b,
                           ::sOceanBowlStageColor1.a);
    material.constants[8] = mTexU0;
    material.constants[9] = mTexV0;
    material.constants[10] = mTexU1;
    material.constants[11] = mTexV1;
    material.constants[12] = mTexU2;
    material.constants[13] = mTexV2;
    // The shore mask follows the player: its offset from the bowl's first point, over the mask's 4800 units.
    material.constants[14] = ((getPoint(0, 0)->mVertexPosition.z - MR::getPlayerPos()->z) + 2400.0f) / 4800.0f;
    material.constants[15] = ((getPoint(0, 0)->mVertexPosition.x - MR::getPlayerPos()->x) + 2400.0f) / 4800.0f;
    TPos3f screen;
    MR::calcTexProjectionMtx(&screen);
    for (u32 row = 0; row < 3; row++) {
        for (u32 column = 0; column < 4; column++) {
            material.constants[16 + row * 4 + column] = screen.mMtx[row][column];
        }
    }
    material.constants[28] = ::sIndirectScale / screenTexture.getWidth();
    material.constants[29] = ::sIndirectScale / screenTexture.getHeight();
    material.constants[30] = inWater ? 0.0f : 1.0f;
    NativeShader::setTexture(material, 0, mWaterTex);
    NativeShader::setTexture(material, 1, &screenTexture);
    NativeShader::setTexture(material, 2, mWaterIndirectTex);
    NativeShader::setTexture(material, 3, mMaskTex);
    NativeShader::send(material);
}

void OceanBowl::loadMaterialBloom() const {
    // runtime/src/render/materials/ocean_bowl_bloom.hlsl: the water texture at its two scrolls times the grey c0 over
    // the grey c1, alpha the texture's by the vertex alpha at both; blended by its alpha where above zero, depth
    // tested and written before texturing, no culling.
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    J3DNativeShape::setCurrentIdentity();
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_OCEAN_BOWL_BLOOM, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_NONE);
    material.early_depth = 1;
    NativeShader::setColor(material, 0, ::sOceanBowlBloomStageColor0V, ::sOceanBowlBloomStageColor0V, ::sOceanBowlBloomStageColor0V,
                           255);
    NativeShader::setColor(material, 4, ::sOceanBowlBloomStageColor1V, ::sOceanBowlBloomStageColor1V, ::sOceanBowlBloomStageColor1V,
                           255);
    material.constants[8] = mTexU0;
    material.constants[9] = mTexV0;
    material.constants[10] = mTexU1;
    material.constants[11] = mTexV1;
    NativeShader::setTexture(material, 0, mWaterTex);
    NativeShader::send(material);
}
