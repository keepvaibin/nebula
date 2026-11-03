#include "Game/Map/OceanSphere.hpp"
#include "Game/Util/NativeShader.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/OceanSpherePoint.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/NativeMesh.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include "Game/System/NebulaArena.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>

NebulaColor8 sOceanSphereTevReg0Face = {0x48, 0x80, 0xBE, 0x1C};
NebulaColor8 sOceanSphereTevReg1FrontDefault = {0x00, 0x51, 0x70, 0x6F};
NebulaColor8 sOceanSphereTevReg1FrontTear = {0x00, 0x64, 0xFF, 0x6F};
NebulaColor8 sOceanSphereTevReg2Face = {0xFF, 0xFF, 0xFF, 0xFF};
NebulaColor8 sOceanSphereKColor0Face = {0x78, 0xFF, 0xFF, 0x00};
NebulaColor8 sOceanSphereTevReg0Back = {0x48, 0x80, 0xBE, 0x1C};
NebulaColor8 sOceanSphereTevReg1BackDefault = {0x00, 0x51, 0x70, 0x6F};
NebulaColor8 sOceanSphereTevReg1BackTear = {0x00, 0xC3, 0xFF, 0x6F};
NebulaColor8 sOceanSphereTevReg2Back = {0xFF, 0xFF, 0xFF, 0xFF};
NebulaColor8 sOceanSphereKColor0Back = {0x78, 0xFF, 0xFF, 0x00};
NebulaColor8 sOceanSphereTevReg0Env = {0x00, 0x69, 0xB8, 0x14};
NebulaColor8 sOceanSphereTevReg1Env = {0x00, 0x00, 0x00, 0xFF};

namespace NrvOceanSphere {
    NEW_NERVE(OceanSphereNrvRiseUp, OceanSphere, RiseUp);
    NEW_NERVE(OceanSphereNrvWait, OceanSphere, Wait);
};  // namespace NrvOceanSphere

namespace {
    Vec sAxisX = {1.0f, 0.0f, 0.0f};
    Vec sAxisY = {0.0f, 1.0f, 0.0f};
    Vec sAxisZ = {0.0f, 0.0f, 1.0f};

    TVec3f sPosAxisX(sAxisX);
    TVec3f sPosAxisY(sAxisY);
    TVec3f sPosAxisZ(sAxisZ);
};  // namespace

namespace {
    // `pMesh`: the mesh recording the sphere (null to draw it now, its vertices giving `attributes`).
    void beginDrawPolygon(int idx, s32 pointCount, NativeMesh* pMesh, u32 attributes) {
        const u16 vtxCount = (u16)(((pointCount - 1 - idx) * 2 + 1));
        if (pMesh != nullptr) {
            pMesh->begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, vtxCount);
        } else {
            J3DNativeDraw::begin(NEBULA_PRIMITIVE_TRIANGLE_STRIP, vtxCount, attributes);
        }
    }
};  // namespace

OceanSpherePlane::OceanSpherePlane(s32 pointCount, const TVec3f* pCenter, const TVec3f& rAxis1, const TVec3f& rAxis2, const TVec2f& rTex1,
                                   const TVec2f& rTex2, const TVec2f& rTex3) {
    mPoints = nullptr;
    mAxisPointCount = pointCount - 2;
    mGridPointCount = mAxisPointCount * mAxisPointCount;
    mPoints = new OceanSpherePoint*[mGridPointCount];

    TRot3f rot;
    rot.identity();
    TVec2f texCenter(0.5f, 0.5f);

    for (s32 row = 1; row < pointCount - 1; row++) {
        rot.setRotate(rAxis2, (static_cast< f32 >(row) / (pointCount - 1)) * (MR::pi() / 2.0f));
        TVec3f vec(rAxis1);
        rot.mult(vec, vec);
        f32 rowRate = static_cast< f32 >(row) / (pointCount - 1);
        TVec2f texA = texCenter * (1.0f - rowRate) + rTex1 * rowRate;
        TVec2f texB = texCenter * (1.0f - rowRate) + rTex2 * rowRate;
        TVec2f texC = texCenter * (1.0f - rowRate) + rTex3 * rowRate;

        for (s32 col = 1; col < 2 * row; col++) {
            rot.setRotate(rAxis1, (static_cast< f32 >(col) / (2 * row)) * MR::pi());
            TVec3f normal(vec);
            rot.mult(normal, normal);
            TVec2f tex(0.0f, 0.0f);
            s32 index;

            if (col <= row) {
                index = (row - 1) * mAxisPointCount + col - 1;
                f32 rate = static_cast< f32 >(col) / row;
                tex = texB * (1.0f - rate) + texA * rate;
            } else {
                index = (row - 1) * (mAxisPointCount + 1) - mAxisPointCount * (col - row);
                f32 rate = static_cast< f32 >(col - row) / row;
                tex = texA * (1.0f - rate) + texC * rate;
            }

            mPoints[index] = new OceanSpherePoint(pCenter, normal, row, col, tex);
        }
    }
}

