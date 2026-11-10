#include "Game/Map/OceanRing.hpp"
#include "Game/AudioLib/AudSoundObject.hpp"
#include "Game/Map/OceanRingBloomDrawer.hpp"
#include "Game/Map/OceanRingDrawer.hpp"
#include "Game/Map/OceanRingPipe.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_OceanRing_f1, "OceanRing.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_OceanRing_f2, "OceanRing.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWaterInfo_f1, "calcWaterInfo.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcStreamVec_f1, "calcStreamVec.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCurrentWidthRate_f1, "calcCurrentWidthRate.f1", 12.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCurrentWidthRate_f2, "calcCurrentWidthRate.f2", 12.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCurrentWidthRate_f3, "calcCurrentWidthRate.f3", 12.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCurrentFlowSpeedRate_f1, "calcCurrentFlowSpeedRate.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCurrentFlowSpeedRate_f2, "calcCurrentFlowSpeedRate.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcCurrentFlowSpeedRate_f3, "calcCurrentFlowSpeedRate.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcClippingBox_f1, "calcClippingBox.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcClippingBox_f2, "calcClippingBox.f2", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcClippingBox_f3, "calcClippingBox.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcClippingBox_f4, "calcClippingBox.f4", 100.0f);

// The water is a grid along the rail: one cross-section of cPointsAcross points every sPointIntervalLine,
// cPointSpacing apart at width rate 1 (sDefaultWidth either side of the center point).
namespace {
    NEBULA_TUNABLE(f32, sPointIntervalLine, 200.0f);
    NEBULA_TUNABLE(f32, sDefaultWidth, 1200.0f);
    NEBULA_TUNABLE(f32, sEdgePointNum, 2.0f);
    NEBULA_TUNABLE(f32, sUpdateDistanceMax, 5000.0f);
    NEBULA_TUNABLE(f32, sWaveSpeed0, -0.04f);
    NEBULA_TUNABLE(f32, sWaveSpeed1, -0.06f);

    NEBULA_TUNABLE(s32, cPointsAcross, 15);
    NEBULA_TUNABLE(s32, cCenterPoint, 7);
    NEBULA_TUNABLE(f32, cPointSpacing, 171.42857f);  // sDefaultWidth / cCenterPoint
    // Segments kept updating behind the player; mUpdateSegsAhead are ahead.
    NEBULA_TUNABLE(s32, cUpdateSegsBehind, 8);
    NEBULA_TUNABLE(s32, cFadeSegs, 3);
    NEBULA_TUNABLE(f32, cNormalSampleOffset, 10.0f);
};  // namespace

OceanRing::OceanRing(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< OceanRing >);
    mWaterPointNum = 0;
    mSegCount = 0;
    mStride = cPointsAcross;
    mWaterPoints = nullptr;
    mWidthMax = 0.0f;
    mUpdateSegsAhead = 30;
    mWaveTheta0 = 0.0f;
    mWaveTheta1 = 0.0f;
    mWaveHeight1 = cNum_OceanRing_f1;
    mWaveHeight2 = cNum_OceanRing_f2;
    mIsClipped = false;
    mPlayerCoord = 0.0f;
    mNearestPos.zero();
    mNearestDir.set(0.0f, 0.0f, 1.0f);
    mRingDrawer = nullptr;
    mClippingCenter.zero();
    mNearestToWatchCam.zero();
    mWatchSoundObj = nullptr;
    mNearestToCam.zero();
    mCamSoundObj = nullptr;
    mOceanRingPipe = nullptr;
    mBloomDrawer = nullptr;
    mUpdateBox.i.zero();
    mUpdateBox.f.zero();
    mClippingBox.i.zero();
    mClippingBox.f.zero();
}

