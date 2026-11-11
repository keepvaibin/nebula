#include "Game/Map/WaterRoad.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Render/GameTexture.hpp"
#include <NebulaGpuTypes.h>
#include "Game/System/NebulaInput.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initPoints_f1, "initPoints.f1", 360.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f1, "drawDirect.f1", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f2, "drawDirect.f2", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f3, "drawDirect.f3", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f4, "drawDirect.f4", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f5, "drawDirect.f5", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f6, "drawDirect.f6", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f7, "drawDirect.f7", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f8, "drawDirect.f8", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f9, "drawDirect.f9", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f10, "drawDirect.f10", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f11, "drawDirect.f11", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drawDirect_f12, "drawDirect.f12", 16384.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_roadMaterial_f1, "roadMaterial.f1", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDisappear_f1, "exeDisappear.f1", 255.0f);

namespace {
    NEBULA_TUNABLE(f32, sRiderSpeedMin, 100.0f);
    NEBULA_TUNABLE(f32, sRiderSpeedMax, 150.0f);
    NEBULA_TUNABLE(f32, sRiderSpinAccel, 50.0f);
    NEBULA_TUNABLE(f32, sRiderFrictionRate, 0.99f);
    static const s32 sStepRideSpin = NebulaFrameClock::stepsOfMilliseconds(500);
    static const s32 sDisappearStep = NebulaFrameClock::stepsOfMilliseconds(500);
    NEBULA_TUNABLE(f32, sSensorOffsetY, 300.0f);
    NEBULA_TUNABLE(f32, sPointIntervalLine, 100.0f);
    NEBULA_TUNABLE(f32, sDotPointCreateHigh, 0.998f);
    NEBULA_TUNABLE(f32, sDotPointCreateLow, 0.99f);
    NEBULA_TUNABLE(f32, sFloatToShortShift, 32768.0f);
    NEBULA_TUNABLE(f32, sDefaultRadius, 500.0f);
    NEBULA_TUNABLE(f32, sDotUpdateAxis, 0.99f);
    NEBULA_TUNABLE(f32, sTexRateU0, 0.05f);
    NEBULA_TUNABLE(f32, sTexRateV0, 0.0001f);
    NEBULA_TUNABLE(f32, sTexRateU1, 0.05f);
    NEBULA_TUNABLE(f32, sTexRateV1, 0.0001f);
    NEBULA_TUNABLE(f32, sTexRateU2, 0.1f);
    NEBULA_TUNABLE(f32, sTexRateV2, 0.0001f);
    NEBULA_TUNABLE(f32, sTexSpeedU0, 0.002f);
    NEBULA_TUNABLE(f32, sTexSpeedV0, -0.005f);
    NEBULA_TUNABLE(f32, sTexSpeedU1, -0.0015f);
    NEBULA_TUNABLE(f32, sTexSpeedV1, -0.005f);
    NEBULA_TUNABLE(f32, sTexSpeedV2, -0.005f);
    NEBULA_TUNABLE(f32, sIndirectScale, 0.2f);
    NEBULA_TUNABLE(f32, sClippingRadiusOffset, 100.0f);
    NEBULA_TUNABLE(f32, sDistanceToLow, 10000.0f);
    NEBULA_TUNABLE(f32, sTexSpeedU2, 0.0f);

    static NebulaColor8 sStageColor1 = {0x28, 0x28, 0x28, 0x14};
    static NebulaColor8 sStageColor2 = {0x55, 0x96, 0xBE, 0xFF};

    inline s32 calcNumDrawLinePoints(WaterRoad* pRoad, f32 dotPointCreate) {
        MR::moveCoordToStartPos(pRoad);

        s32 numTestPoints = static_cast< s32 >(MR::getRailTotalLength(pRoad) / ::sPointIntervalLine) + 1;
        f32 testDelta = MR::getRailTotalLength(pRoad) / (numTestPoints - 1);

        Vec3f testRailDir = MR::getRailDirection(pRoad);

        s32 numPoints = 1;
        for (s32 idx = 1; idx < numTestPoints; idx++) {
            MR::moveCoord(pRoad, testDelta);

            if (MR::getRailDirection(pRoad).dot(testRailDir) < dotPointCreate) {
                numPoints++;
                testRailDir.set(MR::getRailDirection(pRoad));
            }
        }

        return numPoints;
    }

};  // namespace