void OceanSpherePlane::update(f32 radius, f32 wave1Time, f32 wave2Time) {
    for (s32 row = 0; row < mAxisPointCount; row++) {
        for (s32 col = 0; col < mAxisPointCount; col++) {
            getPoint(col, row)->updatePos(radius, wave1Time, wave2Time);
        }
    }
}

OceanSpherePlaneEdge::OceanSpherePlaneEdge(s32 pointCount, const TVec3f* pCenter, const TVec3f& rAxis1, const TVec3f& rAxis2, const TVec2f& rTex1,
                                           const TVec2f& rTex2) {
    mPoints = nullptr;
    mPointCount = pointCount - 2;
    mPoints = new OceanSpherePoint*[mPointCount];
    TVec3f axis = rAxis1.cross(rAxis2);
    MR::normalize(&axis);
    TRot3f rot;
    rot.identity();
    rot.setRotate(axis, (1.0f / (pointCount - 1)) * (MR::pi() / 2.0f));
    TVec3f vec(rAxis1);

    for (s32 i = 0; i < mPointCount; i++) {
        rot.mult(vec, vec);
        f32 rate = static_cast< f32 >(i + 1) / (pointCount - 1);
        TVec2f tex = rTex1 * (1.0f - rate) + rTex2 * rate;
        mPoints[i] = new OceanSpherePoint(pCenter, vec, i, i, tex);
    }
}

void OceanSpherePlaneEdge::update(f32 radius, f32 wave1Time, f32 wave2Time) {
    for (s32 i = 0; i < mPointCount; i++) {
        mPoints[i]->updatePos(radius, wave1Time, wave2Time);
    }
}

OceanSphere::OceanSphere(const char* pName)
    : LiveActor(pName), mRadius(), mRadiusTarget(), mPointCount(), mAxisPointPX(), mAxisPointNX(), mAxisPointPY(), mAxisPointNY(), mAxisPointPZ(),
      mAxisPointNZ(), mPlaneLeftUpper(), mPlaneRightUpper(), mPlaneLeftLower(), mPlaneRightLower(), mEdge0(), mEdge1(), mEdge2(), mEdge3(), mEdge4(),
      mEdge5(), mEdge6(), mEdge7(), mWaveTime1(), mWaveTime2(), mTexOffs0X(), mTexOffs0Y(), mTexOffs1X(), mTexOffs1Y(), mWaterTex(), mWaterEnvTex() {
    NebulaColor8 front = sOceanSphereTevReg1FrontDefault;
    mTevReg1Front = front;

    NebulaColor8 back = sOceanSphereTevReg1BackDefault;
    mTevReg1Back = back;

    mUseMesh = false;
    mAlwaysUseRealDrawing = false;
    mIsCameraInside = false;
    mIsStartPosCamera = true;
    mIsTearDrop = false;
    mEnableStartPosCameraSwitch = false;
}