// Obj_arg0: 0 is a pipe of water (with its own model); 2 low waves, else medium waves, no pipe.
// Obj_arg1: segments updated ahead of the player.
void OceanRing::init(const TableIter& rIter) {
    MR::connectToScene(this, MR::MovementType_MapObj, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_OceanRing);
    MR::initDefaultPos(this, rIter);
    initRailRider(rIter);
    initPoints();
    mRingDrawer = new OceanRingDrawer(this);
    calcClippingBox();
    WaterAreaFunction::entryOceanRing(this);
    s32 shape = 0;
    MR::getPlacementArg0NoInit(rIter, &shape);

    if (shape == 0) {
        mOceanRingPipe = new OceanRingPipe(this, mWidthMax, sDefaultWidth);
        mOceanRingPipe->init(rIter);
    } else {
        if (shape == 2) {
            mWaveHeight1 = cNum_init_f1;
            mWaveHeight2 = cNum_init_f2;
        } else {
            mWaveHeight1 = cNum_init_f3;
            mWaveHeight2 = cNum_init_f4;
        }

        MR::loadTexFromArc("OceanRing", "WaterPipeInside.bti");
    }

    MR::getPlacementArg1NoInit(rIter, &mUpdateSegsAhead);
    mBloomDrawer = new OceanRingBloomDrawer(this);
    mBloomDrawer->initWithoutIter();
    mWatchSoundObj = new AudSoundObject(&mNearestToWatchCam, 4, NebulaArena::current());
    mCamSoundObj = new AudSoundObject(&mNearestToCam, 4, NebulaArena::current());
    MR::registerDemoSimpleCastAll(this);
    makeActorAppeared();
}

void OceanRing::initAfterPlacement() {
    for (s32 i = 0; i < mWaterPointNum; i++) {
        mWaterPoints[i]->initAfterPlacement();
    }
}

// Inside the tube: within the width of the nearest rail point and on the far side of it along gravity.
bool OceanRing::isInWater(const Vec3f& rPos) const {
    if (!mClippingBox.intersectsPoint(rPos)) {
        return false;
    }

    Vec3f nearPos;
    f32 coord = calcNearestPos(rPos, &nearPos, nullptr, nullptr);
    f32 width = (mWidthMax * calcCurrentWidthRate(coord));

    if (nearPos.distance(rPos) > width) {
        return false;
    }

    Vec3f fromNear(rPos);
    fromNear.sub(nearPos);
    Vec3f gravity;
    MR::calcGravityVector(this, rPos, &gravity, nullptr, 0);
    return !(fromNear.dot(gravity) < 0.0f);
}

// The cross-section is a half circle of the current width: depth is cos of the side offset's share of it.
bool OceanRing::calcWaterInfo(const Vec3f& rPos, const Vec3f& rGravity, WaterInfo* pInfo) const {
    Vec3f nearPos, nearDir;
    f32 coord = calcNearestPos(rPos, &nearPos, &nearDir, nullptr);
    f32 width = (mWidthMax * calcCurrentWidthRate(coord));
    Vec3f streamVec;
    calcStreamVec(rPos, coord, &streamVec);
    pInfo->mStreamVec.set(streamVec);
    pInfo->mWaveHeight = calcWaveHeight(rPos, coord, &pInfo->mSurfaceNormal);

    Vec3f side(rPos);
    side.sub(nearPos);
    f32 height = MR::vecKillElement(side, -rGravity, &side);
    // A rail point with width 0 makes a tube with no depth, not a division by 0.
    f32 angle = width > 0.0f ? side.length() / width : 1.0f;
    angle *= PI;
    angle *= cNum_calcWaterInfo_f1;

    pInfo->mDepth = MR::cos(angle) * width;
    pInfo->mDepth += height;

    pInfo->mCamWaterDepth = -height;

    Vec3f gravity(rGravity);
    MR::calcGravityVector(this, rPos, &gravity, nullptr, 0);
    pInfo->mSurfaceNormal.set(-gravity);

    pInfo->mSurfacePos.set(rPos + rGravity * height);

    Vec3f toEdge(rPos);
    toEdge.sub(nearPos);

    if (MR::isNearZero(toEdge)) {
        pInfo->mEdgeDistance = width;
        Vec3f edgePos(rGravity);
        edgePos.scale(width);
        edgePos.add(nearPos);
        pInfo->mEdgePos.set(edgePos);
    } else {
        MR::normalize(&toEdge);
        toEdge.scale(width);
        toEdge.add(nearPos);
        pInfo->mEdgeDistance = toEdge.distance(rPos);
        pInfo->mEdgePos.set(toEdge);
    }

    return true;
}