namespace NrvWaterRoad {
    NEW_NERVE(WaterRoadNrvWait, WaterRoad, Wait);
    NEW_NERVE(WaterRoadNrvWaitInvalid, WaterRoad, WaitInvalid);
    NEW_NERVE(WaterRoadNrvRideStart, WaterRoad, RideStart);
    NEW_NERVE(WaterRoadNrvRideWait, WaterRoad, RideWait);
    NEW_NERVE(WaterRoadNrvRideSpin, WaterRoad, RideSpin);
    NEW_NERVE(WaterRoadNrvDemoStart, WaterRoad, DemoStart);
    NEW_NERVE(WaterRoadNrvDemoGrowUp, WaterRoad, DemoGrowUp);
    NEW_NERVE(WaterRoadNrvDemoWaitEnd, WaterRoad, DemoWaitEnd);
    NEW_NERVE(WaterRoadNrvDisappear, WaterRoad, Disappear);
};  // namespace NrvWaterRoad

WaterRoadModelInfo::WaterRoadModelInfo(WaterRoad* pRoad, bool isLow)
    : mIsLow(isLow), mNumPoints(), mNumLinePoints(), mNumLoopPoints(12), mPoints(), mNormals(), mRailCoords(),
      mRoad(pRoad) {
    initPoints(pRoad);
    initMesh();
}

void WaterRoadModelInfo::initPoints(WaterRoad* pRoad) {
    f32 dotPointCreate;
    if (mIsLow) {
        dotPointCreate = ::sDotPointCreateLow;
    } else {
        dotPointCreate = ::sDotPointCreateHigh;
    }

    s32 numPoints = ::calcNumDrawLinePoints(pRoad, dotPointCreate);

    mNumLinePoints = numPoints < 2 ? 2 : numPoints;
    mNumPoints = mNumLinePoints * mNumLoopPoints;

    // A rail under one interval long steps its whole length at once (the count was 0).
    const s32 intervalCount = static_cast< s32 >(MR::getRailTotalLength(pRoad) / ::sPointIntervalLine);
    f32 lineDelta = MR::getRailTotalLength(pRoad) / (intervalCount > 0 ? intervalCount : 1);

    Vec3f up = pRoad->mBaseUp;
    f32 loopInterval = MR::toRadian(cNum_initPoints_f1 / mNumLoopPoints);

    mPoints = new Vec3f[mNumPoints];
    mNormals = new Vec3s[mNumPoints];
    mRailCoords = new f32[mNumPoints];

    MR::moveCoordToStartPos(pRoad);
    Vec3f front = MR::getRailDirection(pRoad);

    s32 pointIdx = 0;
    for (s32 lineIdx = 0; lineIdx < mNumLinePoints; lineIdx++) {
        if (lineIdx == mNumLinePoints - 1) {
            MR::moveCoordToEndPos(pRoad);
        }

        Vec3f side = MR::getRailDirection(pRoad).cross(up);
        MR::normalize(&side);

        Pos3f loopRot;
        loopRot.identity();
        Vec3f rotAxis = MR::getRailDirection(pRoad) * -1.0f;
        loopRot.setRotate(rotAxis, loopInterval);

        up.cross(side, MR::getRailDirection(pRoad));

        mRailCoords[lineIdx] = MR::getRailCoord(pRoad);

        for (s32 loopIdx = 0; loopIdx < mNumLoopPoints; loopIdx++) {
            Vec3f pos = side;
            pos.scale(pRoad->mRadius);
            pos.add(MR::getRailPos(pRoad));
            mPoints[pointIdx].set(pos);
            mNormals[pointIdx].set((s32)(side.x * ::sFloatToShortShift), (s32)(side.y * ::sFloatToShortShift), (s32)(side.z * ::sFloatToShortShift));
            pointIdx++;
            loopRot.mult(side, side);
        }

        while (pointIdx < mNumPoints && !MR::isRailReachedGoal(pRoad) && MR::getRailDirection(pRoad).dot(front) > dotPointCreate) {
            MR::moveCoord(pRoad, lineDelta);
        }

        front.set(MR::getRailDirection(pRoad));
    }

}