void OceanSphere::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanSphere);
    MR::initDefaultPos(this, rIter);
    mRadius = 100.0f * mScale.x;
    mRadiusTarget = mRadius;
    initPoints();
    updatePoints();
    initMesh();
    WaterAreaFunction::entryOceanSphere(this);

    mWaterTex = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "OceanSphere.bti"));
    mWaterEnvTex = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "OceanSphereEnvRef.bti"));

    MR::setClippingTypeSphere(this, mRadius);
    MR::setClippingFarMax(this);
    initNerve(GET_NERVE(OceanSphere, OceanSphereNrvWait));

    if (MR::isEqualStageName("SkullSharkGalaxy")) {
        mEnableStartPosCameraSwitch = true;
        mAlwaysUseRealDrawing = true;
    }

    if (MR::isEqualStageName("TearDropGalaxy")) {
        mIsTearDrop = true;
        mAlwaysUseRealDrawing = true;
        mIsStartPosCamera = false;
        NebulaColor8 front;
        NebulaColor8 back;
        NebulaColor8 frontTear = sOceanSphereTevReg1FrontTear;
        front = frontTear;
        mTevReg1Front = front;

        NebulaColor8 backTear = sOceanSphereTevReg1BackTear;
        back = backTear;
        mTevReg1Back = back;
    }

    if (MR::tryRegisterDemoCast(this, rIter)) {
        makeActorDead();
    } else {
        MR::registerDemoSimpleCastAll(this);
        makeActorAppeared();
    }
}

void OceanSphere::appear() {
    LiveActor::appear();
    mRadius = 1000.0f;
    setNerve(GET_NERVE(OceanSphere, OceanSphereNrvRiseUp));
}

void OceanSphere::exeWait() {
}

void OceanSphere::exeRiseUp() {
    f32 rate = MR::calcDemoPartStepRate("\227\116\202\253\220\205\217\343\217\270" /* 湧き水上昇 */);
    mRadius = 1000.0f + (rate * (mRadiusTarget - 1000.0f));

    if (MR::isDemoPartLastStep("\227\116\202\253\220\205\217\343\217\270" /* 湧き水上昇 */)) {
        setNerve(GET_NERVE(OceanSphere, OceanSphereNrvWait));
    }
}

bool OceanSphere::isInWater(const TVec3f& rPos) const {
    if (MR::isDead(this)) {
        return false;
    }

    return rPos.distance(mPosition) <= mRadius;
}

bool OceanSphere::calcWaterInfo(const TVec3f& rPos, const TVec3f& rGravity, WaterInfo* pInfo) const {
    const TVec3f& rPosition = mPosition;
    const f32 radius = mRadius;
    TVec3f offset = rPos - rPosition;
    const f32 alongGravity = MR::vecKillElement(offset, -rGravity, &offset);
    const f32 height = radius * MR::cos(offset.length() / radius * MR::pi() / 2.0f);
    pInfo->mCamWaterDepth = height - alongGravity;
    pInfo->_4 = height + alongGravity;

    TVec3f normal = rPos - rPosition;
    MR::normalizeOrZero(&normal);
    pInfo->mSurfaceNormal.set(normal);
    pInfo->mSurfacePos.set(rPosition + normal * radius);
    return true;
}

