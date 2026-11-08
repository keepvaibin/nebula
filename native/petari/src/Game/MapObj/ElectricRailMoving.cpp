#include "Game/Render/Material.hpp"
#include "Game/MapObj/ElectricRailMoving.hpp"
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

namespace {
    const char* cSensorNameTable[] = {"body0", "body1", "body2", "body3", "body4", "body5", "body6", "body7"};
};  // namespace

namespace NrvElectricRailMoving {
    NEW_NERVE(ElectricRailMovingNrvWait, ElectricRailMoving, Wait);
    NEW_NERVE(ElectricRailMovingNrvDisappear, ElectricRailMoving, Disappear);
    NEW_NERVE(ElectricRailMovingNrvDisappeared, ElectricRailMoving, Disappeared);
};  // namespace NrvElectricRailMoving

ElectricRailMovingPoint::ElectricRailMovingPoint(const char* pName) : LiveActor(pName) {
    mHasShadow = 1;
    mIsElectric = 1;
}

void ElectricRailMovingPoint::init(const TableIter& rIter) {
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
    mTexMtx.identity();
}

void ElectricRailMoving::init(const TableIter& rIter) {
    s32 arg = 0;
    MR::getPlacementArg4NoInit(rIter, &arg);
    ElectricRailFunction::registerRail(this, (ElectricRailType)(arg + 2));
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

void ElectricRailMoving::drawWith(MaterialDraw& rMaterial) const {
    if (MR::isValidDraw(this)) {
        rMaterial.setCoordinate(1, mTexMtx);
        rMaterial.setRegister(MaterialRegister_1, Color8(0xFF, 0xFF, 0xFF, mAlpha));
        TypedDraw site(rMaterial, nullptr, MR::getCameraViewMtx());

        if (isNerve(GET_NERVE(ElectricRailMoving, ElectricRailMovingNrvWait))) {
            mMesh.draw(site);
        } else {
            drawRail(site, mRailWidth);
        }
    }
}

void ElectricRailMoving::disappear() {
    setNerve(GET_NERVE(ElectricRailMoving, ElectricRailMovingNrvDisappear));
}

bool ElectricRailMoving::isTouchRail(const HitSensor* pSensor, Vec3f* pPos, Vec3f* pDirection) const {
    f32 radius;
    f32 coord;

    Vec3f nearest;
    coord = MR::calcNearestRailPos(&nearest, this, pSensor->mPosition);
    radius = pSensor->mRadius;
    radius += ElectricRailFunction::getHitSensorRadius();

    if (isValidCoord(coord) && nearest.squared(pSensor->mPosition) < radius * radius) {
        if (pPos != nullptr) {
            pPos->set(nearest);
        }

        if (pDirection != nullptr) {
            MR::calcRailDirectionAtCoord(pDirection, this, coord);
        }

        return true;
    }

    return false;
}

void ElectricRailMoving::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (!MR::isPlayerSquat()) {
            MR::sendMsgEnemyAttackElectric(pReceiver, pSender);
        }
    }
}

void ElectricRailMoving::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mSegmentNum);
    MR::getPlacementArg1NoInit(rIter, &mMovementSpeed);
    MR::getPlacementArg3NoInit(rIter, &mStackHeight);

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &ElectricRailMoving::disappear));
    }
}

void ElectricRailMoving::initSensor() {
    initHitSensor(mStackHeight);
    mSensorOffsets = new Vec3f[mStackHeight];

    for (s32 i = 0; i < mStackHeight; i++) {
        MR::addHitSensorPosMapObj(this, ::cSensorNameTable[i], 8, ElectricRailFunction::getHitSensorRadius(), &mSensorOffsets[i],
                                  Vec3f(0.0f, 0.0f, 0.0f));
    }

    updateHitSensorPos();
}

void ElectricRailMoving::initRail(const TableIter& rIter) {
    initRailRider(rIter);
    f32 v11;
    MR::calcRailClippingInfo(&mPosition, &v11, this, 100.0f, 500.0f);
    MR::setClippingTypeSphere(this, v11, &mPosition);

    if (!MR::getPlacementArg2NoInit(rIter, &mSegmentLength)) {
        mSegmentLength = MR::getRailTotalLength(this) / (2 * mSegmentNum);
    }

    mRailPosNum = static_cast< s32 >(MR::getRailTotalLength(this) / 100.0f) + 2;
    mRailPositions = new Vec3f[mRailPosNum];

    for (s32 i = 0; i < mRailPosNum; i++) {
        f32 totalLength = MR::getRailTotalLength(this);
        f32 len = 100.0f * i >= totalLength ? totalLength : 100.0f * i;

        MR::calcRailPosAtCoord(&mRailPositions[i], this, len);
    }
}