namespace {
    void setThirdCoord(VertexPNT3& rVertex, f32 s, f32 t) {
        rVertex.s2 = s;
        rVertex.t2 = t;
    }
    void setThirdCoord(VertexPNT2&, f32, f32) {
    }
}  // namespace

template < class T >
void WaterRoadModelInfo::writeMesh(SiteMesh< T >& rMesh) {
    rMesh.init(2 * mNumLinePoints * mNumLoopPoints);
    const auto vertex = [this](u16 at, f32 s0, f32 t0, f32 s1, f32 t1, f32 s2, f32 t2) {
        const Vec3f& rPlace = mPoints[at];
        const Vec3s& rNormal = mNormals[at];
        T out;
        out.x = rPlace.x;
        out.y = rPlace.y;
        out.z = rPlace.z;
        out.nx = rNormal.x / 16384.0f;
        out.ny = rNormal.y / 16384.0f;
        out.nz = rNormal.z / 16384.0f;
        out.s0 = s0;
        out.t0 = t0;
        out.s1 = s1;
        out.t1 = t1;
        ::setThirdCoord(out, s2, t2);
        return out;
    };

    f32 texU0A = 0.0f;
    f32 texU0B = ::sTexRateU0;
    f32 texU1A = 0.0f;
    f32 texU1B = ::sTexRateU1;
    f32 texU2A = 0.0f;
    f32 texU2B = ::sTexRateU2;

    for (s32 loopIdx = 0; loopIdx < mNumLoopPoints; loopIdx++) {
        s32 nextLoopIdx = loopIdx + 1;
        if (loopIdx == mNumLoopPoints - 1) {
            nextLoopIdx = 0;
        }

        u16 numPoints = mNumLinePoints * 2;
        T* vtx = rMesh.strip(numPoints);

        for (s32 lineIdx = 0; lineIdx < mNumLinePoints; lineIdx++) {
            f32 texV0 = mRailCoords[lineIdx] * ::sTexRateV0;
            f32 texV1 = mRailCoords[lineIdx] * ::sTexRateV1;
            f32 texV2 = mRailCoords[lineIdx] * ::sTexRateV2;

            u16 pointIdx = calcPointIndex(lineIdx, loopIdx);
            u16 nextIdx = calcPointIndex(lineIdx, nextLoopIdx);

            *vtx++ = vertex(pointIdx, texU0A, texV0, texU1A, texV1, texU2A, texV2);
            *vtx++ = vertex(nextIdx, texU0B, texV0, texU1B, texV1, texU2B, texV2);
        }

        texU0A = texU0B;
        texU1A = texU1B;
        texU2A = texU2B;
        texU0B += ::sTexRateU0;
        texU1B += ::sTexRateU1;
        texU2B += ::sTexRateU2;
    }

    rMesh.send();
}

void WaterRoadModelInfo::initMesh() {
    if (mIsLow) {
        writeMesh(mMeshLow);
    } else {
        writeMesh(mMeshHigh);
    }
}


void WaterRoadModelInfo::drawGD() const {
    if (mIsLow) {
        const SiteMaterial material = loadMaterialLow();
        TypedDraw site(material, nullptr, MR::getCameraViewMtx());
        mMeshLow.draw(site);
    } else {
        const SiteMaterial material = loadMaterialHigh(nullptr);
        TypedDraw site(material, nullptr, MR::getCameraViewMtx());
        mMeshHigh.draw(site);
    }
}