void OceanSphere::initPoints() {
    if (mRadius <= 300.0f) {
        mPointCount = 8;
    } else if (mRadius <= 500.0f) {
        mPointCount = 10;
    } else if (mRadius <= 1000.0f) {
        mPointCount = 15;
    } else {
        mPointCount = 20;
    }

    OceanSpherePoint* pPoint;

    pPoint = new OceanSpherePoint(&mPosition, ::sPosAxisX, 0.0f, 0.0f, TVec2f(1.0f, 0.0f));
    mAxisPointPX = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, -::sPosAxisX, 0.0f, 0.0f, TVec2f(0.0f, 1.0f));
    mAxisPointNX = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, ::sPosAxisY, 0.0f, 0.0f, TVec2f(0.5f, 0.5f));
    mAxisPointPY = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, -::sPosAxisY, 0.0f, 0.0f, TVec2f(0.5f, 0.5f));
    mAxisPointNY = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, ::sPosAxisZ, 0.0f, 0.0f, TVec2f(1.0f, 1.0f));
    mAxisPointPZ = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, -::sPosAxisZ, 0.0f, 0.0f, TVec2f(0.0f, 0.0f));
    mAxisPointNZ = pPoint;

    OceanSpherePlane* pPlane;

    pPlane = new OceanSpherePlane(mPointCount, &mPosition, ::sPosAxisY, -::sPosAxisX, TVec2f(0.0f, 1.0f), TVec2f(0.0f, 0.0f), TVec2f(1.0f, 1.0f));
    mPlaneLeftUpper = pPlane;

    pPlane = new OceanSpherePlane(mPointCount, &mPosition, ::sPosAxisY, ::sPosAxisX, TVec2f(1.0f, 0.0f), TVec2f(1.0f, 1.0f), TVec2f(0.0f, 0.0f));
    mPlaneRightUpper = pPlane;

    pPlane = new OceanSpherePlane(mPointCount, &mPosition, -::sPosAxisY, -::sPosAxisX, TVec2f(0.0f, 1.0f), TVec2f(1.0f, 1.0f), TVec2f(0.0f, 0.0f));
    mPlaneLeftLower = pPlane;

    pPlane = new OceanSpherePlane(mPointCount, &mPosition, -::sPosAxisY, ::sPosAxisX, TVec2f(1.0f, 0.0f), TVec2f(0.0f, 0.0f), TVec2f(1.0f, 1.0f));
    mPlaneRightLower = pPlane;

    mEdge0 = new OceanSpherePlaneEdge(mPointCount, &mPosition, ::sPosAxisY, ::sPosAxisZ, TVec2f(0.5f, 0.5f), TVec2f(1.0f, 1.0f));

    mEdge1 = new OceanSpherePlaneEdge(mPointCount, &mPosition, ::sPosAxisY, -::sPosAxisZ, TVec2f(0.5f, 0.5f), TVec2f(0.0f, 0.0f));

    mEdge2 = new OceanSpherePlaneEdge(mPointCount, &mPosition, -::sPosAxisY, ::sPosAxisZ, TVec2f(0.5f, 0.5f), TVec2f(1.0f, 1.0f));

    mEdge3 = new OceanSpherePlaneEdge(mPointCount, &mPosition, -::sPosAxisY, -::sPosAxisZ, TVec2f(0.5f, 0.5f), TVec2f(0.0f, 0.0f));

    mEdge4 = new OceanSpherePlaneEdge(mPointCount, &mPosition, ::sPosAxisZ, -::sPosAxisX, TVec2f(1.0f, 1.0f), TVec2f(0.0f, 1.0f));

    mEdge5 = new OceanSpherePlaneEdge(mPointCount, &mPosition, ::sPosAxisZ, ::sPosAxisX, TVec2f(1.0f, 1.0f), TVec2f(1.0f, 0.0f));

    mEdge6 = new OceanSpherePlaneEdge(mPointCount, &mPosition, -::sPosAxisZ, -::sPosAxisX, TVec2f(0.0f, 0.0f), TVec2f(0.0f, 1.0f));

    mEdge7 = new OceanSpherePlaneEdge(mPointCount, &mPosition, -::sPosAxisZ, ::sPosAxisX, TVec2f(0.0f, 0.0f), TVec2f(1.0f, 0.0f));
}

void OceanSphere::initMesh() {
    // The face's corners carry the sphere's normals too (loadMaterialFace), the back's only them.
    const u32 corners = 8 * mPointCount * (mPointCount + 1);
    mMeshFace.init(NEBULA_SHAPE_NORMAL | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1, corners);
    drawSphere(false, &mMeshFace);
    mMeshFace.send();
    mMeshBack.init(NEBULA_SHAPE_NORMAL, corners);
    drawSphere(true, &mMeshBack);
    mMeshBack.send();
}