void ElectricRailMoving::initPoints() {
    mPointNum = 2 * mSegmentNum;

    if (!MR::isLoopRail(this)) {
        mPointNum += 2;
    }

    mPointNum *= mStackHeight;
    mMovingPoints = new ElectricRailMovingPoint[mPointNum];

    if (!MR::isLoopRail(this)) {
        u32 v4 = mPointNum - 2 * mStackHeight;
        Vec3f startPos, endPos;
        MR::calcRailStartPointPos(&startPos, this);
        MR::calcRailEndPointPos(&endPos, this);

        mMovingPoints[v4++].mPosition.set(startPos);
        mMovingPoints[v4++].mPosition.set(endPos);
        u32 v6 = v4;

        if (mStackHeight > 1) {
            Vec3f g;
            MR::calcGravityVector(this, startPos, &g, nullptr, 0);
            g.scale(-100.0f);

            Vec3f v16;
            v16.set(startPos);

            for (s32 i = 1; i < mStackHeight; i++) {
                v16.add(g);
                mMovingPoints[v6++].mPosition.set(v16);
            }

            MR::calcGravityVector(this, endPos, &g, nullptr, 0);
            g.scale(-100.0f);
            v16.set(endPos);

            for (s32 i = 1; i < mStackHeight; i++) {
                v16.add(g);
                mMovingPoints[v6++].mPosition.set(v16);
            }
        }
    }

    updatePointPos();
    s32 v14 = 0;
    s32 v15 = 0;

    while (v14 < mPointNum) {
        if ((mStackHeight != 0 ? v14 % (2 * mStackHeight) : v14) >= 2) {
            mMovingPoints[v15].mHasShadow = 0;
        }

        mMovingPoints[v15].initWithoutIter();
        v14++;
        v15++;
    }
}

void ElectricRailMoving::initMesh() {
    mMesh.init(mRailPosNum * 4 * mStackHeight);
    drawPlane(30.0f, 30.0f, -30.0f, -30.0f);
    drawPlane(-30.0f, 30.0f, 30.0f, -30.0f);
    mMesh.send();
}

void ElectricRailMoving::drawRail(TypedDraw& rDraw, f32 a1) const {
    drawPlaneGX(rDraw, a1, a1, -a1, -a1);
    drawPlaneGX(rDraw, -a1, a1, a1, -a1);
}

void ElectricRailMoving::drawPlane(f32 a2, f32 a3, f32 a4, f32 a5) {
    for (s32 i = 0; i < mStackHeight; i++) {
        VertexPT* vtx = mMesh.strip((2 * mRailPosNum) & 0xFFFE);

        for (s32 j = 0; j < mRailPosNum; j++) {
            setVertexAttribute(j, i, a2, a3, a4, a5, vtx);
        }
    }
}

void ElectricRailMoving::drawPlaneGX(TypedDraw& rDraw, f32 a1, f32 a2, f32 a3, f32 a4) const {
    for (s32 i = 0; i < mStackHeight; i++) {
        VertexPT* vtx = rDraw.vertices< VertexPT >(NEBULA_PRIMITIVE_TRIANGLE_STRIP, mRailPosNum * 2);

        for (s32 j = 0; j < mRailPosNum; j++) {
            setVertexAttribute(j, i, a1, a2, a3, a4, vtx);
        }
    }
}

