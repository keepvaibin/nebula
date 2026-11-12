#include "Game/Render/Material.hpp"
#include "Game/MapObj/ElectricRailMoving.hpp"
#include <nebula_format.h>
#include "Game/Render/FrameList.hpp"
#include "Game/Render/TypedDraw.hpp"
#include "Game/AudioLib/AudAnmSoundObject.hpp"
#include "Game/Gravity/GravityInfo.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Color.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    // One sensor per stacked rail (obj_arg3).
    const char* const cSensorNameTable[] = {"body0", "body1", "body2", "body3", "body4", "body5", "body6", "body7"};
    const f32 sRailStep = 100.0f;  // between kept rail positions, and between stacked rails
};  // namespace

namespace NrvElectricRailMoving {
    NEW_NERVE(ElectricRailMovingNrvWait, ElectricRailMoving, Wait);
    NEW_NERVE(ElectricRailMovingNrvDisappear, ElectricRailMoving, Disappear);
    NEW_NERVE(ElectricRailMovingNrvDisappeared, ElectricRailMoving, Disappeared);
};  // namespace NrvElectricRailMoving

ElectricRailMovingPoint::ElectricRailMovingPoint(const char* pName) : LiveActor(pName), mHasShadow(true), mIsElectric(true) {
    lumacore::setClass(this, lumacore::kClassId< ElectricRailMovingPoint >);
}

void ElectricRailMovingPoint::init(const TableIter& /*rIter*/) {
    initModelManagerWithAnm("ElectricRailPoint", nullptr, 0);
    MR::connectToSceneMapObjDecoration(this);

    if (mHasShadow) {
        MR::initShadowVolumeSphere(this, 35.0f);
        MR::onCalcGravity(this);
    }

    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 8, 35.0f, Vec3f(0.0f, 0.0f, 0.0f));
    makeActorAppeared();
}

void ElectricRailMovingPoint::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (mIsElectric) {
        MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

ElectricRailMoving::ElectricRailMoving(const char* pName)
    : ElectricRailDrawn(pName), mSegmentNum(10), mMovementSpeed(10.0f), mSegmentLength(500.0f), mStackHeight(1), mAlpha(0x80), mSensorOffsets(),
      mRailPosNum(), mRailPositions(), mCoord(), mRailWidth(30.0f) {
    lumacore::setClass(this, lumacore::kClassId< ElectricRailMoving >);
    mTexMtx.identity();
}

void ElectricRailMoving::init(const TableIter& rIter) {
    s32 railType = 0;
    MR::getPlacementArg4NoInit(rIter, &railType);
    ElectricRailFunction::registerRail(this, static_cast< ElectricRailType >(railType + 2));
    MR::connectToSceneMapObjMovement(this);
    initMapToolInfo(rIter);
    initRail(rIter);
    initSensor();
    MR::setClippingFar200m(this);
    MR::setGroupClipping(this, rIter, 16);
    initPoints();
    initMesh();
    initSound(4, true);
    mSoundObject->setTrans(mSensorOffsets);
    initNerve(GET_NERVE(ElectricRailMoving, ElectricRailMovingNrvWait));
    makeActorAppeared();
}

// While it waits the rail is the sent mesh; vanishing, it is drawn each frame at the shrinking width.
void ElectricRailMoving::drawWith(MaterialDraw& rMaterial) const {
    if (!MR::isValidDraw(this)) {
        return;
    }

    rMaterial.setCoordinate(1, mTexMtx);
    rMaterial.setRegister(MaterialRegister_1, Color8(0xFF, 0xFF, 0xFF, mAlpha));
    TypedDraw site(rMaterial, nullptr, MR::getCameraViewMtx());

    if (isNerve(GET_NERVE(ElectricRailMoving, ElectricRailMovingNrvWait))) {
        mMesh.draw(site);
    } else {
        drawRail(site, mRailWidth);
    }
}

void ElectricRailMoving::disappear() {
    setNerve(GET_NERVE(ElectricRailMoving, ElectricRailMovingNrvDisappear));
}

bool ElectricRailMoving::isTouchRail(const HitSensor* pSensor, Vec3f* pPos, Vec3f* pDirection) const {
    Vec3f nearest;
    f32 coord = MR::calcNearestRailPos(&nearest, this, pSensor->mPosition);
    f32 radius = pSensor->mRadius;
    radius += ElectricRailFunction::getHitSensorRadius();

    if (!isValidCoord(coord) || !(nearest.squared(pSensor->mPosition) < radius * radius)) {
        return false;
    }

    if (pPos != nullptr) {
        pPos->set(nearest);
    }

    if (pDirection != nullptr) {
        MR::calcRailDirectionAtCoord(pDirection, this, coord);
    }

    return true;
}

void ElectricRailMoving::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver) && !MR::isPlayerSquat()) {
        MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
    }
}