void WaterRoadModelInfo::drawDirect(const WaterRoad* pRoad) const {
    const SiteMaterial material = loadMaterialHigh(pRoad);
    TypedDraw site(material, nullptr, MR::getCameraViewMtx());

    s32 pointNum = calcDemoDrawPointNum(pRoad);
    u16 numPoints = pointNum * 2;
    if (pointNum < mNumLinePoints) {
        numPoints += 2;
    }

    f32 texU0A = 0.0f;
    f32 texU0B = ::sTexRateU0;
    f32 texU1A = 0.0f;
    f32 texU1B = ::sTexRateU1;
    f32 texU2A = 0.0f;
    f32 texU2B = ::sTexRateU2;

    for (s32 loopIdx = 0; loopIdx < mNumLoopPoints; loopIdx++) {
        s32 nextLoopIdx = loopIdx + 1;
        if (loopIdx == mNumLoopPoints - 1) {
            nextLoopIdx = 0;
        }

        VertexPNT3* vtx = site.vertices< VertexPNT3 >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, numPoints);

        for (s32 lineIdx = 0; lineIdx < pointNum; lineIdx++) {
            f32 texV0 = mRailCoords[lineIdx] * ::sTexRateV0;
            f32 texV1 = mRailCoords[lineIdx] * ::sTexRateV1;
            f32 texV2 = mRailCoords[lineIdx] * ::sTexRateV2;

            const Vec3f& pointA = mPoints[calcPointIndex(lineIdx, loopIdx)];
            const Vec3f& pointB = mPoints[calcPointIndex(lineIdx, nextLoopIdx)];

            const Vec3s& normA = mNormals[calcPointIndex(lineIdx, loopIdx)];
            const Vec3s& normB = mNormals[calcPointIndex(lineIdx, nextLoopIdx)];

            *vtx++ = {pointA.x, pointA.y, pointA.z, normA.x / cNum_drawDirect_f1, normA.y / cNum_drawDirect_f2, normA.z / cNum_drawDirect_f3, texU0A, texV0, texU1A, texV1, texU2A, texV2};

            *vtx++ = {pointB.x, pointB.y, pointB.z, normB.x / cNum_drawDirect_f4, normB.y / cNum_drawDirect_f5, normB.z / cNum_drawDirect_f6, texU0B, texV0, texU1B, texV1, texU2B, texV2};
        }

        if (pointNum < mNumLinePoints) {
            // Two line points on one coordinate (a road with no length) draw the earlier one.
            const f32 coordSpan = mRailCoords[pointNum] - mRailCoords[pointNum - 1];
            f32 t = coordSpan != 0.0f ? (MR::getRailCoord(pRoad) - mRailCoords[pointNum - 1]) / coordSpan : 0.0f;

            // The road's end: points and normals between the last line passed and the next, on both loop sides.
            const Vec3f& prevA = mPoints[calcPointIndex(pointNum - 1, loopIdx)];
            s32 currAIdx = calcPointIndex(pointNum, loopIdx);
            const Vec3f& currA = mPoints[currAIdx];
            const Vec3f& prevB = mPoints[calcPointIndex(pointNum - 1, nextLoopIdx)];
            const Vec3f& currB = mPoints[calcPointIndex(pointNum, nextLoopIdx)];

            const Vec3s& prevNormA = mNormals[calcPointIndex(pointNum - 1, loopIdx)];
            const Vec3s& currNormA = mNormals[currAIdx];
            const Vec3s& prevNormB = mNormals[calcPointIndex(pointNum - 1, nextLoopIdx)];
            const Vec3s& currNormB = mNormals[calcPointIndex(pointNum, nextLoopIdx)];

            Vec3f vA = prevA * (1.0f - t) + currA * t;
            Vec3f vB = prevB * (1.0f - t) + currB * t;

            Vec3s nA;
            nA.x = (s32)(prevNormA.x * (1.0f - t) + currNormA.x * t);
            nA.y = (s32)(prevNormA.y * (1.0f - t) + currNormA.y * t);
            nA.z = (s32)(prevNormA.z * (1.0f - t) + currNormA.z * t);

            Vec3s nB;
            nB.x = (s32)(prevNormB.x * (1.0f - t) + currNormB.x * t);
            nB.y = (s32)(prevNormB.y * (1.0f - t) + currNormB.y * t);
            nB.z = (s32)(prevNormB.z * (1.0f - t) + currNormB.z * t);

            f32 texV0 = MR::getRailCoord(pRoad) * ::sTexRateV0;
            f32 texV1 = MR::getRailCoord(pRoad) * ::sTexRateV1;
            f32 texV2 = MR::getRailCoord(pRoad) * ::sTexRateV2;

            *vtx++ = {vA.x, vA.y, vA.z, nA.x / cNum_drawDirect_f7, nA.y / cNum_drawDirect_f8, nA.z / cNum_drawDirect_f9, texU0A, texV0, texU1A, texV1, texU2A, texV2};

            *vtx++ = {vB.x, vB.y, vB.z, nB.x / cNum_drawDirect_f10, nB.y / cNum_drawDirect_f11, nB.z / cNum_drawDirect_f12, texU0B, texV0, texU1B, texV1, texU2B, texV2};
        }

        texU0A = texU0B;
        texU1A = texU1B;
        texU2A = texU2B;
        texU0B += ::sTexRateU0;
        texU1B += ::sTexRateU1;
        texU2B += ::sTexRateU2;
    }
}