void OceanSphere::control() {
    mIsCameraInside = false;

    if (WaterAreaFunction::getCameraWaterInfo()->mOceanSphere == this) {
        mIsCameraInside = true;

        if (mEnableStartPosCameraSwitch && mIsStartPosCamera && MR::isStartPosCameraEnd()) {
            mIsStartPosCamera = false;
        }
    }

    mWaveTime1 += -0.1f;
    mWaveTime2 += -0.1f;

    if (!mAlwaysUseRealDrawing && MR::calcDistanceToPlayer(this) > 10000.0f) {
        mUseMesh = true;
    } else {
        mUseMesh = false;
        updatePoints();
    }

    const f32 add = 0.0008f;
    const f32 sub = -0.0008f;

    mTexOffs0X = MR::repeat(add + mTexOffs0X, 0.0f, 1.0f);
    mTexOffs0Y = MR::repeat(add + mTexOffs0Y, 0.0f, 1.0f);
    mTexOffs1X = MR::repeat(add + mTexOffs1X, 0.0f, 1.0f);
    mTexOffs1Y = MR::repeat(sub + mTexOffs1Y, 0.0f, 1.0f);
}

void OceanSphere::updatePoints() {
    mAxisPointPX->updatePos(mRadius, mWaveTime1, mWaveTime2);
    mAxisPointNX->updatePos(mRadius, mWaveTime1, mWaveTime2);
    mAxisPointPY->updatePos(mRadius, mWaveTime1, mWaveTime2);
    mAxisPointNY->updatePos(mRadius, mWaveTime1, mWaveTime2);
    mAxisPointPZ->updatePos(mRadius, mWaveTime1, mWaveTime2);
    mAxisPointNZ->updatePos(mRadius, mWaveTime1, mWaveTime2);

    mPlaneLeftUpper->update(mRadius, mWaveTime1, mWaveTime2);
    mPlaneRightUpper->update(mRadius, mWaveTime1, mWaveTime2);
    mPlaneLeftLower->update(mRadius, mWaveTime1, mWaveTime2);
    mPlaneRightLower->update(mRadius, mWaveTime1, mWaveTime2);

    mEdge0->update(mRadius, mWaveTime1, mWaveTime2);
    mEdge1->update(mRadius, mWaveTime1, mWaveTime2);
    mEdge2->update(mRadius, mWaveTime1, mWaveTime2);
    mEdge3->update(mRadius, mWaveTime1, mWaveTime2);
    mEdge4->update(mRadius, mWaveTime1, mWaveTime2);
    mEdge5->update(mRadius, mWaveTime1, mWaveTime2);
    mEdge6->update(mRadius, mWaveTime1, mWaveTime2);
    mEdge7->update(mRadius, mWaveTime1, mWaveTime2);
}

OceanSpherePoint* OceanSphere::getPlanePointLeftUpper(int row, int col) const {
    int max;

    if (row != 0 && col != 0) {
        max = mPointCount - 1;

        if (row != max && col != max) {
            return mPlaneLeftUpper->getPoint(row - 1, col - 1);
        }
    }

    if (row == 0) {
        if (col == 0) {
            return mAxisPointPY;
        }

        max = mPointCount - 1;
        if (col == max) {
            return mAxisPointNZ;
        }

        return mEdge1->mPoints[col - 1];
    }

    if (col == 0) {
        max = mPointCount - 1;
        if (row == max) {
            return mAxisPointPZ;
        }

        return mEdge0->mPoints[row - 1];
    }

    max = mPointCount - 1;
    if (row == max) {
        if (col == max) {
            return mAxisPointNX;
        }

        return mEdge4->mPoints[col - 1];
    }

    return mEdge6->mPoints[row - 1];
}

OceanSpherePoint* OceanSphere::getPlanePointRightUpper(int row, int col) const {
    int max;

    if (row != 0 && col != 0) {
        max = mPointCount - 1;

        if (row != max && col != max) {
            return mPlaneRightUpper->getPoint(row - 1, col - 1);
        }
    }

    if (row == 0) {
        if (col == 0) {
            return mAxisPointPY;
        }

        max = mPointCount - 1;
        if (col == max) {
            return mAxisPointPZ;
        }

        return mEdge0->mPoints[col - 1];
    }

    if (col == 0) {
        max = mPointCount - 1;
        if (row == max) {
            return mAxisPointNZ;
        }

        return mEdge1->mPoints[row - 1];
    }

    max = mPointCount - 1;
    if (row == max) {
        if (col == max) {
            return mAxisPointPX;
        }

        return mEdge7->mPoints[col - 1];
    }

    return mEdge5->mPoints[row - 1];
}

