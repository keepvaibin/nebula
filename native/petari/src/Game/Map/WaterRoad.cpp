#include "Game/Map/WaterRoad.hpp"
#include <JSystem/J3DGraphBase/J3DNativeState.hpp>
#include <JSystem/J3DGraphBase/J3DNativeShape.hpp>
#include <JSystem/J3DGraphBase/J3DNativeDraw.hpp>
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
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <JSystem/JUtility/JUTTexture.hpp>
#include <revolution/gx/GXEnum.h>
#include <revolution/gx/GXGeometry.h>
#include <revolution/gx/GXLighting.h>
#include <revolution/gx/GXStruct.h>
#include <revolution/gx/GXTransform.h>
#include <revolution/gx/GXVert.h>
#include <revolution/wpad.h>

void WaterRoad_DUMMY() {
    TVec3f a, b;
    a.sub(b);
    TVec3f c(1.0f);
}

namespace {
    static const f32 sRiderSpeedMin = 100.0f;
    static const f32 sRiderSpeedMax = 150.0f;
    static const f32 sRiderSpinAccel = 50.0f;
    static const f32 sRiderFrictionRate = 0.99f;
    static const s32 sStepRideSpin = 30;
    static const s32 sDisappearStep = 30;
    static const f32 sSensorOffsetY = 300.0f;
    static const f32 sPointIntervalLine = 100.0f;
    static const f32 sDotPointCreateHigh = 0.998f;
    static const f32 sDotPointCreateLow = 0.99f;
    static const f32 sFloatToShortShift = 32768.0f;
    static const f32 sDefaultRadius = 500.0f;
    static const f32 sDotUpdateAxis = 0.99f;
    // static const f32 sPlayerFlowSpeed =
    static const f32 sTexRateU0 = 0.05f;
    static const f32 sTexRateV0 = 0.0001f;
    static const f32 sTexRateU1 = 0.05f;
    static const f32 sTexRateV1 = 0.0001f;
    static const f32 sTexRateU2 = 0.1f;
    static const f32 sTexRateV2 = 0.0001f;
    static const f32 sTexSpeedU0 = 0.002f;
    static const f32 sTexSpeedV0 = -0.005f;
    static const f32 sTexSpeedU1 = -0.0015f;
    static const f32 sTexSpeedV1 = -0.005f;
    static const f32 sTexSpeedV2 = -0.005f;
    static const f32 sIndirectScale = 0.2f;
    // static const f32 sEnvMapScale =
    static const f32 sClippingRadiusOffset = 100.0f;
    static const f32 sDistanceToLow = 10000.0f;
    static const f32 sTexSpeedU2 = 0.0f;

    static GXColor sTevColor1 = {0x28, 0x28, 0x28, 0x14};
    static GXColor sTevColor2 = {0x55, 0x96, 0xBE, 0xFF};