s32 WaterRoadModelInfo::calcDemoDrawPointNum(const WaterRoad* pRoad) const {
    for (s32 pointNum = 1; pointNum < mNumLinePoints; pointNum++) {
        if (MR::getRailCoord(pRoad) < mRailCoords[pointNum]) {
            return pointNum;
        }
    }

    return mNumLinePoints;
}

namespace {
    // Its texture scrolls (WaterRoad::draw), for the material that follows.
    Mtx sRoadScroll[3];

    // runtime/src/render/materials/water_road.hlsl at high or low detail: blended by alpha where above zero, depth
    // tested and written, no culling.
    SiteMaterial roadMaterial(const WaterRoad* pRoad, bool high, u8 alpha) {
        GameTexture screen(MR::getScreenResTIMG());
        SiteMaterial material;
        material.init(NEBULA_SHADER_WATER_ROAD, SiteMaterial::BLEND_ALPHA, NEBULA_COMPARE_LEQUAL, true,
                           NEBULA_CULL_NONE);
        // The scrolls, the screen's projection, then the environment map's: the camera's rotation, the map's half
        // scale and centre.
        const u32 scrolls = high ? 3 : 2;
        for (u32 i = 0; i < scrolls; i++) {
            material.setMatrix(4, i, sRoadScroll[i]);
        }
        Pos3f projection;
        MR::calcTexProjectionMtx(&projection);
        material.setMatrix(4, scrolls, projection);
        Pos3f pos;
        pos.identity();
        pos.set(MR::getCameraViewMtx());
        pos.zeroTrans();
        static Mtx qMtx2 = {{0.5f, 0.0f, 0.0f, 0.5f}, {0.0f, -0.5f, 0.0f, 0.5f}, {0.0f, 0.0f, 1.0f, 0.0f}};
        MR::multMtx(pos.mMtx, qMtx2, pos.mMtx);
        material.setMatrix(4, scrolls + 1, pos.mMtx);
        material.setColor(0, ::sStageColor1.r, ::sStageColor1.g, ::sStageColor1.b, ::sStageColor1.a);
        material.setColor(4, ::sStageColor2.r, ::sStageColor2.g, ::sStageColor2.b, ::sStageColor2.a);
        material.constants[8] = high ? 1.0f : 0.0f;
        material.constants[9] = ::sIndirectScale;
        material.constants[10] = alpha / cNum_roadMaterial_f1;
        material.constants[12] = static_cast< f32 >(screen.getWidth());
        material.constants[13] = static_cast< f32 >(screen.getHeight());
        material.setTexture(0, pRoad->mTexture);
        if (high) {
            material.setTexture(1, pRoad->mIndirect);
        }
        material.setTexture(2, &screen);
        material.setTexture(3, pRoad->mEnvMap);
        return material;
    }
}  // namespace