// The rail coord nearest rPos (by the nearest center point and the foot on the segment toward the nearer
// neighbour). pNearPos gets the foot, pDir the rail direction there, pCrossPos the foot on the cross-section.
f32 OceanRing::calcNearestPos(const Vec3f& rPos, Vec3f* pNearPos, Vec3f* pDir, Vec3f* pCrossPos) const {
    WaterPoint* pNearest = getPoint(cCenterPoint, 0);
    f32 nearestDist = pNearest->mOrigPos.distance(rPos);

    s32 nearestSeg = 0;
    for (s32 seg = 1; seg < mSegCount; seg++) {
        WaterPoint* pPoint = getPoint(cCenterPoint, seg);
        f32 dist = pPoint->mOrigPos.distance(rPos);
        if (dist < nearestDist) {
            nearestDist = dist;
            pNearest = pPoint;
            nearestSeg = seg;
        }
    }

    f32 coord = 0.0f;
    if (nearestSeg > 0 && nearestSeg < mSegCount - 1) {
        WaterPoint* pPrev = getPoint(cCenterPoint, nearestSeg - 1);
        WaterPoint* pNext = getPoint(cCenterPoint, nearestSeg + 1);
        f32 prevDist = pPrev->mOrigPos.distance(rPos);

        if (prevDist < pNext->mOrigPos.distance(rPos)) {
            coord = (sPointIntervalLine * (nearestSeg - MR::calcPerpendicFootToLine(pNearPos, rPos, pNearest->mOrigPos, pPrev->mOrigPos)));
        } else {
            coord = (sPointIntervalLine * (nearestSeg + MR::calcPerpendicFootToLine(pNearPos, rPos, pNearest->mOrigPos, pNext->mOrigPos)));
        }
    } else {
        pNearPos->set(pNearest->mOrigPos);
    }

    if (pCrossPos) {
        Vec3f railDir, railPos;
        MR::calcRailPosAndDirectionAtCoord(&railPos, &railDir, this, coord);
        Vec3f gravity(0.0f, -1.0f, 0.0f);
        MR::calcGravityVector(this, railPos, &gravity, 0, 0);
        Vec3f side = MR::getRailDirection(this).cross(-gravity);
        f32 width = (mWidthMax * calcCurrentWidthRate(coord));
        Vec3f leftEdge(side);
        leftEdge.scale(-width);
        leftEdge.add(*pNearPos);
        Vec3f rightEdge(side);
        rightEdge.scale(width);
        rightEdge.add(*pNearPos);
        MR::calcPerpendicFootToLineInside(pCrossPos, rPos, leftEdge, rightEdge);
    }

    if (pDir) {
        MR::calcRailDirectionAtCoord(pDir, this, coord);
    }

    return coord;
}