    inline s32 calcNumDrawLinePoints(WaterRoad* pRoad, f32 dotPointCreate) {
        MR::moveCoordToStartPos(pRoad);

        s32 numTestPoints = static_cast< s32 >(MR::getRailTotalLength(pRoad) / ::sPointIntervalLine) + 1;
        f32 testDelta = MR::getRailTotalLength(pRoad) / (numTestPoints - 1);

        TVec3f testRailDir = MR::getRailDirection(pRoad);

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
    : mIsLow(isLow), mNumPoints(), mNumLinePoints(), mNumLoopPoints(12), mPoints(), mNormals(), mRailCoords() {
    initPoints(pRoad);
    initDisplayList();
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

    f32 lineDelta = MR::getRailTotalLength(pRoad) / static_cast< s32 >(MR::getRailTotalLength(pRoad) / ::sPointIntervalLine);

    TVec3f up = pRoad->mBaseUp;
    f32 loopInterval = MR::toRadian(360.0f / mNumLoopPoints);

    mPoints = new (0x20) TVec3f[mNumPoints];
    mNormals = new (0x20) TVec3s[mNumPoints];
    mRailCoords = new f32[mNumPoints];

    MR::moveCoordToStartPos(pRoad);
    TVec3f front = MR::getRailDirection(pRoad);

    s32 pointIdx = 0;
    for (s32 lineIdx = 0; lineIdx < mNumLinePoints; lineIdx++) {
        if (lineIdx == mNumLinePoints - 1) {
            MR::moveCoordToEndPos(pRoad);
        }

        TVec3f side = MR::getRailDirection(pRoad).cross(up);
        MR::normalize(&side);

        TPos3f loopRot;
        loopRot.identity();
        TVec3f rotAxis = MR::getRailDirection(pRoad) * -1.0f;
        loopRot.setRotate(rotAxis, loopInterval);

        up.cross(side, MR::getRailDirection(pRoad));

        mRailCoords[lineIdx] = MR::getRailCoord(pRoad);

        for (s32 loopIdx = 0; loopIdx < mNumLoopPoints; loopIdx++) {
            TVec3f pos = side;
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

void WaterRoadModelInfo::initDisplayList() {
    u32 attributes = NEBULA_SHAPE_NORMAL | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1;
    if (!mIsLow) {
        attributes |= NEBULA_SHAPE_TEXCOORD0 << 2;
    }
    mMesh.init(attributes, 2 * mNumLinePoints * mNumLoopPoints);
    mMesh.useArray(NativeMesh::SLOT_POSITION, mPoints);
    mMesh.useArray(NativeMesh::SLOT_NORMAL, mNormals);
    NativeMesh::sendArray(mPoints, NEBULA_SHAPE_ARRAY_POSITION, mNumPoints, mPoints);
    // The normals as GX reads S16 normals (1.14 fixed point).
    f32* pNormals = new f32[3 * mNumPoints];
    for (s32 i = 0; i < mNumPoints; i++) {
        pNormals[3 * i] = mNormals[i].x / 16384.0f;
        pNormals[3 * i + 1] = mNormals[i].y / 16384.0f;
        pNormals[3 * i + 2] = mNormals[i].z / 16384.0f;
    }
    NativeMesh::sendArray(mNormals, NEBULA_SHAPE_ARRAY_NORMAL, mNumPoints, pNormals);
    delete[] pNormals;
    sendGD();
    mMesh.send();
}

void WaterRoadModelInfo::sendGD() {
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
        mMesh.begin(GX_TRIANGLESTRIP, numPoints);

        for (s32 lineIdx = 0; lineIdx < mNumLinePoints; lineIdx++) {
            f32 texV0 = mRailCoords[lineIdx] * ::sTexRateV0;
            f32 texV1 = mRailCoords[lineIdx] * ::sTexRateV1;
            f32 texV2 = mRailCoords[lineIdx] * ::sTexRateV2;

            u16 pointIdx = calcPointIndex(lineIdx, loopIdx);
            u16 nextIdx = calcPointIndex(lineIdx, nextLoopIdx);

            mMesh.index(NativeMesh::SLOT_POSITION, pointIdx);
            mMesh.index(NativeMesh::SLOT_NORMAL, pointIdx);
            mMesh.texCoord(0, texU0A, texV0);
            mMesh.texCoord(1, texU1A, texV1);
            if (!mIsLow) {
                mMesh.texCoord(2, texU2A, texV2);
            }
            mMesh.corner();

            mMesh.index(NativeMesh::SLOT_POSITION, nextIdx);
            mMesh.index(NativeMesh::SLOT_NORMAL, nextIdx);
            mMesh.texCoord(0, texU0B, texV0);
            mMesh.texCoord(1, texU1B, texV1);
            if (!mIsLow) {
                mMesh.texCoord(2, texU2B, texV2);
            }
            mMesh.corner();
        }

        texU0A = texU0B;
        texU1A = texU1B;
        texU2A = texU2B;
        texU0B += ::sTexRateU0;
        texU1B += ::sTexRateU1;
        texU2B += ::sTexRateU2;
    }
}

void WaterRoadModelInfo::drawGD() const {
    if (mIsLow) {
        loadMaterialLow();
    } else {
        loadMaterialHigh(nullptr);
    }

    mMesh.draw();
}

void WaterRoadModelInfo::drawDirect(const WaterRoad* pRoad) const {
    loadMaterialHigh(pRoad);

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

        J3DNativeDraw::begin(GX_TRIANGLESTRIP, numPoints, NEBULA_SHAPE_NORMAL | NEBULA_SHAPE_TEXCOORD0 | NEBULA_SHAPE_TEXCOORD0 << 1 | NEBULA_SHAPE_TEXCOORD0 << 2);

        for (s32 lineIdx = 0; lineIdx < pointNum; lineIdx++) {
            f32 texV0 = mRailCoords[lineIdx] * ::sTexRateV0;
            f32 texV1 = mRailCoords[lineIdx] * ::sTexRateV1;
            f32 texV2 = mRailCoords[lineIdx] * ::sTexRateV2;

            const TVec3f& pointA = mPoints[calcPointIndex(lineIdx, loopIdx)];
            const TVec3f& pointB = mPoints[calcPointIndex(lineIdx, nextLoopIdx)];

            const TVec3s& normA = mNormals[calcPointIndex(lineIdx, loopIdx)];
            const TVec3s& normB = mNormals[calcPointIndex(lineIdx, nextLoopIdx)];

            J3DNativeDraw::position(pointA.x, pointA.y, pointA.z);
            J3DNativeDraw::normal(normA.x / 16384.0f, normA.y / 16384.0f, normA.z / 16384.0f);
            J3DNativeDraw::texCoord(texU0A, texV0);
            J3DNativeDraw::texCoord(texU1A, texV1);
            J3DNativeDraw::texCoord(texU2A, texV2);

            J3DNativeDraw::position(pointB.x, pointB.y, pointB.z);
            J3DNativeDraw::normal(normB.x / 16384.0f, normB.y / 16384.0f, normB.z / 16384.0f);
            J3DNativeDraw::texCoord(texU0B, texV0);
            J3DNativeDraw::texCoord(texU1B, texV1);
            J3DNativeDraw::texCoord(texU2B, texV2);
        }

        if (pointNum < mNumLinePoints) {
            f32 t = (MR::getRailCoord(pRoad) - mRailCoords[pointNum - 1]) / (mRailCoords[pointNum] - mRailCoords[pointNum - 1]);

            const TVec3f& v1 = mPoints[calcPointIndex(pointNum - 1, loopIdx)];
            s32 idx2 = calcPointIndex(pointNum, loopIdx);
            const TVec3f& v2 = mPoints[idx2];
            const TVec3f& v3 = mPoints[calcPointIndex(pointNum - 1, nextLoopIdx)];
            const TVec3f& v4 = mPoints[calcPointIndex(pointNum, nextLoopIdx)];

            const TVec3s& n1 = mNormals[calcPointIndex(pointNum - 1, loopIdx)];
            const TVec3s& n2 = mNormals[idx2];
            const TVec3s& n3 = mNormals[calcPointIndex(pointNum - 1, nextLoopIdx)];
            const TVec3s& n4 = mNormals[calcPointIndex(pointNum, nextLoopIdx)];

            TVec3f vA = v1 * (1.0f - t) + v2 * t;
            TVec3f vB = v3 * (1.0f - t) + v4 * t;

            TVec3s nA;
            nA.x = (s32)(n1.x * (1.0f - t) + n2.x * t);
            nA.y = (s32)(n1.y * (1.0f - t) + n2.y * t);
            nA.z = (s32)(n1.z * (1.0f - t) + n2.z * t);

            TVec3s nB;
            nB.x = (s32)(n3.x * (1.0f - t) + n4.x * t);
            nB.y = (s32)(n3.y * (1.0f - t) + n4.y * t);
            nB.z = (s32)(n3.z * (1.0f - t) + n4.z * t);

            f32 texV0 = MR::getRailCoord(pRoad) * ::sTexRateV0;
            f32 texV1 = MR::getRailCoord(pRoad) * ::sTexRateV1;
            f32 texV2 = MR::getRailCoord(pRoad) * ::sTexRateV2;

            J3DNativeDraw::position(vA.x, vA.y, vA.z);
            J3DNativeDraw::normal(nA.x / 16384.0f, nA.y / 16384.0f, nA.z / 16384.0f);
            J3DNativeDraw::texCoord(texU0A, texV0);
            J3DNativeDraw::texCoord(texU1A, texV1);
            J3DNativeDraw::texCoord(texU2A, texV2);

            J3DNativeDraw::position(vB.x, vB.y, vB.z);
            J3DNativeDraw::normal(nB.x / 16384.0f, nB.y / 16384.0f, nB.z / 16384.0f);
            J3DNativeDraw::texCoord(texU0B, texV0);
            J3DNativeDraw::texCoord(texU1B, texV1);
            J3DNativeDraw::texCoord(texU2B, texV2);
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

void WaterRoadModelInfo::loadMaterialHigh(const WaterRoad* pRoad) const {
    J3DNativeShape::sendPosition(MR::getCameraViewMtx(), 0);
    J3DNativeShape::sendNormal(MR::getCameraViewMtx(), 0);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setNumChans(0);
    J3DNativeState::setNumTexGens(5);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x1E, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x21, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX2x4, GX_TG_TEX2, 0x24, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX3x4, GX_TG_POS, 0x27, 0, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD4, GX_TG_MTX2x4, GX_TG_NRM, 0x2A, 0, 0x7D);
    MR::loadTexProjectionMtx(0x27);

    TPos3f pos;
    pos.identity();
    pos.set(MR::getCameraViewMtx());
    pos.zeroTrans();

    TPos3f mtx;
    mtx.identity();
    mtx.scale(1.0f);

    static Mtx qMtx2 = {0.5f, 0.0f, 0.0f, 0.5f, 0.0f, -0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f};

    MR::multMtx(pos.mMtx, mtx, pos.mMtx);
    MR::multMtx(pos.mMtx, qMtx2, pos.mMtx);
    J3DNativeShape::sendTexture(pos.mMtx, 0x2A, GX_MTX2x4);
    J3DNativeState::setNumIndStages(1);
    J3DNativeState::setIndTexOrder(GX_INDTEXSTAGE0, GX_TEXCOORD2, GX_TEXMAP1);
    J3DNativeState::setTevIndWarp(GX_TEVSTAGE3, GX_INDTEXSTAGE0, GX_TRUE, GX_FALSE, GX_ITM_0);

    f32 indirectScale = ::sIndirectScale;

    Mtx23 indMtx;
    indMtx[0][1] = 0.0f;
    indMtx[0][0] = indirectScale;
    indMtx[0][2] = 0.0f;
    indMtx[1][0] = 0.0f;
    indMtx[1][1] = indirectScale;
    indMtx[1][2] = 0.0f;

    J3DNativeState::setIndTexMtx(GX_ITM_0, indMtx, 0);
    J3DNativeState::setNumTevStages(5);

    J3DNativeState::setTevColor(GX_TEVREG0, ::sTevColor1);
    J3DNativeState::setTevColor(GX_TEVREG1, ::sTevColor2);

    if (pRoad != nullptr) {
        J3DNativeState::setTevColor(GX_TEVREG2, Color8(0xFF, 0xFF, 0xFF, pRoad->mAlpha));
    }

    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE2, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE2, GX_CC_CPREV, GX_CC_A0, GX_CC_C0, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE2, GX_TEV_COMP_R8_EQ, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE2, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE3, GX_TEXCOORD3, GX_TEXMAP2, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE3, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C1, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE3, GX_CA_APREV, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE4, GX_TEXCOORD4, GX_TEXMAP3, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE4, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

    if (pRoad == nullptr || pRoad->mAlpha == 0xFF) {
        J3DNativeState::setTevAlphaIn(GX_TEVSTAGE4, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    } else {
        J3DNativeState::setTevAlphaIn(GX_TEVSTAGE4, GX_CA_A2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    }

    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE4, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    J3DNativeState::setAlphaCompare(GX_GREATER, 0, GX_AOP_OR, GX_GREATER, 0);
    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}

void WaterRoadModelInfo::loadMaterialLow() const {
    J3DNativeShape::sendPosition(MR::getCameraViewMtx(), 0);
    J3DNativeShape::sendNormal(MR::getCameraViewMtx(), 0);
    J3DNativeState::setCurrentMtx(0);
    J3DNativeState::setNumChans(0);
    J3DNativeState::setNumTexGens(4);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, 0x1E, GX_FALSE, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD1, GX_TG_MTX2x4, GX_TG_TEX1, 0x21, GX_FALSE, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD2, GX_TG_MTX3x4, GX_TG_POS, 0x24, GX_FALSE, 0x7D);
    J3DNativeState::setTexCoordGen2(GX_TEXCOORD3, GX_TG_MTX2x4, GX_TG_NRM, 0x27, GX_FALSE, 0x7D);

    MR::loadTexProjectionMtx(0x24);

    TPos3f pos;
    pos.identity();
    pos.set(MR::getCameraViewMtx());
    pos.zeroTrans();

    TPos3f mtx;
    mtx.identity();
    mtx.scale(1.0f);

    static Mtx qMtx2 = {0.5f, 0.0f, 0.0f, 0.5f, 0.0f, -0.5f, 0.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f};

    MR::multMtx(pos.mMtx, mtx, pos.mMtx);
    MR::multMtx(pos.mMtx, qMtx2, pos.mMtx);
    J3DNativeShape::sendTexture(pos.mMtx, 0x27, GX_MTX2x4);

    J3DNativeState::setNumIndStages(0);
    J3DNativeState::setNumTevStages(4);
    J3DNativeState::setTevColor(GX_TEVREG0, ::sTevColor1);
    J3DNativeState::setTevColor(GX_TEVREG1, ::sTevColor2);
    J3DNativeState::setTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE0, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP0, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_CPREV, GX_CC_ZERO);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_2, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE2, GX_TEXCOORD2, GX_TEXMAP2, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C1, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    J3DNativeState::setTevOrder(GX_TEVSTAGE3, GX_TEXCOORD3, GX_TEXMAP3, GX_COLOR_NULL);
    J3DNativeState::setTevColorIn(GX_TEVSTAGE3, GX_CC_TEXC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    J3DNativeState::setTevColorOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setTevAlphaIn(GX_TEVSTAGE3, GX_CA_KONST, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO);
    J3DNativeState::setTevAlphaOp(GX_TEVSTAGE3, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    J3DNativeState::setBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_NOOP);
    J3DNativeState::setAlphaCompare(GX_GREATER, 0, GX_AOP_OR, GX_GREATER, 0);
    J3DNativeState::setZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    J3DNativeState::setZCompLoc(GX_TRUE);
    J3DNativeState::setCullMode(GX_CULL_NONE);
    J3DNativeState::setClipMode(GX_CLIP_ENABLE);
}

WaterRoad::WaterRoad(const char* pName)
    : LiveActor(pName), _8C(-1), mModelInfoHigh(), mModelInfoLow(), mRadius(::sDefaultRadius), mAlpha(0xFF), mBaseUp(0.0f, 1.0f, 0.0f), mRider(),
      mTexUV0(0.0f, 0.0f), mTexUV1(0.0f, 0.0f), mTexUV2(0.0f, 0.0f), mTexture(), mIndirect(), mEnvMap(), mClippingCenter(0.0f, 0.0f, 0.0f),
      mCamRailNearestPos(0.0f, 0.0f, 0.0f), mSoundObj(), mCameraInfo() {
    mBaseMtx.identity();
    mRiderMtx.identity();
    mBoundingBox.zero();
}

void WaterRoad::init(const JMapInfoIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_WaterRoad);
    MR::initDefaultPos(this, rIter);
    MR::makeMtxTR(mBaseMtx, this);
    mBaseMtx.getYDir(mBaseUp);
    MR::getJMapInfoArg0NoInit(rIter, &_8C);
    MR::getJMapInfoArg1NoInit(rIter, &mRadius);
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

    initEffectKeeper(0, "WaterRoad", false);
    MR::setEffectHostMtx(this, "Top", mBaseMtx);
    MR::setEffectHostMtx(this, "End", mBaseMtx);

    mTexture = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "Water.bti"), 0);
    mIndirect = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "WaterIndirect.bti"), 0);
    mEnvMap = new JUTTexture(MR::loadTexFromArc("WaterWave.arc", "EnvMap.bti"), 0);

    initSound(3, false);
    mSoundObj = new AudSoundObject(&mCamRailNearestPos, 4, MR::getCurrentHeap());

    MR::calcBoundingBox(this, &mBoundingBox, ::sClippingRadiusOffset);
    mBoundingBox.pad(mRadius);
    mBoundingBox.getCenter(&mClippingCenter);
    MR::setClippingTypeSphere(this, mClippingCenter.distance(mBoundingBox.i) + ::sClippingRadiusOffset, &mClippingCenter);
    MR::setClippingFarMax(this);

    bool hasDemo = MR::tryRegisterDemoCast(this, rIter);
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "\203\157\203\103\203\223\203\150\222\206" /* バインド中 */);
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

