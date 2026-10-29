#include "Game/Map/OceanSphere.hpp"
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
#include "Game/Util/SchedulerUtil.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>
#include <JSystem/JKernel/JKRHeap.hpp>
#include <JSystem/JUtility/JUTTexture.hpp>
#include <revolution/gx/GXVert.h>

GXColor sOceanSphereTevReg0Face = {0x48, 0x80, 0xBE, 0x1C};
GXColor sOceanSphereTevReg1FrontDefault = {0x00, 0x51, 0x70, 0x6F};
GXColor sOceanSphereTevReg1FrontTear = {0x00, 0x64, 0xFF, 0x6F};
GXColor sOceanSphereTevReg2Face = {0xFF, 0xFF, 0xFF, 0xFF};
GXColor sOceanSphereKColor0Face = {0x78, 0xFF, 0xFF, 0x00};
GXColor sOceanSphereTevReg0Back = {0x48, 0x80, 0xBE, 0x1C};
GXColor sOceanSphereTevReg1BackDefault = {0x00, 0x51, 0x70, 0x6F};
GXColor sOceanSphereTevReg1BackTear = {0x00, 0xC3, 0xFF, 0x6F};
GXColor sOceanSphereTevReg2Back = {0xFF, 0xFF, 0xFF, 0xFF};
GXColor sOceanSphereKColor0Back = {0x78, 0xFF, 0xFF, 0x00};
GXColor sOceanSphereTevReg0Env = {0x00, 0x69, 0xB8, 0x14};
GXColor sOceanSphereTevReg1Env = {0x00, 0x00, 0x00, 0xFF};

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
            pMesh->begin(GX_TRIANGLESTRIP, vtxCount);
        } else {
            J3DNativeDraw::begin(GX_TRIANGLESTRIP, vtxCount, attributes);
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
    GXColor front = sOceanSphereTevReg1FrontDefault;
    mTevReg1Front = front;

    GXColor back = sOceanSphereTevReg1BackDefault;
    mTevReg1Back = back;

    mUseDisplayList = false;
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
    initDisplayList();
    WaterAreaFunction::entryOceanSphere(this);

    mWaterTex = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "OceanSphere.bti"), 0);
    mWaterEnvTex = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "OceanSphereEnvRef.bti"), 0);

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
        GXColor front;
        GXColor back;
        GXColor frontTear = sOceanSphereTevReg1FrontTear;
        front = frontTear;
        mTevReg1Front = front;

        GXColor backTear = sOceanSphereTevReg1BackTear;
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

#pragma push
#pragma opt_propagation off
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
#pragma pop

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

void OceanSphere::initDisplayList() {
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
        mUseDisplayList = true;
    } else {
        mUseDisplayList = false;
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

void OceanSphere::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    if (mIsStartPosCamera && !mIsCameraInside) {
        loadMaterialBack();

        if (mUseDisplayList) {
            mMeshBack.draw();
        } else {
            drawSphere(true, nullptr);
        }
    }

    loadMaterialFace();

    if (mIsStartPosCamera) {
        if (mIsCameraInside) {
            J3DNativeState::setCullMode(GX_CULL_FRONT);
        }

        if (mUseDisplayList) {
            mMeshFace.draw();
        } else {
            drawSphere(false, nullptr);
        }

        return;
    }

    J3DNativeState::setCullMode(GX_CULL_FRONT);
    drawSphere(false, nullptr);

    if (mIsCameraInside) {
        return;
    }

    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setTevColor(GX_TEVREG0, sOceanSphereTevReg0Back);
    J3DNativeState::setTevColor(GX_TEVREG1, mTevReg1Back);
    J3DNativeState::setTevColor(GX_TEVREG2, sOceanSphereTevReg2Back);
    J3DNativeState::setTevKColor(GX_KCOLOR0, sOceanSphereKColor0Back);
    drawSphere(false, nullptr);
}

const GXColor sOceanSphereFogFace = {0xFF, 0xFF, 0xFF, 0};
const GXColor sOceanSphereFogBack = {0xFF, 0xFF, 0xFF, 0};

void OceanSphere::loadMaterialFace() const {
    J3DNativeShape::sendPosition(MR::getCameraViewMtx(), 0);
    // The face's vertices carry the sphere's normals (unlit: the picture is unchanged), so ray tracing shades the
    // water as the sphere it is rather than as its flat triangles.
    J3DNativeShape::sendNormal(MR::getCameraViewMtx(), 0);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setNumChans(1);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(2);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX3x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX3x4, GX_TG_TEX1, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    mWaterTex->load(GX_TEXMAP0);
    J3DNativeState::setNumIndStages(0);
    J3DNativeState::setNumTevStages(4);
    J3DNativeState::setTevColor(GX_TEVREG0, sOceanSphereTevReg0Face);
    J3DNativeState::setTevColor(GX_TEVREG1, mTevReg1Front);
    J3DNativeState::setTevColor(GX_TEVREG2, sOceanSphereTevReg2Face);
    J3DNativeState::setTevKColor(GX_KCOLOR0, sOceanSphereKColor0Face);
    J3DNativeState::setTevKColorSel(GX_TEVSTAGE0, GX_TEV_KCSEL_K0);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE2, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE2, GX_CC_CPREV, GX_CC_A0, GX_CC_C0, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE2, GX_TEV_COMP_R8_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE3, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE3, GX_CC_C1, GX_CC_C2, GX_CC_CPREV, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE3, GX_CA_A1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setCullMode(GX_CULL_BACK);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, sOceanSphereFogFace);
}

void OceanSphere::loadMaterialBack() const {
    J3DNativeShape::sendPosition(MR::getCameraViewMtx(), 0);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setNumChans(0);
    J3DNativeState::setChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setChanCtrl(GX_COLOR1A1, GX_FALSE, GX_SRC_REG, GX_SRC_REG, 0, GX_DF_NONE, GX_AF_NONE);
    J3DNativeState::setNumTexGens(1);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_NRM, GX_TEXMTX0, GX_FALSE, GX_PTIDENTITY);

    TPos3f pos;
    pos.set(MR::getCameraViewMtx());
    pos.zeroTrans();
    pos.scale(0.779175f);
    J3DNativeShape::sendTexture(pos.toMtxPtr(), GX_TEXMTX0, GX_MTX2x4);

    mWaterEnvTex->load(GX_TEXMAP0);
    J3DNativeState::setNumIndStages(0);
    J3DNativeState::setNumTevStages(1);
    J3DNativeState::setTevColor(GX_TEVREG0, sOceanSphereTevReg0Env);
    J3DNativeState::setTevColor(GX_TEVREG1, sOceanSphereTevReg1Env);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_C1, GX_CC_TEXC, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_ONE, GX_BL_ZERO, GX_LO_NOOP);
    J3DNativeState::setAlphaCompare(GX_ALWAYS, 0, GX_AOP_OR, GX_ALWAYS, 0);
    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setCullMode(GX_CULL_FRONT);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
    J3DNativeState::setFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, sOceanSphereFogBack);
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