// obj_arg0 segments, arg1 speed, arg2 segment length, arg3 stacked rails, arg4 rail type.
void ElectricRailMoving::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mSegmentNum);
    MR::getPlacementArg1NoInit(rIter, &mMovementSpeed);
    MR::getPlacementArg3NoInit(rIter, &mStackHeight);
    if (mSegmentNum < 1) {
        mSegmentNum = 1;
    }
    if (mStackHeight < 1) {
        mStackHeight = 1;
    }

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &ElectricRailMoving::disappear));
    }
}

void ElectricRailMoving::initSensor() {
    initHitSensor(mStackHeight);
    mSensorOffsets = new Vec3f[mStackHeight];

    for (s32 i = 0; i < mStackHeight; i++) {
        // The sensor keeps its name: the table's for the first eight, a name made here (in this actor's arena) past them.
        const char* pName;
        if (i < static_cast< s32 >(ARRAY_SIZE(cSensorNameTable))) {
            pName = cSensorNameTable[i];
        } else {
            char* const pMade = new char[16];
            nebula_snprintf(pMade, 16, "body%d", static_cast< int >(i));
            pName = pMade;
        }
        MR::addHitSensorPosMapObj(this, pName, 8, ElectricRailFunction::getHitSensorRadius(), &mSensorOffsets[i],
                                  Vec3f(0.0f, 0.0f, 0.0f));
    }

    updateHitSensorPos();
}

// The rail is kept as positions every sRailStep along it, the last at its end.
void ElectricRailMoving::initRail(const TableIter& rIter) {
    initRailRider(rIter);
    f32 clipRadius;
    MR::calcRailClippingInfo(&mPosition, &clipRadius, this, 100.0f, 500.0f);
    MR::setClippingTypeSphere(this, clipRadius, &mPosition);

    if (!MR::getPlacementArg2NoInit(rIter, &mSegmentLength)) {
        mSegmentLength = MR::getRailTotalLength(this) / (2 * mSegmentNum);
    }

    mRailPosNum = static_cast< s32 >(MR::getRailTotalLength(this) / sRailStep) + 2;
    mRailPositions = new Vec3f[mRailPosNum];

    for (s32 i = 0; i < mRailPosNum; i++) {
        f32 totalLength = MR::getRailTotalLength(this);
        f32 len = sRailStep * i >= totalLength ? totalLength : sRailStep * i;

        MR::calcRailPosAtCoord(&mRailPositions[i], this, len);
    }
}

