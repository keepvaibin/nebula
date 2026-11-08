#include "Game/MapObj/BallRail.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"

namespace NrvBallRail {
    NEW_NERVE(BallRailNrvWait, BallRail, Wait);
    NEW_NERVE(BallRailNrvSetUp, BallRail, SetUp);
    NEW_NERVE(BallRailNrvRun, BallRail, Run);
    NEW_NERVE(BallRailNrvNoBind, BallRail, NoBind);
};  // namespace NrvBallRail

BallRail::BallRail(const char* pName)
    : LiveActor(pName), mRailPoints(), mBindSensor(), mNumPoints(), mAcceleration(1.5f), mDeceleration(0.995f), mBindSensorRadius(100.0f) {
}

void BallRail::init(const TableIter& rIter) {
    if (MR::getPlacementArg0NoInit(rIter, &mAcceleration)) {
        mAcceleration /= 1000.0f;
    }

    if (MR::getPlacementArg1NoInit(rIter, &mDeceleration)) {
        mDeceleration /= 1000.0f;
    }

    MR::connectToSceneMapObjMovement(this);
    MR::invalidateClipping(this);
    MR::onCalcGravity(this);
    initRailRider(rIter);
    initRailPoints();
    MR::moveCoordToStartPos(this);
    MR::setRailCoordSpeed(this, 0.0f);
    initHitSensor(1);
    Vec3f offs;
    offs.y = mBindSensorRadius;
    offs.x = 0.0f;
    offs.z = 0.0f;
    MR::addHitSensor(this, "bind", ATYPE_BALL_RAIL, 8, mBindSensorRadius, offs);
    initNerve(GET_NERVE(BallRail, BallRailNrvWait));
    appear();
}

void BallRail::control() {
}

bool BallRail::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (msg == ACTMES_SPHERE_PLAYER_BINDED && isNerve(GET_NERVE(BallRail, BallRailNrvWait))) {
        Vec3f v9(pSender->mHost->mVelocity);
        Vec3f v8 = pSender->mPosition - pReceiver->mPosition;

        if (v8.dot(v9) > 0.0f) {
            return false;
        }

        if (mGravity.dot(v8) > 0.0f) {
            MR::normalizeOrZero(&v8);
            v8.dot(v9);
            MR::zeroVelocity(pSender->mHost);

            return false;
        } else {
            mBindSensor = pSender;

            return true;
        }
    }

    return false;
}

namespace {
    inline void initPointSide(BallRailPoint* pPoint, const Vec3f& rUp) {
        pPoint->mSide.cross(pPoint->mDir, rUp);
        MR::normalizeOrZero(&pPoint->mSide);
    }
};  // namespace

void BallRail::initRailPoints() {
    u32 numPoints = (u32)(MR::getRailTotalLength(this) / 100.0f) + 2;
    mNumPoints = numPoints;
    mRailPoints = new BallRailPoint[numPoints];
    MR::moveCoordToStartPos(this);
    MR::setRailCoordSpeed(this, 100.0f);

    for (u32 i = 0; i < mNumPoints - 1; i++) {
        mRailPoints[i].mPos = MR::getRailPos(this);
        mRailPoints[i].mDir = MR::getRailDirection(this);
        MR::moveRailRider(this);
    }

    MR::moveCoordToEndPos(this);
    mRailPoints[mNumPoints - 1].mPos = MR::getRailPos(this);
    mRailPoints[mNumPoints - 1].mDir = MR::getRailDirection(this);
    MR::moveCoordToStartPos(this);
    MR::setRailCoordSpeed(this, 0.0f);

    BallRailPoint* pLastPoint;
    BallRailPoint* pPoint;
    Vec3f up(0, 1, 0);

    if (mNumPoints >= 2U) {
        ::initPointSide(mRailPoints, up);
    }

    for (u32 i = 0; i < mNumPoints; i++) {
        pPoint = &mRailPoints[i];
        pPoint->mSide.cross(pPoint->mDir, up);
        MR::normalizeOrZero(&pPoint->mSide);
    }

    if (mNumPoints >= 2U) {
        pLastPoint = &mRailPoints[mNumPoints - 1];
        pLastPoint->mSide.cross(pLastPoint->mDir, up);
        MR::normalizeOrZero(&pLastPoint->mSide);
    }
}

void BallRail::exeWait() {
    MR::moveCoordToNearestPos(this, *MR::getPlayerPos());
    MR::moveTransToCurrentRailPos(this);

    if (mBindSensor != nullptr) {
        setNerve(GET_NERVE(BallRail, BallRailNrvSetUp));
    }
}

void BallRail::exeSetUp() {
    if (MR::isFirstStep(this)) {
        mBindStartPos = mBindSensor->mPosition;
        MR::zeroVelocity(mBindSensor->mHost);
        MR::setRailCoordSpeed(this, 0.0f);
    }

    MR::moveCoord(this, 3.0f);
    MR::moveTransToCurrentRailPos(this);

    Vec3f v7;

    if (MR::isLessStep(this, 15)) {
        Vec3f v6;
        v6.scaleAdd(-mBindSensor->mRadius, mGravity, mPosition);
        f32 rate = MR::calcNerveEaseInRate(this, 15);
        MR::vecBlend(mBindStartPos, v6, &v7, rate);
    } else {
        v7.scaleAdd(-mBindSensor->mRadius, mGravity, mPosition);
    }

    if (MR::isStep(this, 15)) {
        MR::tryRumblePadStrong(this, PLAYER_1);
    }

    Vec3f& rVelocity = mBindSensor->mHost->mVelocity;
    rVelocity.set(v7 - mBindSensor->mPosition);

    if (MR::isGreaterStep(this, 45)) {
        setNerve(GET_NERVE(BallRail, BallRailNrvRun));
    }
}

void BallRail::exeRun() {
    if (MR::isFirstStep(this)) {
        MR::setRailCoordSpeed(this, 0.0f);
    }

    Vec3f v14 = MR::getRailDirection(this);

    Vec3f acceleration = mGravity * mAcceleration;
    MR::accelerateRailCoordSpeed(this, acceleration.dot(v14));
    Vec3f v12;
    MR::slowDownRailCoordSpeed(this, mDeceleration);

    if (!MR::isRailGoingToEnd(this) || MR::getRailCoordSpeed(this) < 6.0f) {
        if (!MR::isRailGoingToEnd(this)) {
            MR::setRailDirectionToEnd(this);
        }

        MR::setRailCoordSpeed(this, 6.0f);
    }

    MR::moveRailRider(this);
    MR::moveTransToCurrentRailPos(this);
    v12.scaleAdd(-mBindSensor->mRadius, mGravity, mPosition);
    Vec3f& rVelocity = mBindSensor->mHost->mVelocity;
    rVelocity.set(v12 - mBindSensor->mPosition);

    if (MR::isRailReachedGoal(this)) {
        Vec3f& rVelocity = mBindSensor->mHost->mVelocity;
        rVelocity.set(MR::getRailDirection(this) * MR::getRailCoordSpeed(this));
        mBindSensor->receiveMessage(ACTMES_END_BALL_RAIL, getSensor("bind"));
        mBindSensor = nullptr;
        setNerve(GET_NERVE(BallRail, BallRailNrvNoBind));
    }
}

void BallRail::exeNoBind() {
    if (getNerveStep() > 60) {
        setNerve(GET_NERVE(BallRail, BallRailNrvWait));
    }
}

BallRailPoint::BallRailPoint() : mPos(0, 0, 0), mSide(1, 0, 0), mDir(0, 0, 1) {
}

BallRail::~BallRail() {
}