SiteMaterial WaterRoadModelInfo::loadMaterialHigh(const WaterRoad* pRoad) const {
    return roadMaterial(mRoad, true, pRoad != nullptr ? pRoad->mAlpha : 0xFF);
}

SiteMaterial WaterRoadModelInfo::loadMaterialLow() const {
    return roadMaterial(mRoad, false, 0xFF);
}

WaterRoad::WaterRoad(const char* pName)
    : LiveActor(pName), mModelInfoHigh(), mModelInfoLow(), mRadius(::sDefaultRadius), mAlpha(0xFF), mBaseUp(0.0f, 1.0f, 0.0f), mRider(),
      mTexUV0(0.0f, 0.0f), mTexUV1(0.0f, 0.0f), mTexUV2(0.0f, 0.0f), mTexture(), mIndirect(), mEnvMap(), mClippingCenter(0.0f, 0.0f, 0.0f),
      mCamRailNearestPos(0.0f, 0.0f, 0.0f), mSoundObj(), mCameraInfo() {
    lumacore::setClass(this, lumacore::kClassId< WaterRoad >);
    mBaseMtx.identity();
    mRiderMtx.identity();
    mBoundingBox.zero();
}

void WaterRoad::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_WaterRoad);
    MR::initDefaultPos(this, rIter);
    MR::makeMtxTR(mBaseMtx, this);
    mBaseMtx.getYDir(mBaseUp);
    MR::getPlacementArg1NoInit(rIter, &mRadius);
    initRailRider(rIter);

    if (MR::getRailDirection(this).dot(mBaseUp) >= ::sDotUpdateAxis) {
        MR::calcActorAxisX(&mBaseUp, this);
    }

    updateDemo(0.0f);

    initNerve(GET_NERVE(WaterRoad, WaterRoadNrvWait));

    initHitSensor(1);
    MR::addHitSensorCallbackBinder(this, "Binder", 8, mRadius);

    mModelInfoHigh = new WaterRoadModelInfo(this, false);
    mModelInfoLow = new WaterRoadModelInfo(this, true);

    MR::moveCoordToStartPos(this);

    initEffectKeeper(0, "WaterRoad");
    MR::setEffectHostMtx(this, "Top", mBaseMtx);
    MR::setEffectHostMtx(this, "End", mBaseMtx);

    mTexture = new GameTexture(MR::loadTexFromArc("WaterWave", "Water.bti"));
    mIndirect = new GameTexture(MR::loadTexFromArc("WaterWave", "WaterIndirect.bti"));
    mEnvMap = new GameTexture(MR::loadTexFromArc("WaterWave", "EnvMap.bti"));

    initSound(3, false);
    mSoundObj = new AudSoundObject(&mCamRailNearestPos, 4, NebulaArena::current());

    MR::calcBoundingBox(this, &mBoundingBox, ::sClippingRadiusOffset);
    mBoundingBox.pad(mRadius);
    mBoundingBox.getCenter(&mClippingCenter);
    MR::setClippingTypeSphere(this, mClippingCenter.distance(mBoundingBox.i) + ::sClippingRadiusOffset, &mClippingCenter);
    MR::setClippingFarMax(this);

    bool hasDemo = MR::tryRegisterDemoCast(this, rIter);
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "バインド中");
    MR::useStageSwitchWriteA(this, rIter);
    if (hasDemo) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void WaterRoad::appear() {
    LiveActor::appear();
    MR::invalidateClipping(this);
    mPosition.set(MR::getRailPointPosStart(this));

    if (MR::isValidSwitchA(this)) {
        MR::onSwitchA(this);
    }

    if (MR::isDemoPartActive("ウォーターロード成長")) {
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoGrowUp));
    } else {
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoStart));
    }
}

void WaterRoad::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
    }
}

void WaterRoad::exeWaitInvalid() {
    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvWait));
    }
}