    if (MR::isDemoPartActive("\203\105\203\110\201\133\203\136\201\133\203\215\201\133\203\150\220\254\222\267" /* ウォーターロード成長 */)) {
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
    if (MR::isStep(this, 180)) {
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

    if (MR::isCorePadSwing(WPAD_CHAN0)) {
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

    if (MR::isCorePadSwing(WPAD_CHAN0)) {
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

    if (MR::isDemoPartLastStep("\203\105\203\110\201\133\203\136\201\133\203\215\201\133\203\150\217\157\214\273" /* ウォーターロード出現 */)) {
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoGrowUp));
    }
}

void WaterRoad::exeDemoGrowUp() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Top");
        MR::startSound(this, "SE_OJ_WATER_ROAD_GROW_START");
        MR::shakeCameraStrong();
        MR::tryRumblePadVeryStrong(this, WPAD_CHAN0);
    }

    updateDemo(MR::calcDemoPartStepRate("\203\105\203\110\201\133\203\136\201\133\203\215\201\133\203\150\220\254\222\267" /* ウォーターロード成長 */));

    MR::startLevelSound(this, "SE_OJ_LV_WATER_ROAD_GROW");
    MR::startAtmosphereLevelSE("SE_AT_LV_EARTHQUAKE");

    if (MR::isDemoPartLastStep("\203\105\203\110\201\133\203\136\201\133\203\215\201\133\203\150\220\254\222\267" /* ウォーターロード成長 */)) {
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
    mAlpha = (s32)(MR::calcNerveValue(this, ::sDisappearStep, 255.0f, 0.0f));

    if (MR::isStep(this, ::sDisappearStep)) {
        kill();
    }
}

void WaterRoad::movement() {
    // https://decomp.me/scratch/6VOUU

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

bool WaterRoad::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgAutoRushBegin(msg)) {
        if (!isNerve(GET_NERVE(WaterRoad, WaterRoadNrvWait))) {
            return false;
        }

        mRider = MR::getSensorHost(pSender);
        MR::moveCoordToNearestPos(this, mRider->mPosition);
        MR::startMultiActorCameraTargetPlayer(this, mCameraInfo, "\203\157\203\103\203\223\203\150\222\206" /* バインド中 */);
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvRideStart));
        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg)) {
        TVec3f front;
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
        MR::endBindAndPlayerJump(this, TVec3f(0.0f, 0.0f, 0.0f), 0);
        MR::endMultiActorCamera(this, mCameraInfo, "\203\157\203\103\203\223\203\150\222\206" /* バインド中 */, true);
        mRider = nullptr;
        setNerve(GET_NERVE(WaterRoad, WaterRoadNrvDisappear));
        return true;
    }

    return false;
}