void ElectricRailMoving::setVertexAttribute(int a2, int a3, f32 a4, f32 a5, f32 a6, f32 a7, VertexPT*& rVtx) const {
    f32 railLength = MR::getRailTotalLength(this);

    if (100.0f * a2 < railLength) {
        railLength = 100.0f * a2;
    }

    f32 texCoord = 0.25f * railLength / 100.0f;
    Vec3f position;
    position.set(mRailPositions[a2]);
    Vec3f railDirection;
    Vec3f gravity;

    MR::calcRailDirectionAtCoord(&railDirection, this, railLength);
    MR::calcGravityVector(this, position, &gravity, 0, 0);

    gravity.negate();

    if (a3 > 0) {
        f32 height = 100.0f * a3;
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

    const f32 x0 = position.x + axis1.x * a4 + axis2.x * a5;
    const f32 y0 = position.y + axis1.y * a4 + axis2.y * a5;
    const f32 z0 = position.z + axis1.z * a4 + axis2.z * a5;
    const f32 x1 = position.x + axis1.x * a6 + axis2.x * a7;
    const f32 y1 = position.y + axis1.y * a6 + axis2.y * a7;
    const f32 z1 = position.z + axis1.z * a6 + axis2.z * a7;
    *rVtx++ = {x0, y0, z0, texCoord, 0.0f};
    *rVtx++ = {x1, y1, z1, texCoord, 1.0f};
}

void ElectricRailMoving::updateHitSensorPos() {
    f32 nearestRailPos = MR::calcNearestRailPos(mSensorOffsets, this, *MR::getPlayerCenterPos());

    if (mStackHeight > 1) {
        Vec3f v9;
        MR::calcGravityVector(this, mSensorOffsets[0], &v9, nullptr, 0);
        v9 *= -100.0f;


        for (s32 i = 1; i < mStackHeight; i++) {
            mSensorOffsets[i].add(mSensorOffsets[i - 1], v9);
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

namespace {
    inline bool updateMovingPoints(ElectricRailMoving* pActor) {
        s32 i;
        f32 railTotalLength = MR::getRailTotalLength(pActor);
        s32 segmentNum = pActor->mSegmentNum;

        f32 railCoord;
        f32 segmentLength;
        s32 pointIndex;
        bool reachedEnd;
        segmentLength = railTotalLength / segmentNum;
        reachedEnd = false;
        pointIndex = 0;
        railCoord = pActor->mCoord;

        Vec3f stackPosition;
        Vec3f gravity;
        Vec3f previousPosition;
        Vec3f position;

        for (i = 0; i < pActor->mSegmentNum; i++) {
            f32 coord = pActor->getRepeatedCoord(railCoord);

            pActor->calcPointPos(&position, coord);
            f32 previousCoord = pActor->getRepeatedCoord(railCoord - pActor->mSegmentLength);

            pActor->calcPointPos(&previousPosition, previousCoord);

            pActor->mMovingPoints[pointIndex++].mPosition.set(position);
            pActor->mMovingPoints[pointIndex++].mPosition.set(previousPosition);

            if (pActor->mStackHeight > 1) {
                MR::calcGravityVector(pActor, position, &gravity, nullptr, 0);
                gravity *= -100.0f;

                stackPosition.set(position);

                for (s32 j = 1; j < pActor->mStackHeight; j++) {
                    stackPosition += gravity;
                    pActor->mMovingPoints[pointIndex++].mPosition.set(stackPosition);
                }

                MR::calcGravityVector(pActor, previousPosition, &gravity, nullptr, 0);
                gravity *= -100.0f;

                stackPosition.set(previousPosition);

                for (s32 j = 1; j < pActor->mStackHeight; j++) {
                    stackPosition += gravity;
                    pActor->mMovingPoints[pointIndex++].mPosition.set(stackPosition);
                }
            }

            if (!MR::isLoopRail(pActor) &&
                (pActor->getRepeatedCoord(railCoord + pActor->mMovementSpeed) - (pActor->mSegmentLength + (3.0f * pActor->mMovementSpeed))) < 0.0f) {
                reachedEnd = true;
            }

            railCoord = pActor->getRepeatedCoord(railCoord + segmentLength);
        }

        return reachedEnd;
    }
};  // namespace

bool ElectricRailMoving::updatePointPos() {
    return ::updateMovingPoints(this);
}

void ElectricRailMoving::updatePointPosAndModel() {
    bool updated = updatePointPos();

    if (!MR::isLoopRail(this)) {
        if (updated) {
            for (s32 i = 0; i < 2 * mStackHeight; i++) {
                MR::showModelIfHidden(&mMovingPoints[mPointNum - 1 - i]);
            }
        } else {
            for (s32 i = 0; i < 2 * mStackHeight; i++) {
                MR::hideModelIfShown(&mMovingPoints[mPointNum - 1 - i]);
            }
        }
    }
}

bool ElectricRailMoving::isValidCoord(f32 coord) const {
    f32 repeated = getRepeatedCoord(coord - mCoord);
    f32 interval = MR::getRailTotalLength(this) / mSegmentNum;
    f32 offset = MR::mod(repeated, interval);
    return interval - mSegmentLength < offset;
}

f32 ElectricRailMoving::getRepeatedCoord(f32 coord) const {
    f32 len = MR::getRailTotalLength(this);
    return MR::repeat(coord, 0.0f, len);
}

void ElectricRailMoving::calcPointPos(Vec3f* pOutPos, f32 c) const {
    s32 v5 = (c / 100.0f);
    f32 v6 = MR::mod(c, 100.0f);
    f32 v7 = 100.0f;

    if ((mRailPosNum - 3) < v5) {
        v7 = (MR::getRailTotalLength(this) - (100.0f * v5));
    }

    pOutPos->lerp(mRailPositions[v5], mRailPositions[v5 + 1], (v6 / v7));
}

void ElectricRailMoving::move() {
    mCoord = getRepeatedCoord(mCoord + mMovementSpeed);

    f32 scale = (100.0f * mSegmentNum) / (0.25f * MR::getRailTotalLength(this));

    mTexMtx.identity();

    mTexMtx.mMtx[0][0] = scale;
    mTexMtx.mMtx[0][3] = -((0.25f * (scale * mCoord)) / 100.0f);
    f32 railTotalLength2 = MR::getRailTotalLength(this);
    f32 segmentLength = railTotalLength2 / mSegmentNum;
    f32 angle = (-TWO_PI * mSegmentLength) / segmentLength;
    f32 sinValue;
    sinValue = Angle::sinRadian(angle);
    f32 value = (15.0f * sinValue) + ((255.0f * mSegmentLength) / segmentLength);

    s32 c;
    if (static_cast< s32 >(value) < 0) {
        c = 0;
    } else if (static_cast< s32 >(value) > 255) {
        c = 255;
    } else {
        c = static_cast< s32 >(value);
    }

    mAlpha = c;

    updatePointPosAndModel();
}

void ElectricRailMoving::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_ELEC_RAIL_VANISH");
        MR::invalidateHitSensors(this);

        for (s32 i = 0; i < mPointNum; i++) {
            mMovingPoints[i].mIsElectric = 0;
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

ElectricRailMoving::~ElectricRailMoving() {
}

ElectricRailMovingPoint::~ElectricRailMovingPoint() {
}

