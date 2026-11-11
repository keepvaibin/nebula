#include "Game/Map/OceanSphere.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
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
#include <nebula_geometry.hpp>
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/GameTexture.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_OceanSpherePlane_f1, "OceanSpherePlane.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OceanSpherePlane_f2, "OceanSpherePlane.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_OceanSpherePlane_f3, "OceanSpherePlane.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OceanSpherePlaneEdge_f1, "OceanSpherePlaneEdge.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_appear_f1, "appear.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRiseUp_f1, "exeRiseUp.f1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRiseUp_f2, "exeRiseUp.f2", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWaterInfo_f1, "calcWaterInfo.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f1, "initPoints.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f2, "initPoints.f2", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f3, "initPoints.f3", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f4, "initPoints.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f5, "initPoints.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f6, "initPoints.f6", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f7, "initPoints.f7", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f8, "initPoints.f8", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f9, "initPoints.f9", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f10, "initPoints.f10", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f11, "initPoints.f11", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f12, "initPoints.f12", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f13, "initPoints.f13", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f14, "initPoints.f14", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f15, "initPoints.f15", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 10000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f4, "control.f4", 0.0008f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f5, "control.f5", 0.0008f);
NEBULA_TUNABLE_NAMED(f32, cNum_loadMaterialBack_f1, "loadMaterialBack.f1", 0.779175f);

NebulaColor8 sOceanSphereStageColor0Face = {0x48, 0x80, 0xBE, 0x1C};
NebulaColor8 sOceanSphereStageColor1FrontDefault = {0x00, 0x51, 0x70, 0x6F};
NebulaColor8 sOceanSphereStageColor1FrontTear = {0x00, 0x64, 0xFF, 0x6F};
NebulaColor8 sOceanSphereStageColor2Face = {0xFF, 0xFF, 0xFF, 0xFF};
NebulaColor8 sOceanSphereKColor0Face = {0x78, 0xFF, 0xFF, 0x00};
NebulaColor8 sOceanSphereStageColor0Back = {0x48, 0x80, 0xBE, 0x1C};
NebulaColor8 sOceanSphereStageColor1BackDefault = {0x00, 0x51, 0x70, 0x6F};
NebulaColor8 sOceanSphereStageColor1BackTear = {0x00, 0xC3, 0xFF, 0x6F};
NebulaColor8 sOceanSphereStageColor2Back = {0xFF, 0xFF, 0xFF, 0xFF};
NebulaColor8 sOceanSphereKColor0Back = {0x78, 0xFF, 0xFF, 0x00};
NebulaColor8 sOceanSphereStageColor0Env = {0x00, 0x69, 0xB8, 0x14};
NebulaColor8 sOceanSphereStageColor1Env = {0x00, 0x00, 0x00, 0xFF};

namespace NrvOceanSphere {
    NEW_NERVE(OceanSphereNrvRiseUp, OceanSphere, RiseUp);
    NEW_NERVE(OceanSphereNrvWait, OceanSphere, Wait);
};  // namespace NrvOceanSphere

namespace {
    Vec sAxisX = {1.0f, 0.0f, 0.0f};
    Vec sAxisY = {0.0f, 1.0f, 0.0f};
    Vec sAxisZ = {0.0f, 0.0f, 1.0f};

    Vec3f sPosAxisX(sAxisX);
    Vec3f sPosAxisY(sAxisY);
    Vec3f sPosAxisZ(sAxisZ);
};  // namespace

namespace {
    // `pMesh`: the mesh recording the sphere (null to draw it now, its vertices giving `attributes`).
    u16 stripCount(int idx, s32 pointCount) {
        return (u16)(((pointCount - 1 - idx) * 2 + 1));
    }
};  // namespace