// Each segment's front and back point, each with mStackHeight - 1 copies stacked sRailStep apart against gravity; an
// open rail adds its two ends (and their stacks) last. Only the bottom front and back points cast shadows.
void ElectricRailMoving::initPoints() {
    mPointNum = 2 * mSegmentNum;

    if (!MR::isLoopRail(this)) {
        mPointNum += 2;
    }

    mPointNum *= mStackHeight;
    mMovingPoints = new ElectricRailMovingPoint[mPointNum];

    if (!MR::isLoopRail(this)) {
        u32 endIndex = mPointNum - 2 * mStackHeight;
        Vec3f startPos, endPos;
        MR::calcRailStartPointPos(&startPos, this);
        MR::calcRailEndPointPos(&endPos, this);

        mMovingPoints[endIndex++].mPosition.set(startPos);
        mMovingPoints[endIndex++].mPosition.set(endPos);

        if (mStackHeight > 1) {
            Vec3f stackStep;
            MR::calcGravityVector(this, startPos, &stackStep, nullptr, 0);
            stackStep.scale(-sRailStep);

            Vec3f stackPos;
            stackPos.set(startPos);

            for (s32 i = 1; i < mStackHeight; i++) {
                stackPos.add(stackStep);
                mMovingPoints[endIndex++].mPosition.set(stackPos);
            }

            MR::calcGravityVector(this, endPos, &stackStep, nullptr, 0);
            stackStep.scale(-sRailStep);
            stackPos.set(endPos);

            for (s32 i = 1; i < mStackHeight; i++) {
                stackPos.add(stackStep);
                mMovingPoints[endIndex++].mPosition.set(stackPos);
            }
        }
    }

    updatePointPos();

    for (s32 i = 0; i < mPointNum; i++) {
        if (i % (2 * mStackHeight) >= 2) {
            mMovingPoints[i].mHasShadow = false;
        }

        mMovingPoints[i].initWithoutIter();
    }
}

void ElectricRailMoving::initMesh() {
    mMesh.init(mRailPosNum * 4 * mStackHeight);
    drawPlane(30.0f, 30.0f, -30.0f, -30.0f);
    drawPlane(-30.0f, 30.0f, 30.0f, -30.0f);
    mMesh.send();
}

// Two crossed planes (the diagonals of the width x width square around the rail).
void ElectricRailMoving::drawRail(TypedDraw& rDraw, f32 width) const {
    drawPlaneQuad(rDraw, width, width, -width, -width);
    drawPlaneQuad(rDraw, -width, width, width, -width);
}

void ElectricRailMoving::drawPlane(f32 side0, f32 up0, f32 side1, f32 up1) {
    for (s32 level = 0; level < mStackHeight; level++) {
        VertexPT* vtx = mMesh.strip(2 * mRailPosNum);

        for (s32 j = 0; j < mRailPosNum; j++) {
            setVertexAttribute(j, level, side0, up0, side1, up1, vtx);
        }
    }
}

void ElectricRailMoving::drawPlaneQuad(TypedDraw& rDraw, f32 side0, f32 up0, f32 side1, f32 up1) const {
    for (s32 level = 0; level < mStackHeight; level++) {
        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mRailPosNum * 2);

        for (s32 j = 0; j < mRailPosNum; j++) {
            setVertexAttribute(j, level, side0, up0, side1, up1, vtx);
        }
    }
}

// Rail position pointIndex on stacked rail level: the corners (side0, up0) and (side1, up1) across the rail.
void ElectricRailMoving::setVertexAttribute(int pointIndex, int level, f32 side0, f32 up0, f32 side1, f32 up1, VertexPT*& rVtx) const {
    f32 railLength = MR::getRailTotalLength(this);

    if (100.0f * pointIndex < railLength) {
        railLength = 100.0f * pointIndex;
    }

    f32 texCoord = 0.25f * railLength / 100.0f;
    Vec3f position;
    position.set(mRailPositions[pointIndex]);
    Vec3f railDirection;
    Vec3f gravity;

    MR::calcRailDirectionAtCoord(&railDirection, this, railLength);
    MR::calcGravityVector(this, position, &gravity, 0, 0);

    gravity.negate();

    if (level > 0) {
        f32 height = 100.0f * level;
        position += gravity * height;
    }

    Vec3f axis1;
    Vec3f axis2;
    Vec3f negRailDirection = -railDirection;

    if (MR::isSameDirection(gravity, negRailDirection)) {
        MR::makeAxisCrossPlane(&axis1, &axis2, negRailDirection);
    } else {
        MR::makeAxisFrontUp(&axis1, &axis2, negRailDirection, gravity);
    }

    const f32 x0 = position.x + axis1.x * side0 + axis2.x * up0;
    const f32 y0 = position.y + axis1.y * side0 + axis2.y * up0;
    const f32 z0 = position.z + axis1.z * side0 + axis2.z * up0;
    const f32 x1 = position.x + axis1.x * side1 + axis2.x * up1;
    const f32 y1 = position.y + axis1.y * side1 + axis2.y * up1;
    const f32 z1 = position.z + axis1.z * side1 + axis2.z * up1;
    *rVtx++ = {x0, y0, z0, texCoord, 0.0f};
    *rVtx++ = {x1, y1, z1, texCoord, 1.0f};
}