void WaterRoad::exeRideStart() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("WaterRoadIn");
        MR::startSound(mRider, "SE_OJ_WATER_ROAD_BIND_IN");
    }

    if (updateRide()) {
        return;
    }

    if (MR::isMainSpin(PLAYER_1)) {
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvRideSpin));
        return;
    }

    if (MR::isBckStopped(mRider)) {
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvRideWait));
    }
}

void WaterRoad::exeRideWait() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SwimFlutter");
    }

    if (updateRide()) {
        return;
    }

    if (MR::isMainSpin(PLAYER_1)) {
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvRideSpin));
    }
}

void WaterRoad::exeRideSpin() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("SwimSpin");
        MR::startSound(mRider, "SE_PM_TORNADE_IN_WATER_ST");
        f32 speed = MR::getRailCoordSpeed(this);
        speed += ::sRiderSpinAccel;
        MR::setRailCoordSpeed(this, speed);
    }

    MR::startLevelSound(mRider, "SE_PM_LV_TORNADE_IN_WATER");

    if (updateRide()) {
        return;
    }

    if (MR::isStep(this, ::sStepRideSpin)) {
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvRideWait));
    }
}

void WaterRoad::exeDemoStart() {
    MR::startLevelSound(this, "SE_OJ_LV_WATER_ROAD_APPEAR");
    MR::startAtmosphereLevelSE("SE_AT_LV_EARTHQUAKE");

    if (MR::isDemoPartLastStep("ウォーターロード出現")) {
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoGrowUp));
    }
}

void WaterRoad::exeDemoGrowUp() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Top");
        MR::startSound(this, "SE_OJ_WATER_ROAD_GROW_START");
        MR::shakeCameraStrong();
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
    }

    updateDemo(MR::calcDemoPartStepRate("ウォーターロード成長"));

    MR::startLevelSound(this, "SE_OJ_LV_WATER_ROAD_GROW");
    MR::startAtmosphereLevelSE("SE_AT_LV_EARTHQUAKE");

    if (MR::isDemoPartLastStep("ウォーターロード成長")) {
        MR::deleteEffect(this, "Top");
        MR::emitEffect(this, "End");
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoWaitEnd));
    }
}

void WaterRoad::exeDemoWaitEnd() {
    if (!MR::isDemoActive()) {
        MR::showPlayer();
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvWait));
    }
}

void WaterRoad::exeDisappear() {
    mAlpha = (s32)(MR::calcNerveValue(this, ::sDisappearStep, cNum_exeDisappear_f1, 0.0f));

    if (MR::isStep(this, ::sDisappearStep)) {
        kill();
    }
}

void WaterRoad::movement() {

    if (!MR::isValidMovement(this)) {
        return;
    }

    LiveActor::movement();

    mTexUV0.x = MR::repeat(mTexUV0.x + ::sTexSpeedU0, 0.0f, 1.0f);
    mTexUV0.y = MR::repeat(mTexUV0.y + ::sTexSpeedV0, 0.0f, 1.0f);
    mTexUV1.x = MR::repeat(mTexUV1.x + ::sTexSpeedU1, 0.0f, 1.0f);
    mTexUV1.y = MR::repeat(mTexUV1.y + ::sTexSpeedV1, 0.0f, 1.0f);
    mTexUV2.x = MR::repeat(mTexUV2.x + ::sTexSpeedU2, 0.0f, 1.0f);
    mTexUV2.y = MR::repeat(mTexUV2.y + ::sTexSpeedV2, 0.0f, 1.0f);

    MR::calcNearestRailPos(&mCamRailNearestPos, this, MR::getCamPos());
    MR::startSoundObjectLevel(mSoundObj, "SE_AT_LV_WATER_ROAD");
    mSoundObj->process();
}

void WaterRoad::updateHitSensor(HitSensor* pSensor) {
    if (!isNerve(GET_NERVE(WaterRoad, WaterRoadNrvWait))) {
        return;
    }

    MR::calcRailPosNearestPlayer(&pSensor->mPosition, this);
    if (pSensor->mPosition.y < MR::getRailPointPosStart(this).y + ::sSensorOffsetY) {
        pSensor->mPosition.y = MR::getRailPointPosStart(this).y + ::sSensorOffsetY;
    }
}