OceanSpherePlane::OceanSpherePlane(s32 pointCount, const Vec3f* pCenter, const Vec3f& rAxis1, const Vec3f& rAxis2, const Vec2f& rTex1,
                                   const Vec2f& rTex2, const Vec2f& rTex3) {
    mPoints = nullptr;
    mAxisPointCount = pointCount - 2;
    mGridPointCount = mAxisPointCount * mAxisPointCount;
    mPoints = new OceanSpherePoint*[mGridPointCount];

    Rot3f rot;
    rot.identity();
    Vec2f texCenter(cNum_OceanSpherePlane_f1, cNum_OceanSpherePlane_f2);

    for (s32 row = 1; row < pointCount - 1; row++) {
        rot.setRotate(rAxis2, (static_cast< f32 >(row) / (pointCount - 1)) * (MR::pi() / cNum_OceanSpherePlane_f3));
        Vec3f vec(rAxis1);
        rot.mult(vec, vec);
        f32 rowRate = static_cast< f32 >(row) / (pointCount - 1);
        Vec2f texA = texCenter * (1.0f - rowRate) + rTex1 * rowRate;
        Vec2f texB = texCenter * (1.0f - rowRate) + rTex2 * rowRate;
        Vec2f texC = texCenter * (1.0f - rowRate) + rTex3 * rowRate;

        for (s32 col = 1; col < 2 * row; col++) {
            rot.setRotate(rAxis1, (static_cast< f32 >(col) / (2 * row)) * MR::pi());
            Vec3f normal(vec);
            rot.mult(normal, normal);
            Vec2f tex(0.0f, 0.0f);
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

OceanSpherePlaneEdge::OceanSpherePlaneEdge(s32 pointCount, const Vec3f* pCenter, const Vec3f& rAxis1, const Vec3f& rAxis2, const Vec2f& rTex1,
                                           const Vec2f& rTex2) {
    mPoints = nullptr;
    mPointCount = pointCount - 2;
    mPoints = new OceanSpherePoint*[mPointCount];
    Vec3f axis = rAxis1.cross(rAxis2);
    MR::normalize(&axis);
    Rot3f rot;
    rot.identity();
    rot.setRotate(axis, (1.0f / (pointCount - 1)) * (MR::pi() / cNum_OceanSpherePlaneEdge_f1));
    Vec3f vec(rAxis1);

    for (s32 i = 0; i < mPointCount; i++) {
        rot.mult(vec, vec);
        f32 rate = static_cast< f32 >(i + 1) / (pointCount - 1);
        Vec2f tex = rTex1 * (1.0f - rate) + rTex2 * rate;
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
    lumacore::setClass(this, lumacore::kClassId< OceanSphere >);
    NebulaColor8 front = sOceanSphereStageColor1FrontDefault;
    mStageColor1Front = front;

    NebulaColor8 back = sOceanSphereStageColor1BackDefault;
    mStageColor1Back = back;

    mUseMesh = false;
    mAlwaysUseRealDrawing = false;
    mIsCameraInside = false;
    mIsStartPosCamera = true;
    mEnableStartPosCameraSwitch = false;
}

void OceanSphere::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanSphere);
    MR::initDefaultPos(this, rIter);
    mRadius = cNum_init_f1 * mScale.x;
    mRadiusTarget = mRadius;
    initPoints();
    updatePoints();
    initMesh();
    WaterAreaFunction::entryOceanSphere(this);

    mWaterTex = new GameTexture(MR::loadTexFromArc("WaterWave", "OceanSphere.bti"));
    mWaterEnvTex = new GameTexture(MR::loadTexFromArc("WaterWave", "OceanSphereEnvRef.bti"));

    MR::setClippingTypeSphere(this, mRadius);
    MR::setClippingFarMax(this);
    initNerve(GET_NERVE(OceanSphere, OceanSphereNrvWait));

    if (MR::isEqualStageName("SkullSharkGalaxy")) {
        mEnableStartPosCameraSwitch = true;
        mAlwaysUseRealDrawing = true;
    }

    if (MR::isEqualStageName("TearDropGalaxy")) {
        mAlwaysUseRealDrawing = true;
        mIsStartPosCamera = false;
        NebulaColor8 front;
        NebulaColor8 back;
        NebulaColor8 frontTear = sOceanSphereStageColor1FrontTear;
        front = frontTear;
        mStageColor1Front = front;

        NebulaColor8 backTear = sOceanSphereStageColor1BackTear;
        back = backTear;
        mStageColor1Back = back;
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
    mRadius = cNum_appear_f1;
    setNerve(GET_NERVE(OceanSphere, OceanSphereNrvRiseUp));
}

void OceanSphere::exeWait() {
}

void OceanSphere::exeRiseUp() {
    f32 rate = MR::calcDemoPartStepRate("湧き水上昇");
    mRadius = cNum_exeRiseUp_f1 + (rate * (mRadiusTarget - cNum_exeRiseUp_f2));

    if (MR::isDemoPartLastStep("湧き水上昇")) {
        setNerve(GET_NERVE(OceanSphere, OceanSphereNrvWait));
    }
}

bool OceanSphere::isInWater(const Vec3f& rPos) const {
    if (MR::isDead(this)) {
        return false;
    }

    return rPos.distance(mPosition) <= mRadius;
}

bool OceanSphere::calcWaterInfo(const Vec3f& rPos, const Vec3f& rGravity, WaterInfo* pInfo) const {
    const Vec3f& rPosition = mPosition;
    const f32 radius = mRadius;
    Vec3f offset = rPos - rPosition;
    const f32 alongGravity = MR::vecKillElement(offset, -rGravity, &offset);
    // A sphere placed with no scale (radius 0) holds no water: height 0, not a division by 0.
    const f32 reach = radius > 0.0f ? offset.length() / radius : 1.0f;
    const f32 height = radius * MR::cos(reach * MR::pi() / cNum_calcWaterInfo_f1);
    pInfo->mCamWaterDepth = height - alongGravity;
    pInfo->mDepth = height + alongGravity;

    Vec3f normal = rPos - rPosition;
    MR::normalizeOrZero(&normal);
    pInfo->mSurfaceNormal.set(normal);
    pInfo->mSurfacePos.set(rPosition + normal * radius);
    return true;
}

void OceanSphere::initPoints() {
    if (mRadius <= cNum_initPoints_f1) {
        mPointCount = 8;
    } else if (mRadius <= cNum_initPoints_f2) {
        mPointCount = 10;
    } else if (mRadius <= cNum_initPoints_f3) {
        mPointCount = 15;
    } else {
        mPointCount = 20;
    }

    OceanSpherePoint* pPoint;

    pPoint = new OceanSpherePoint(&mPosition, ::sPosAxisX, 0.0f, 0.0f, Vec2f(1.0f, 0.0f));
    mAxisPointPX = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, -::sPosAxisX, 0.0f, 0.0f, Vec2f(0.0f, 1.0f));
    mAxisPointNX = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, ::sPosAxisY, 0.0f, 0.0f, Vec2f(cNum_initPoints_f4, cNum_initPoints_f5));
    mAxisPointPY = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, -::sPosAxisY, 0.0f, 0.0f, Vec2f(cNum_initPoints_f6, cNum_initPoints_f7));
    mAxisPointNY = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, ::sPosAxisZ, 0.0f, 0.0f, Vec2f(1.0f, 1.0f));
    mAxisPointPZ = pPoint;

    pPoint = new OceanSpherePoint(&mPosition, -::sPosAxisZ, 0.0f, 0.0f, Vec2f(0.0f, 0.0f));
    mAxisPointNZ = pPoint;

    OceanSpherePlane* pPlane;

    pPlane = new OceanSpherePlane(mPointCount, &mPosition, ::sPosAxisY, -::sPosAxisX, Vec2f(0.0f, 1.0f), Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f));
    mPlaneLeftUpper = pPlane;

    pPlane = new OceanSpherePlane(mPointCount, &mPosition, ::sPosAxisY, ::sPosAxisX, Vec2f(1.0f, 0.0f), Vec2f(1.0f, 1.0f), Vec2f(0.0f, 0.0f));
    mPlaneRightUpper = pPlane;

    pPlane = new OceanSpherePlane(mPointCount, &mPosition, -::sPosAxisY, -::sPosAxisX, Vec2f(0.0f, 1.0f), Vec2f(1.0f, 1.0f), Vec2f(0.0f, 0.0f));
    mPlaneLeftLower = pPlane;

    pPlane = new OceanSpherePlane(mPointCount, &mPosition, -::sPosAxisY, ::sPosAxisX, Vec2f(1.0f, 0.0f), Vec2f(0.0f, 0.0f), Vec2f(1.0f, 1.0f));
    mPlaneRightLower = pPlane;

    mEdge0 = new OceanSpherePlaneEdge(mPointCount, &mPosition, ::sPosAxisY, ::sPosAxisZ, Vec2f(cNum_initPoints_f8, cNum_initPoints_f9), Vec2f(1.0f, 1.0f));

    mEdge1 = new OceanSpherePlaneEdge(mPointCount, &mPosition, ::sPosAxisY, -::sPosAxisZ, Vec2f(cNum_initPoints_f10, cNum_initPoints_f11), Vec2f(0.0f, 0.0f));

    mEdge2 = new OceanSpherePlaneEdge(mPointCount, &mPosition, -::sPosAxisY, ::sPosAxisZ, Vec2f(cNum_initPoints_f12, cNum_initPoints_f13), Vec2f(1.0f, 1.0f));

    mEdge3 = new OceanSpherePlaneEdge(mPointCount, &mPosition, -::sPosAxisY, -::sPosAxisZ, Vec2f(cNum_initPoints_f14, cNum_initPoints_f15), Vec2f(0.0f, 0.0f));

    mEdge4 = new OceanSpherePlaneEdge(mPointCount, &mPosition, ::sPosAxisZ, -::sPosAxisX, Vec2f(1.0f, 1.0f), Vec2f(0.0f, 1.0f));

    mEdge5 = new OceanSpherePlaneEdge(mPointCount, &mPosition, ::sPosAxisZ, ::sPosAxisX, Vec2f(1.0f, 1.0f), Vec2f(1.0f, 0.0f));

    mEdge6 = new OceanSpherePlaneEdge(mPointCount, &mPosition, -::sPosAxisZ, -::sPosAxisX, Vec2f(0.0f, 0.0f), Vec2f(0.0f, 1.0f));

    mEdge7 = new OceanSpherePlaneEdge(mPointCount, &mPosition, -::sPosAxisZ, ::sPosAxisX, Vec2f(0.0f, 0.0f), Vec2f(1.0f, 0.0f));
}