OceanSpherePoint* OceanSphere::getPlanePointLeftLower(int row, int col) const {
    int max;

    if (row != 0 && col != 0) {
        max = mPointCount - 1;

        if (row != max && col != max) {
            return mPlaneLeftLower->getPoint(row - 1, col - 1);
        }
    }

    if (row == 0) {
        if (col == 0) {
            return mAxisPointNY;
        }

        max = mPointCount - 1;
        if (col == max) {
            return mAxisPointPZ;
        }

        return mEdge2->mPoints[col - 1];
    }

    if (col == 0) {
        max = mPointCount - 1;
        if (row == max) {
            return mAxisPointNZ;
        }

        return mEdge3->mPoints[row - 1];
    }

    max = mPointCount - 1;
    if (row == max) {
        if (col == max) {
            return mAxisPointNX;
        }

        return mEdge6->mPoints[col - 1];
    }

    return mEdge4->mPoints[row - 1];
}

OceanSpherePoint* OceanSphere::getPlanePointRightLower(int row, int col) const {
    int max;

    if (row != 0 && col != 0) {
        max = mPointCount - 1;

        if (row != max && col != max) {
            return mPlaneRightLower->getPoint(row - 1, col - 1);
        }
    }

    if (row == 0) {
        if (col == 0) {
            return mAxisPointNY;
        }

        max = mPointCount - 1;
        if (col == max) {
            return mAxisPointNZ;
        }

        return mEdge3->mPoints[col - 1];
    }

    if (col == 0) {
        max = mPointCount - 1;
        if (row == max) {
            return mAxisPointPZ;
        }

        return mEdge2->mPoints[row - 1];
    }

    max = mPointCount - 1;
    if (row == max) {
        if (col == max) {
            return mAxisPointPX;
        }

        return mEdge5->mPoints[col - 1];
    }

    return mEdge7->mPoints[row - 1];
}

namespace {
    // runtime/src/render/materials/ocean_sphere.hlsl with its four colours: blended by alpha, depth tested and not
    // written, `cull` (NEBULA_CULL_*), no fog.
    void sendFaceMaterial(JUTTexture* pTexture, const NebulaColor8& rHighlight, const NebulaColor8& rDeep, const NebulaColor8& rShallow,
                          const NebulaColor8& rTint, u8 cull) {
        NebulaNativeMaterial material;
        NativeShader::init(material, NEBULA_SHADER_OCEAN_SPHERE, NativeShader::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                           cull);
        NativeShader::setColor(material, 0, rHighlight.r, rHighlight.g, rHighlight.b, rHighlight.a);
        NativeShader::setColor(material, 4, rDeep.r, rDeep.g, rDeep.b, rDeep.a);
        NativeShader::setColor(material, 8, rShallow.r, rShallow.g, rShallow.b, rShallow.a);
        NativeShader::setColor(material, 12, rTint.r, rTint.g, rTint.b, rTint.a);
        NativeShader::setTexture(material, 0, pTexture);
        NativeShader::send(material);
    }
}  // namespace

void OceanSphere::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    if (mIsStartPosCamera && !mIsCameraInside) {
        loadMaterialBack();

        if (mUseMesh) {
            mMeshBack.draw();
        } else {
            drawSphere(true, nullptr);
        }
    }

    loadMaterialFace();

    if (mIsStartPosCamera) {
        if (mIsCameraInside) {
            sendFaceMaterial(mWaterTex, sOceanSphereTevReg0Face, mTevReg1Front, sOceanSphereTevReg2Face,
                             sOceanSphereKColor0Face, NEBULA_CULL_FRONT);
        }

        if (mUseMesh) {
            mMeshFace.draw();
        } else {
            drawSphere(false, nullptr);
        }

        return;
    }

    sendFaceMaterial(mWaterTex, sOceanSphereTevReg0Face, mTevReg1Front, sOceanSphereTevReg2Face, sOceanSphereKColor0Face,
                     NEBULA_CULL_FRONT);
    drawSphere(false, nullptr);

    if (mIsCameraInside) {
        return;
    }

    sendFaceMaterial(mWaterTex, sOceanSphereTevReg0Back, mTevReg1Back, sOceanSphereTevReg2Back, sOceanSphereKColor0Back,
                     NEBULA_CULL_BACK);
    drawSphere(false, nullptr);
}