bool WaterRoad::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgAutoRushBegin(msg)) {
        if (!isNerve(GET_NERVE(WaterRoad, WaterRoadNrvWait))) {
            return false;
        }

        mRider = MR::getSensorHost(pSender);
        MR::moveCoordToNearestPos(this, mRider->mPosition);
        MR::startMultiActorCameraTargetPlayer(this, mCameraInfo, "バインド中");
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvRideStart));
        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        Vec3f front;
        mRiderMtx.getZDir(front);
        MR::makeMtxUpFrontPos(&mRiderMtx, MR::getRailDirection(this), front, MR::getRailPos(this));
        MR::setBaseTRMtx(mRider, mRiderMtx);
        return true;
    }

    return false;
}

bool WaterRoad::updateRide() {
    f32 speed = MR::getRailCoordSpeed(this);
    speed *= ::sRiderFrictionRate;
    MR::moveCoordAndFollowTrans(this, MR::clamp(speed, ::sRiderSpeedMin, ::sRiderSpeedMax));

    if (mRider != nullptr) {
        MR::startLevelSound(mRider, "SE_OJ_LV_WATER_ROAD_BIND");
    }

    if (MR::isRailReachedGoal(this)) {
        MR::startSound(mRider, "SE_OJ_WATER_ROAD_BIND_END");
        MR::startSound(mRider, "SE_PV_JUMP_M");
        MR::endBindAndPlayerJump(this, Vec3f(0.0f, 0.0f, 0.0f), 0);
        MR::endMultiActorCamera(this, mCameraInfo, "バインド中", true);
        mRider = nullptr;
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvDisappear));
        return true;
    }

    return false;
}

void WaterRoad::updateDemo(f32 demoRate) {
    MR::setRailCoord(this, MR::getEaseOutValue(demoRate, 0.0f, 1.0f, 1.0f) * MR::getRailTotalLength(this));

    mPosition.set(MR::getRailPos(this));
    Vec3f front;
    mBaseMtx.getZDir(front);
    MR::makeMtxUpFront(&mBaseMtx, MR::getRailDirection(this), front);
    mBaseMtx.setTrans(mPosition);
}

void WaterRoad::draw() const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    if (isNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoStart))) {
        return;
    }

    Pos3f mtx;
    mtx.identity();
    mtx[0][2] = mTexUV0.x;
    mtx[1][2] = mTexUV0.y;
    NebulaMtxCopy(mtx, sRoadScroll[0]);
    mtx[0][2] = mTexUV1.x;
    mtx[1][2] = mTexUV1.y;
    NebulaMtxCopy(mtx, sRoadScroll[1]);


    if (isNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoGrowUp)) || isNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoWaitEnd))) {
        mtx[0][2] = mTexUV2.x;
        mtx[1][2] = mTexUV2.y;
        NebulaMtxCopy(mtx, sRoadScroll[2]);
        mModelInfoHigh->drawDirect(this);
        return;
    }

    Vec3f boundedPos;
    boundedPos.x = MR::clamp(MR::getPlayerPos()->x, mBoundingBox.i.x, mBoundingBox.f.x);
    boundedPos.y = MR::clamp(MR::getPlayerPos()->y, mBoundingBox.i.y, mBoundingBox.f.y);
    boundedPos.z = MR::clamp(MR::getPlayerPos()->z, mBoundingBox.i.z, mBoundingBox.f.z);

    if (MR::getPlayerPos()->distance(boundedPos) > ::sDistanceToLow) {
        mModelInfoLow->drawGD();
    } else {
        mtx[0][2] = mTexUV2.x;
        mtx[1][2] = mTexUV2.y;
        NebulaMtxCopy(mtx, sRoadScroll[2]);
        mModelInfoHigh->drawGD();
    }
}