template < class T, class Begin >
void OceanSphere::writeSphere(Begin begin) const {
    T* vtx;
    const OceanSpherePoint* pPoint;

    for (s32 i = 0; i < mPointCount - 1; i++) {
        vtx = begin(::stripCount(i, mPointCount));
        writeVertex(getPlanePointLeftUpper(i, i), vtx);

        for (s32 j = i + 1; j < mPointCount; j++) {
            pPoint = getPlanePointLeftUpper(i, j);
            writeVertex(getPlanePointLeftUpper(i + 1, j), vtx);
            writeVertex(pPoint, vtx);
        }


        vtx = begin(::stripCount(i, mPointCount));

        for (s32 j = 0; j < mPointCount - i - 1; j++) {
            pPoint = getPlanePointLeftUpper(mPointCount - 1 - j, i + 1);
            writeVertex(getPlanePointLeftUpper(mPointCount - 1 - j, i), vtx);
            writeVertex(pPoint, vtx);
        }

        writeVertex(getPlanePointLeftUpper(i, i), vtx);

        vtx = begin(::stripCount(i, mPointCount));
        writeVertex(getPlanePointRightUpper(i, i), vtx);

        for (s32 j = i + 1; j < mPointCount; j++) {
            pPoint = getPlanePointRightUpper(i, j);
            writeVertex(getPlanePointRightUpper(i + 1, j), vtx);
            writeVertex(pPoint, vtx);
        }


        vtx = begin(::stripCount(i, mPointCount));

        for (s32 j = 0; j < mPointCount - i - 1; j++) {
            pPoint = getPlanePointRightUpper(mPointCount - 1 - j, i + 1);
            writeVertex(getPlanePointRightUpper(mPointCount - 1 - j, i), vtx);
            writeVertex(pPoint, vtx);
        }

        writeVertex(getPlanePointRightUpper(i, i), vtx);

        vtx = begin(::stripCount(i, mPointCount));
        writeVertex(getPlanePointLeftLower(i, i), vtx);

        for (s32 j = i + 1; j < mPointCount; j++) {
            pPoint = getPlanePointLeftLower(i, j);
            writeVertex(getPlanePointLeftLower(i + 1, j), vtx);
            writeVertex(pPoint, vtx);
        }


        vtx = begin(::stripCount(i, mPointCount));

        for (s32 j = 0; j < mPointCount - i - 1; j++) {
            pPoint = getPlanePointLeftLower(mPointCount - 1 - j, i + 1);
            writeVertex(getPlanePointLeftLower(mPointCount - 1 - j, i), vtx);
            writeVertex(pPoint, vtx);
        }

        writeVertex(getPlanePointLeftLower(i, i), vtx);

        vtx = begin(::stripCount(i, mPointCount));
        writeVertex(getPlanePointRightLower(i, i), vtx);

        for (s32 j = i + 1; j < mPointCount; j++) {
            pPoint = getPlanePointRightLower(i, j);
            writeVertex(getPlanePointRightLower(i + 1, j), vtx);
            writeVertex(pPoint, vtx);
        }


        vtx = begin(::stripCount(i, mPointCount));

        for (s32 j = 0; j < mPointCount - i - 1; j++) {
            pPoint = getPlanePointRightLower(mPointCount - 1 - j, i + 1);
            writeVertex(getPlanePointRightLower(mPointCount - 1 - j, i), vtx);
            writeVertex(pPoint, vtx);
        }

        writeVertex(getPlanePointRightLower(i, i), vtx);
    }
}