// The wave height under rPos at rail coord coord; the normal from four samples cNormalSampleOffset to each side.
f32 OceanRing::calcWaveHeight(const Vec3f& rPos, f32 coord, Vec3f* pNormal) const {
    MR::setRailCoord(const_cast< OceanRing* >(this), coord);
    Vec3f fromRail(rPos);
    fromRail.sub(MR::getRailPos(this));
    Vec3f gravity(0.0f, -1.0f, 0.0f);
    MR::calcGravityVector(this, rPos, &gravity, nullptr, 0);
    Vec3f side = MR::getRailDirection(this).cross(-gravity);

    f32 sideOffset = fromRail.dot(side);
    f32 across = (sDefaultWidth + sideOffset);
    s32 nearPoint = (cCenterPoint + (sideOffset / cPointSpacing));
    f32 segRate = (coord / sPointIntervalLine);
    s32 point = MR::clamp(nearPoint, 0, mStride - 1);
    s32 seg = MR::clamp((s32)segRate, 0, mSegCount - 1);

    const WaterPoint* pPoint = getPoint(point, seg);
    f32 height = pPoint->calcHeight(mWaveTheta0, mWaveTheta1, mWaveHeight1, mWaveHeight2, across, coord);
    Vec3f left(side);
    left.scale(-cNormalSampleOffset);
    Vec3f right(side);
    right.scale(cNormalSampleOffset);
    Vec3f front(MR::getRailDirection(this));
    front.scale(cNormalSampleOffset);
    Vec3f back(MR::getRailDirection(this));
    back.scale(-cNormalSampleOffset);
    left.y += pPoint->calcHeight(mWaveTheta0, mWaveTheta1, mWaveHeight1, mWaveHeight2, (across - cNormalSampleOffset), coord);
    right.y += pPoint->calcHeight(mWaveTheta0, mWaveTheta1, mWaveHeight1, mWaveHeight2, (cNormalSampleOffset + across), coord);
    front.y += pPoint->calcHeight(mWaveTheta0, mWaveTheta1, mWaveHeight1, mWaveHeight2, across, (cNormalSampleOffset + coord));
    back.y += pPoint->calcHeight(mWaveTheta0, mWaveTheta1, mWaveHeight1, mWaveHeight2, across, coord - cNormalSampleOffset);
    Vec3f leftToRight(right);
    leftToRight.sub(left);
    Vec3f frontToBack(back);
    frontToBack.sub(front);
    pNormal->cross(frontToBack, leftToRight);

    if (MR::isNearZero(*pNormal)) {
        pNormal->set(-gravity);
    } else {
        MR::normalize(pNormal);
    }

    return height;
}

// The stream flows along the rail at the player's nearest point, 10 at flow rate 1, within the width.
void OceanRing::calcStreamVec(const Vec3f& rPos, f32 coord, Vec3f* pStreamVec) const {
    pStreamVec->zero();
    f32 width = (mWidthMax * calcCurrentWidthRate(coord));
    f32 speed = (cNum_calcStreamVec_f1 * calcCurrentFlowSpeedRate(coord));
    Vec3f toNearest(mNearestPos);
    toNearest.sub(rPos);
    if (toNearest.length() < width) {
        pStreamVec->set(mNearestDir);
        pStreamVec->scale(speed);
    }
}

void OceanRing::movement() {
    mWaveTheta0 += sWaveSpeed0;
    mWaveTheta1 += sWaveSpeed1;
    updatePoints();
    mRingDrawer->update();

    Vec3f nearPos;
    calcNearestPos(MR::getCameraWatchPos(), &nearPos, nullptr, &mNearestToWatchCam);
    MR::startSoundObjectLevel(mWatchSoundObj, "SE_AT_LV_OCEAN_RING");
    mWatchSoundObj->process();

    calcNearestPos(MR::getCamPos(), &nearPos, nullptr, &mNearestToCam);
    MR::startSoundObjectLevel(mCamSoundObj, "SE_AT_LV_OCEAN_RING_SUB");
    mCamSoundObj->process();
}

void OceanRing::startClipped() {
    LiveActor::startClipped();

    if (mOceanRingPipe != nullptr) {
        mOceanRingPipe->startClipped();
    }
}

void OceanRing::endClipped() {
    LiveActor::endClipped();

    if (mOceanRingPipe != nullptr) {
        mOceanRingPipe->endClipped();
    }
}