// The sensors sit at the rail point nearest the player, stacked; they are on only where a segment is.
void ElectricRailMoving::updateHitSensorPos() {
    f32 nearestRailPos = MR::calcNearestRailPos(mSensorOffsets, this, *MR::getPlayerCenterPos());

    if (mStackHeight > 1) {
        Vec3f stackStep;
        MR::calcGravityVector(this, mSensorOffsets[0], &stackStep, nullptr, 0);
        stackStep *= -sRailStep;

        for (s32 i = 1; i < mStackHeight; i++) {
            mSensorOffsets[i].add(mSensorOffsets[i - 1], stackStep);
        }
    }

    bool isValid = isValidCoord(nearestRailPos);

    for (s32 i = 0; i < mStackHeight; i++) {
        if (isValid) {
            MR::getSensorWithIndex(this, i)->validate();
        } else {
            MR::getSensorWithIndex(this, i)->invalidate();
        }
    }
}

// Places every segment's points from mCoord on; true when an open rail's first segment is near its end, so the end
// points show.
bool ElectricRailMoving::updatePointPos() {
    f32 railTotalLength = MR::getRailTotalLength(this);
    f32 segmentInterval = railTotalLength / mSegmentNum;
    bool reachedEnd = false;
    s32 pointIndex = 0;
    f32 railCoord = mCoord;

    Vec3f stackPosition;
    Vec3f gravity;
    Vec3f previousPosition;
    Vec3f position;

    for (s32 i = 0; i < mSegmentNum; i++) {
        f32 coord = getRepeatedCoord(railCoord);

        calcPointPos(&position, coord);
        f32 previousCoord = getRepeatedCoord(railCoord - mSegmentLength);

        calcPointPos(&previousPosition, previousCoord);

        mMovingPoints[pointIndex++].mPosition.set(position);
        mMovingPoints[pointIndex++].mPosition.set(previousPosition);

        if (mStackHeight > 1) {
            MR::calcGravityVector(this, position, &gravity, nullptr, 0);
            gravity *= -sRailStep;

            stackPosition.set(position);

            for (s32 j = 1; j < mStackHeight; j++) {
                stackPosition += gravity;
                mMovingPoints[pointIndex++].mPosition.set(stackPosition);
            }

            MR::calcGravityVector(this, previousPosition, &gravity, nullptr, 0);
            gravity *= -sRailStep;

            stackPosition.set(previousPosition);

            for (s32 j = 1; j < mStackHeight; j++) {
                stackPosition += gravity;
                mMovingPoints[pointIndex++].mPosition.set(stackPosition);
            }
        }

        if (!MR::isLoopRail(this) && (getRepeatedCoord(railCoord + mMovementSpeed) - (mSegmentLength + (3.0f * mMovementSpeed))) < 0.0f) {
            reachedEnd = true;
        }

        railCoord = getRepeatedCoord(railCoord + segmentInterval);
    }

    return reachedEnd;
}

void ElectricRailMoving::updatePointPosAndModel() {
    bool reachedEnd = updatePointPos();

    if (MR::isLoopRail(this)) {
        return;
    }

    for (s32 i = 0; i < 2 * mStackHeight; i++) {
        if (reachedEnd) {
            MR::showModelIfHidden(&mMovingPoints[mPointNum - 1 - i]);
        } else {
            MR::hideModelIfShown(&mMovingPoints[mPointNum - 1 - i]);
        }
    }
}