void OceanSphere::initMesh() {
    // The face's corners carry the sphere's normals too (loadMaterialFace), the back's only them.
    const u32 corners = 8 * mPointCount * (mPointCount + 1);
    mMeshFace.init(corners);
    writeSphere< VertexPNT2 >([this](u32 count) { return mMeshFace.strip(count); });
    mMeshFace.send();
    mMeshBack.init(corners);
    writeSphere< VertexPN >([this](u32 count) { return mMeshBack.strip(count); });
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

    mWaveTime1 += -cNum_control_f1;
    mWaveTime2 += -cNum_control_f2;

    if (!mAlwaysUseRealDrawing && MR::calcDistanceToPlayer(this) > cNum_control_f3) {
        mUseMesh = true;
    } else {
        mUseMesh = false;
        updatePoints();
    }

    const f32 add = cNum_control_f4;
    const f32 sub = -cNum_control_f5;

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
    SiteMaterial faceMaterial(GameTexture* pTexture, const NebulaColor8& rHighlight, const NebulaColor8& rDeep, const NebulaColor8& rShallow,
                          const NebulaColor8& rTint, u8 cull) {
        SiteMaterial material;
        material.init(NEBULA_SHADER_OCEAN_SPHERE, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, false,
                           cull);
        material.setColor(0, rHighlight.r, rHighlight.g, rHighlight.b, rHighlight.a);
        material.setColor(4, rDeep.r, rDeep.g, rDeep.b, rDeep.a);
        material.setColor(8, rShallow.r, rShallow.g, rShallow.b, rShallow.a);
        material.setColor(12, rTint.r, rTint.g, rTint.b, rTint.a);
        material.setTexture(0, pTexture);
        return material;
    }
}  // namespace