// One cross-section per segment along the rail; the outer sEdgePointNum points each side ease their waves out.
// Rail point arg 1 is the width (12 is rate 1), arg 0 the flow speed (100 is rate 1).
void OceanRing::initPoints() {
    s32 segCount = MR::getRailTotalLength(this) / sPointIntervalLine;
    segCount++;
    mSegCount = segCount;
    mWaterPointNum = cPointsAcross * segCount;
    f32 totalLength = MR::getRailTotalLength(this);
    f32 segLength = (totalLength / mSegCount);
    mWidthMax = sDefaultWidth;
    mWaterPoints = new WaterPoint*[mWaterPointNum];
    s32 index = 0;

    for (s32 seg = 0; seg < mSegCount; seg++) {
        Vec3f up(0.0f, 1.0f, 0.0f);
        MR::calcGravityVector(this, MR::getRailPos(this), &up, nullptr, 0);
        up.scale(-1.0f);
        Vec3f side = MR::getRailDirection(this).cross(up);
        MR::normalize(&side);

        f32 railCoord = MR::getRailCoord(this);
        f32 widthRate = calcCurrentWidthRate(railCoord);

        for (s32 j = -cCenterPoint; j <= cCenterPoint; j++) {
            Vec3f pos(side);
            pos.scale((widthRate * (cPointSpacing * j)));
            pos.add(MR::getRailPos(this));
            f32 ease = 1.0f;
            s32 fromEdge = cCenterPoint - MR::abs(j);
            if (fromEdge < sEdgePointNum) {
                f32 edgeRate = fromEdge / sEdgePointNum;
                ease = MR::getEaseOutValue(edgeRate, 0.0f, 1.0f, 1.0f);
            }

            mWaterPoints[index] = new WaterPoint(pos, up, ((j + cCenterPoint) * cPointSpacing), (seg * segLength), ease);
            index++;
        }

        MR::moveCoord(this, segLength);
    }
}

// Updates the segments from cUpdateSegsBehind behind the player to mUpdateSegsAhead ahead (ahead is the way
// the camera looks along the rail), fading the waves in over cFadeSegs at each open end; nothing beyond
// sUpdateDistanceMax. A loop rail wraps the window around.
void OceanRing::updatePoints() {
    if (!mUpdateBox.intersectsPoint(*MR::getPlayerPos())) {
        mIsClipped = true;
        return;
    }

    mPlayerCoord = calcNearestPos(*MR::getPlayerPos(), &mNearestPos, &mNearestDir, nullptr);
    if (mNearestPos.distance(*MR::getPlayerPos()) > sUpdateDistanceMax) {
        mIsClipped = true;
        return;
    }

    mIsClipped = false;
    if (mSegCount < mUpdateSegsAhead + cUpdateSegsBehind) {
        updatePointsInLine(0, mSegCount - 1, 0, 0);
        return;
    }

    bool isCamForward = !(MR::getCamZdir().dot(mNearestDir) < 0.0f);

    s32 playerSeg = (mPlayerCoord / sPointIntervalLine);
    s32 first, last;
    if (isCamForward) {
        first = playerSeg - cUpdateSegsBehind;
        last = playerSeg + mUpdateSegsAhead;
    } else {
        last = playerSeg + cUpdateSegsBehind;
        first = playerSeg - mUpdateSegsAhead;
    }

    if (!MR::isLoopRail(this)) {
        updatePointsInLine(MR::clamp(first, 0, mSegCount - 1), MR::clamp(last, 0, mSegCount - 1), cFadeSegs, cFadeSegs);
        return;
    }

    if (first >= 0 && last < mSegCount) {
        updatePointsInLine(first, last, cFadeSegs, cFadeSegs);
        return;
    }

    const s32 lastSeg = mSegCount - 1;
    if (first < 0) {
        updatePointsInLine(0, last, 0, cFadeSegs);
        updatePointsInLine(lastSeg != 0 ? (lastSeg + first) % lastSeg : first, lastSeg, cFadeSegs, 0);
        return;
    }

    if (last > lastSeg) {
        updatePointsInLine(first, lastSeg, cFadeSegs, 0);
        updatePointsInLine(0, lastSeg != 0 ? (lastSeg + last) % lastSeg : last, 0, cFadeSegs);
    }
}

// Segments [first, end); the wave rate ramps 0 to 1 over the first fadeInSegs and 1 to 0 over the last fadeOutSegs.
void OceanRing::updatePointsInLine(s32 first, s32 end, s32 fadeInSegs, s32 fadeOutSegs) {
    const s32 fadeInEnd = first + fadeInSegs;
    const s32 fadeOutStart = end - fadeOutSegs;

    for (s32 seg = first; seg < end; seg++) {
        f32 rate = 1.0f;
        if (seg < fadeInEnd) {
            rate = (f32)(seg - first) / (f32)fadeInSegs;
        }

        if (seg >= fadeOutStart) {
            rate = ((f32)(end - 1 - seg) / (f32)fadeOutSegs);
        }

        for (s32 point = 0; point < mStride; point++) {
            getPoint(point, seg)->updatePos(mWaveTheta0, mWaveTheta1, mWaveHeight1, mWaveHeight2, rate);
        }
    }
}