void OceanSphere::loadMaterialFace() const {
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    J3DNativeShape::setNormal(0, MR::getCameraViewMtx());
    J3DNativeShape::setCurrentIdentity();
    sendFaceMaterial(mWaterTex, sOceanSphereTevReg0Face, mTevReg1Front, sOceanSphereTevReg2Face, sOceanSphereKColor0Face,
                     NEBULA_CULL_BACK);
}

void OceanSphere::loadMaterialBack() const {
    J3DNativeShape::setPosition(0, MR::getCameraViewMtx());
    TPos3f pos;
    pos.set(MR::getCameraViewMtx());
    pos.zeroTrans();
    pos.scale(0.779175f);
    J3DNativeShape::setTexture(0, pos.toMtxPtr(), 2);
    static const u8 kPlaces[8] = {0, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture,
                                  J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture, J3DNativeShape::kIdentityTexture};
    J3DNativeShape::setCurrent(0, kPlaces);
    // runtime/src/render/materials/ocean_sphere_env.hlsl: the environment map by the normal from sOceanSphereTevReg0Env
    // toward sOceanSphereTevReg1Env; opaque, depth tested and written, front faces culled, no fog.
    NebulaNativeMaterial material;
    NativeShader::init(material, NEBULA_SHADER_OCEAN_SPHERE_ENV, NativeShader::BLEND_OPAQUE, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_FRONT);
    const NebulaColor8& rLow = sOceanSphereTevReg0Env;
    const NebulaColor8& rHigh = sOceanSphereTevReg1Env;
    NativeShader::setColor(material, 0, rLow.r, rLow.g, rLow.b, rLow.a);
    NativeShader::setColor(material, 4, rHigh.r, rHigh.g, rHigh.b, rHigh.a);
    NativeShader::setTexture(material, 0, mWaterEnvTex);
    NativeShader::send(material);
}