void OceanSphere::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    if (mIsStartPosCamera && !mIsCameraInside) {
        const SiteMaterial back = loadMaterialBack();
        TypedDraw site(back, nullptr, MR::getCameraViewMtx());

        if (mUseMesh) {
            mMeshBack.draw(site);
        } else {
            drawBack(site);
        }
    }

    const SiteMaterial face = loadMaterialFace();
    TypedDraw site(face, nullptr, MR::getCameraViewMtx());

    if (mIsStartPosCamera) {
        if (mIsCameraInside) {
            site.setMaterial(faceMaterial(mWaterTex, sOceanSphereStageColor0Face, mStageColor1Front, sOceanSphereStageColor2Face,
                                          sOceanSphereKColor0Face, NEBULA_CULL_FRONT));
        }

        if (mUseMesh) {
            mMeshFace.draw(site);
        } else {
            drawFace(site);
        }

        return;
    }

    site.setMaterial(faceMaterial(mWaterTex, sOceanSphereStageColor0Face, mStageColor1Front, sOceanSphereStageColor2Face, sOceanSphereKColor0Face,
                                  NEBULA_CULL_FRONT));
    drawFace(site);

    if (mIsCameraInside) {
        return;
    }

    site.setMaterial(faceMaterial(mWaterTex, sOceanSphereStageColor0Back, mStageColor1Back, sOceanSphereStageColor2Back, sOceanSphereKColor0Back,
                                  NEBULA_CULL_BACK));
    drawFace(site);
}