// Rail point arg 1 between the two rail points around coord, by distance; 12 is rate 1.
f32 OceanRing::calcCurrentWidthRate(f32 coord) const {
    f32 toCurrent = 0.0f;
    f32 toNext = 0.0f;
    MR::setRailCoord(const_cast< OceanRing* >(this), coord);
    MR::calcDistanceToCurrentAndNextRailPoint(this, &toCurrent, &toNext);
    f32 total = (toCurrent + toNext);

    if (total < 1.0f) {
        return 1.0f;
    }

    f32 currentWidth = cNum_calcCurrentWidthRate_f1;
    f32 nextWidth = cNum_calcCurrentWidthRate_f2;
    MR::getCurrentRailPointArg1NoInit(this, &currentWidth);
    MR::getNextRailPointArg1NoInit(this, &nextWidth);
    return ((((currentWidth * toNext) + (nextWidth * toCurrent)) / total) / cNum_calcCurrentWidthRate_f3);
}

// Rail point arg 0 between the two rail points around coord, by distance; 100 is rate 1.
f32 OceanRing::calcCurrentFlowSpeedRate(f32 coord) const {
    f32 toCurrent = 0.0f;
    f32 toNext = 0.0f;
    MR::setRailCoord(const_cast< OceanRing* >(this), coord);
    MR::calcDistanceToCurrentAndNextRailPoint(this, &toCurrent, &toNext);
    f32 total = (toCurrent + toNext);

    if (total < 1.0f) {
        return 1.0f;
    }

    f32 currentSpeed = cNum_calcCurrentFlowSpeedRate_f1;
    f32 nextSpeed = cNum_calcCurrentFlowSpeedRate_f2;
    MR::getCurrentRailPointArg0NoInit(this, &currentSpeed);
    MR::getNextRailPointArg0NoInit(this, &nextSpeed);
    return ((((currentSpeed * toNext) + (nextSpeed * toCurrent)) / total) / cNum_calcCurrentFlowSpeedRate_f3);
}

// mClippingBox: the rail's box grown by the widest point and mWidthMax; mUpdateBox grows it by
// sUpdateDistanceMax more (before the mWidthMax step).
void OceanRing::calcClippingBox() {
    f32 widest = sDefaultWidth;
    for (s32 i = 0; i < MR::getRailPointNum(this); i++) {
        f32 widthArg = 0.0f;
        MR::getRailPointArg1NoInit(this, i, &widthArg);
        f32 width = (cNum_calcClippingBox_f1 * widthArg);

        if (width >= widest) {
            widest = width;
        }
    }

    MR::calcBoundingBox(this, &mClippingBox, cNum_calcClippingBox_f2);
    Vec3f widestGrow(widest);
    mClippingBox.i.sub(widestGrow);
    mClippingBox.f.add(widestGrow);
    mUpdateBox.i.set(mClippingBox.i);
    mUpdateBox.f.set(mClippingBox.f);
    Vec3f updateGrow(sUpdateDistanceMax);
    mUpdateBox.i.sub(updateGrow);
    mUpdateBox.f.add(updateGrow);
    Vec3f widthGrow(mWidthMax);
    mClippingBox.i.sub(widthGrow);
    mClippingBox.f.add(widthGrow);
    mClippingCenter.lerp(mClippingBox.f, mClippingBox.i, cNum_calcClippingBox_f3);
    f32 radius = mClippingCenter.distance(mClippingBox.i);
    MR::setClippingTypeSphere(this, (cNum_calcClippingBox_f4 + radius), &mClippingCenter);
    MR::setClippingFarMax(this);
}

void OceanRing::draw() const {
    if (MR::isValidDraw(this)) {
        mRingDrawer->draw();
    }
}

WaterPoint* OceanRing::getPoint(int index) const {
    return mWaterPoints[index];
}

WaterPoint* OceanRing::getPoint(int point, int seg) const {
    return mWaterPoints[point + seg * mStride];
}