// On a segment: within mSegmentLength behind a segment front (fronts every length / mSegmentNum from mCoord).
bool ElectricRailMoving::isValidCoord(f32 coord) const {
    f32 repeated = getRepeatedCoord(coord - mCoord);
    f32 interval = MR::getRailTotalLength(this) / mSegmentNum;
    f32 offset = MR::mod(repeated, interval);
    return interval - mSegmentLength < offset;
}

f32 ElectricRailMoving::getRepeatedCoord(f32 coord) const {
    f32 len = MR::getRailTotalLength(this);

    // A rail with no length has one coord (the repeat divides by the length).
    if (!(len > 0.0f)) {
        return 0.0f;
    }

    return MR::repeat(coord, 0.0f, len);
}

// Between the kept positions around coord; the last stretch is the rail's remainder.
void ElectricRailMoving::calcPointPos(Vec3f* pOutPos, f32 coord) const {
    // The pair of kept positions stays inside the table (a speed past the rail's length can leave coord outside it).
    f32 indexValue = coord / 100.0f;
    if (!(indexValue >= 0.0f)) {
        indexValue = 0.0f;
    } else if (indexValue > static_cast< f32 >(mRailPosNum - 2)) {
        indexValue = static_cast< f32 >(mRailPosNum - 2);
    }

    s32 index = static_cast< s32 >(indexValue);
    f32 within = MR::mod(coord, 100.0f);
    f32 stretch = 100.0f;

    if ((mRailPosNum - 3) < index) {
        stretch = (MR::getRailTotalLength(this) - (100.0f * index));
    }

    pOutPos->lerp(mRailPositions[index], mRailPositions[index + 1], stretch > 0.0f ? (within / stretch) : 0.0f);
}

// Moves the segments on; the texture scrolls with them and the alpha follows the segments' share of the rail.
void ElectricRailMoving::move() {
    mCoord = getRepeatedCoord(mCoord + mMovementSpeed);

    // A rail with no length keeps its first placement (the texture scale and segment share divide by the length).
    if (!(MR::getRailTotalLength(this) > 0.0f)) {
        return;
    }

    f32 scale = (100.0f * mSegmentNum) / (0.25f * MR::getRailTotalLength(this));

    mTexMtx.identity();

    mTexMtx.mMtx[0][0] = scale;
    mTexMtx.mMtx[0][3] = -((0.25f * (scale * mCoord)) / 100.0f);
    f32 railTotalLength = MR::getRailTotalLength(this);
    f32 segmentInterval = railTotalLength / mSegmentNum;
    f32 angle = (-TWO_PI * mSegmentLength) / segmentInterval;
    f32 sinValue = Angle::sinRadian(angle);
    f32 value = (15.0f * sinValue) + ((255.0f * mSegmentLength) / segmentInterval);

    s32 alpha = static_cast< s32 >(value);
    if (alpha < 0) {
        alpha = 0;
    } else if (alpha > 255) {
        alpha = 255;
    }

    mAlpha = alpha;

    updatePointPosAndModel();
}

void ElectricRailMoving::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_ELEC_RAIL_VANISH");
        MR::invalidateHitSensors(this);

        for (s32 i = 0; i < mPointNum; i++) {
            mMovingPoints[i].mIsElectric = false;
        }
    }

    move();
    mRailWidth = MR::calcNerveEaseInValue(this, 20, 30.0f, 0.0f);

    if (MR::isStep(this, 20)) {
        setNerve(GET_NERVE(ElectricRailMoving, ElectricRailMovingNrvDisappeared));
    }
}

void ElectricRailMoving::exeDisappeared() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
    }
}

void ElectricRailMoving::exeWait() {
    move();
    updateHitSensorPos();
    MR::startLevelSound(this, "SE_OJ_LV_ELEC_RAIL_HAM");
}