void OceanSphere::drawSphere(bool useEnvMap, NativeMesh* pMesh) const {
    const OceanSpherePoint* pPoint;
    const u32 attributes = useEnvMap ? NEBULA_SHAPE_NORMAL : NEBULA_SHAPE_NORMAL | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1;

    for (s32 i = 0; i < mPointCount - 1; i++) {
        ::beginDrawPolygon(i, mPointCount, pMesh, attributes);
        sendVertex(getPlanePointLeftUpper(i, i), useEnvMap, pMesh);

        for (s32 j = i + 1; j < mPointCount; j++) {
            pPoint = getPlanePointLeftUpper(i, j);
            sendVertex(getPlanePointLeftUpper(i + 1, j), useEnvMap, pMesh);
            sendVertex(pPoint, useEnvMap, pMesh);
        }


        ::beginDrawPolygon(i, mPointCount, pMesh, attributes);

        for (s32 j = 0; j < mPointCount - i - 1; j++) {
            pPoint = getPlanePointLeftUpper(mPointCount - 1 - j, i + 1);
            sendVertex(getPlanePointLeftUpper(mPointCount - 1 - j, i), useEnvMap, pMesh);
            sendVertex(pPoint, useEnvMap, pMesh);
        }

        sendVertex(getPlanePointLeftUpper(i, i), useEnvMap, pMesh);

        ::beginDrawPolygon(i, mPointCount, pMesh, attributes);
        sendVertex(getPlanePointRightUpper(i, i), useEnvMap, pMesh);

        for (s32 j = i + 1; j < mPointCount; j++) {
            pPoint = getPlanePointRightUpper(i, j);
            sendVertex(getPlanePointRightUpper(i + 1, j), useEnvMap, pMesh);
            sendVertex(pPoint, useEnvMap, pMesh);
        }


        ::beginDrawPolygon(i, mPointCount, pMesh, attributes);

        for (s32 j = 0; j < mPointCount - i - 1; j++) {
            pPoint = getPlanePointRightUpper(mPointCount - 1 - j, i + 1);
            sendVertex(getPlanePointRightUpper(mPointCount - 1 - j, i), useEnvMap, pMesh);
            sendVertex(pPoint, useEnvMap, pMesh);
        }

        sendVertex(getPlanePointRightUpper(i, i), useEnvMap, pMesh);

        ::beginDrawPolygon(i, mPointCount, pMesh, attributes);
        sendVertex(getPlanePointLeftLower(i, i), useEnvMap, pMesh);

        for (s32 j = i + 1; j < mPointCount; j++) {
            pPoint = getPlanePointLeftLower(i, j);
            sendVertex(getPlanePointLeftLower(i + 1, j), useEnvMap, pMesh);
            sendVertex(pPoint, useEnvMap, pMesh);
        }


        ::beginDrawPolygon(i, mPointCount, pMesh, attributes);

        for (s32 j = 0; j < mPointCount - i - 1; j++) {
            pPoint = getPlanePointLeftLower(mPointCount - 1 - j, i + 1);
            sendVertex(getPlanePointLeftLower(mPointCount - 1 - j, i), useEnvMap, pMesh);
            sendVertex(pPoint, useEnvMap, pMesh);
        }

        sendVertex(getPlanePointLeftLower(i, i), useEnvMap, pMesh);

        ::beginDrawPolygon(i, mPointCount, pMesh, attributes);
        sendVertex(getPlanePointRightLower(i, i), useEnvMap, pMesh);

        for (s32 j = i + 1; j < mPointCount; j++) {
            pPoint = getPlanePointRightLower(i, j);
            sendVertex(getPlanePointRightLower(i + 1, j), useEnvMap, pMesh);
            sendVertex(pPoint, useEnvMap, pMesh);
        }


        ::beginDrawPolygon(i, mPointCount, pMesh, attributes);

        for (s32 j = 0; j < mPointCount - i - 1; j++) {
            pPoint = getPlanePointRightLower(mPointCount - 1 - j, i + 1);
            sendVertex(getPlanePointRightLower(mPointCount - 1 - j, i), useEnvMap, pMesh);
            sendVertex(pPoint, useEnvMap, pMesh);
        }

        sendVertex(getPlanePointRightLower(i, i), useEnvMap, pMesh);
    }
}

void OceanSphere::sendVertex(const OceanSpherePoint* pPoint, bool useEnvMap, NativeMesh* pMesh) const {
    if (pMesh != nullptr) {
        pMesh->position(pPoint->mPos.x, pPoint->mPos.y, pPoint->mPos.z);
        pMesh->normal(pPoint->mNormal.x, pPoint->mNormal.y, pPoint->mNormal.z);
        if (!useEnvMap) {
            pMesh->texCoord(0, mTexOffs0X + pPoint->mTexCoord.x, mTexOffs0Y + pPoint->mTexCoord.y);
            pMesh->texCoord(1, mTexOffs1X + pPoint->mTexCoord.x, mTexOffs1Y + pPoint->mTexCoord.y);
        }
        pMesh->corner();
    } else {
        // The face's vertices carry the sphere's normals too (loadMaterialFace), the back's only them.
        J3DNativeDraw::position(pPoint->mPos.x, pPoint->mPos.y, pPoint->mPos.z);
        J3DNativeDraw::normal(pPoint->mNormal.x, pPoint->mNormal.y, pPoint->mNormal.z);
        if (!useEnvMap) {
            J3DNativeDraw::texCoord(mTexOffs0X + pPoint->mTexCoord.x, mTexOffs0Y + pPoint->mTexCoord.y);
            J3DNativeDraw::texCoord(mTexOffs1X + pPoint->mTexCoord.x, mTexOffs1Y + pPoint->mTexCoord.y);
        }
    }
}