void OceanSphere::drawFace(TypedDraw& rDraw) const {
    writeSphere< VertexPNT2 >([&rDraw](u32 count) { return rDraw.vertices< VertexPNT2 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, count); });
}

void OceanSphere::drawBack(TypedDraw& rDraw) const {
    writeSphere< VertexPN >([&rDraw](u32 count) { return rDraw.vertices< VertexPN >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, count); });
}


SiteMaterial OceanSphere::loadMaterialFace() const {
    return faceMaterial(mWaterTex, sOceanSphereStageColor0Face, mStageColor1Front, sOceanSphereStageColor2Face, sOceanSphereKColor0Face,
                     NEBULA_CULL_BACK);
}

SiteMaterial OceanSphere::loadMaterialBack() const {
    Pos3f pos;
    pos.set(MR::getCameraViewMtx());
    pos.zeroTrans();
    pos.scale(cNum_loadMaterialBack_f1);
    // runtime/src/render/materials/ocean_sphere_env.hlsl: the environment map by the normal from sOceanSphereStageColor0Env
    // toward sOceanSphereStageColor1Env; opaque, depth tested and written, front faces culled, no fog.
    SiteMaterial material;
    material.init(NEBULA_SHADER_OCEAN_SPHERE_ENV, SiteMaterial::BLEND_OPAQUE, NEBULA_COMPARE_LEQUAL, true,
                       NEBULA_CULL_FRONT);
    material.setMatrix(2, 0, pos.toMtxPtr());
    const NebulaColor8& rLow = sOceanSphereStageColor0Env;
    const NebulaColor8& rHigh = sOceanSphereStageColor1Env;
    material.setColor(0, rLow.r, rLow.g, rLow.b, rLow.a);
    material.setColor(4, rHigh.r, rHigh.g, rHigh.b, rHigh.a);
    material.setTexture(0, mWaterEnvTex);
    return material;
}


void OceanSphere::writeVertex(const OceanSpherePoint* pPoint, VertexPN*& rVtx) const {
    *rVtx++ = {pPoint->mPos.x, pPoint->mPos.y, pPoint->mPos.z, pPoint->mNormal.x, pPoint->mNormal.y, pPoint->mNormal.z};
}

void OceanSphere::writeVertex(const OceanSpherePoint* pPoint, VertexPNT2*& rVtx) const {
    *rVtx++ = {pPoint->mPos.x, pPoint->mPos.y, pPoint->mPos.z, pPoint->mNormal.x, pPoint->mNormal.y, pPoint->mNormal.z,
               mTexOffs0X + pPoint->mTexCoord.x, mTexOffs0Y + pPoint->mTexCoord.y, mTexOffs1X + pPoint->mTexCoord.x, mTexOffs1Y + pPoint->mTexCoord.y};
}