void WaterRoad::updateDemo(f32 demoRate) {
    MR::setRailCoord(this, MR::getEaseOutValue(demoRate, 0.0f, 1.0f, 1.0f) * MR::getRailTotalLength(this));

    mPosition.set(MR::getRailPos(this));
    TVec3f front;
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

    TPos3f mtx;
    mtx.identity();
    mtx[0][2] = mTexUV0.x;
    mtx[1][2] = mTexUV0.y;
    J3DNativeShape::sendTexture(mtx, 0x1E, GX_MTX2x4);
    mtx[0][2] = mTexUV1.x;
    mtx[1][2] = mTexUV1.y;
    J3DNativeShape::sendTexture(mtx, 0x21, GX_MTX2x4);

    mTexture->load(GX_TEXMAP0);
    JUTTexture screenTex(MR::getScreenResTIMG(), 0);
    screenTex.load(GX_TEXMAP2);
    mEnvMap->load(GX_TEXMAP3);

    if (isNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoGrowUp)) || isNerve(GET_NERVE(WaterRoad, WaterRoadNrvDemoWaitEnd))) {
        mIndirect->load(GX_TEXMAP1);
        mtx[0][2] = mTexUV2.x;
        mtx[1][2] = mTexUV2.y;
        J3DNativeShape::sendTexture(mtx, 0x24, GX_MTX2x4);
        mModelInfoHigh->drawDirect(this);
        return;
    }

    TVec3f boundedPos;
    boundedPos.x = MR::clamp(MR::getPlayerPos()->x, mBoundingBox.i.x, mBoundingBox.f.x);
    boundedPos.y = MR::clamp(MR::getPlayerPos()->y, mBoundingBox.i.y, mBoundingBox.f.y);
    boundedPos.z = MR::clamp(MR::getPlayerPos()->z, mBoundingBox.i.z, mBoundingBox.f.z);

    if (MR::getPlayerPos()->distance(boundedPos) > ::sDistanceToLow) {
        mModelInfoLow->drawGD();
    } else {
        mIndirect->load(GX_TEXMAP1);
        mtx[0][2] = mTexUV2.x;
        mtx[1][2] = mTexUV2.y;
        J3DNativeShape::sendTexture(mtx, 0x24, GX_MTX2x4);
        mModelInfoHigh->drawGD();
    }
}
