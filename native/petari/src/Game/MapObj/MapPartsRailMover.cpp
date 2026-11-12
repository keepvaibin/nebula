#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapParts.hpp"
#include "Game/MapObj/MapPartsFunction.hpp"
#include "Game/MapObj/MapPartsRailPointPassChecker.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvMapPartsRailMover {
    NEW_NERVE(HostTypeWait, MapPartsRailMover, Wait);
    NEW_NERVE(HostTypeMove, MapPartsRailMover, Move);
    NEW_NERVE(HostTypeMoveStart, MapPartsRailMover, MoveStart);
    NEW_NERVE(HostTypeStopAtPointBeforeRotate, MapPartsRailMover, StopAtPoint);
    NEW_NERVE(HostTypeStopAtPointAfterRotate, MapPartsRailMover, StopAtPoint);
    NEW_NERVE(HostTypeStopAtEndBeforeRotate, MapPartsRailMover, StopAtEnd);
    NEW_NERVE(HostTypeStopAtEndAfterRotate, MapPartsRailMover, StopAtEnd);
    NEW_NERVE(HostTypeStopAtEndWithPlayerOn, MapPartsRailMover, StopAtEndWithPlayerOn);
    NEW_NERVE(HostTypeWaitForRestartByPlayerOn, MapPartsRailMover, WaitForRestartByPlayerOn);
    NEW_NERVE(HostTypeRotateAtPoint, MapPartsRailMover, RotateAtPoint);
    NEW_NERVE(HostTypeRotateAtEndPoint, MapPartsRailMover, RotateAtEndPoint);
    NEW_NERVE(HostTypeVanish, MapPartsRailMover, Vanish);
};  // namespace NrvMapPartsRailMover

MapPartsRailMover::MapPartsRailMover(LiveActor* pActor)
    : MapPartsFunction(pActor, "レイル移動"), mRailPointPassChecker(), mMoveConditionType(), mMoveStopType(1), mSignMotionType(), mStartPos(gZeroVec) {
    lumacore::setClass(this, lumacore::kClassId< MapPartsRailMover >);
    mInitCoord = 0.0f;
    mStopTime = 0;
    mSpeed = 0.0f;
    mAccelTime = 0;
    mAcceleration = 0.0f;
    mSignCoord = 0.0f;
}

void MapPartsRailMover::init(const TableIter& rIter) {
    MR::getMapPartsArgMoveConditionType(&mMoveConditionType, rIter);
    MR::getMapPartsArgMoveStopType(&mMoveStopType, mHost);
    MR::getMapPartsArgSignMotionType(&mSignMotionType, rIter);

    s32 initPostType = 0;
    MR::getMapPartsArgRailInitPosType(&initPostType, mHost);

    if (MR::isMapPartsRailInitPosTypeRailPos(initPostType)) {
        MR::moveCoordAndTransToNearestRailPos(mHost);
    } else if (MR::isMapPartsRailInitPosTypeRailPoint(initPostType)) {
        MR::moveCoordAndTransToNearestRailPoint(mHost);
    } else if (MR::isMapPartsRailInitPosTypePoint0(initPostType)) {
        MR::moveCoordAndTransToRailStartPoint(mHost);
    }

    mStartPos.set(mHost->mPosition);
    mInitCoord = MR::getRailCoord(mHost);
    mRailPointPassChecker = new MapPartsRailPointPassChecker(mHost);
    mRailPointPassChecker->init(rIter);
    initNerve(GET_NERVE(MapPartsRailMover, HostTypeWait));
}

void MapPartsRailMover::movement() {
    bool isMove = false;

    if (isNerve(GET_NERVE(MapPartsRailMover, HostTypeMove)) || isNerve(GET_NERVE(MapPartsRailMover, HostTypeMoveStart))) {
        isMove = true;
    }

    if (isMove) {
        tryPassPoint();
    } else {
        tryRestartAtEnd();
    }

    mRailPointPassChecker->movement();
    MapPartsFunction::movement();
}

void MapPartsRailMover::start() {
    moveToInitPos();
    mRailPointPassChecker->start();

    if (MR::hasMapPartsMoveStartSignMotion(mSignMotionType)) {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeMoveStart));
    } else {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeMove));
    }
}

void MapPartsRailMover::end() {
    mRailPointPassChecker->end();
    setNerve(GET_NERVE(MapPartsRailMover, HostTypeWait));
}

bool MapPartsRailMover::isWorking() const {
    bool isWork = false;

    if (isNerve(GET_NERVE(MapPartsRailMover, HostTypeMove)) || isNerve(GET_NERVE(MapPartsRailMover, HostTypeMoveStart))) {
        isWork = true;
    }

    return isWork;
}

bool MapPartsRailMover::receiveMsg(u32 msg) {
    if (msg == ACTMES_MAPPARTS_ON_PLAYER && isNerve(GET_NERVE(MapPartsRailMover, HostTypeWaitForRestartByPlayerOn))) {
        restartAtEnd();
        return true;
    }

    return false;
}

void MapPartsRailMover::moveToInitPos() {
    if (!MR::isNearZero(mInitCoord - MR::getRailCoord(mHost))) {
        MR::setRailCoord(mHost, mInitCoord);
        mStartPos.set(MR::getRailPos(mHost));
        if (!MR::isRailGoingToEnd(mHost)) {
            MR::reverseRailDirection(mHost);
        }
    }
}

void MapPartsRailMover::startWithSignalMotion() {
    setNerve(GET_NERVE(MapPartsRailMover, HostTypeMoveStart));
}

void MapPartsRailMover::cancelSignalMotion() {
    MR::setRailCoord(mHost, mSignCoord);
    mStartPos.set(MR::getRailPos(mHost));
    setNerve(GET_NERVE(MapPartsRailMover, HostTypeWait));
}

bool MapPartsRailMover::tryResetPositionRepeat() {
    if (mMoveStopType != 2) {
        return false;
    }

    if (!isNerve(GET_NERVE(MapPartsRailMover, HostTypeMove))) {
        return false;
    }

    if (getStep() != 1) {
        return false;
    }

    MR::resetPosition(mHost);
    return true;
}

void MapPartsRailMover::resetToInitPos() {
    moveToInitPos();
    MR::resetPosition(mHost, mStartPos);
    setNerve(GET_NERVE(MapPartsRailMover, HostTypeWait));
}

void MapPartsRailMover::passPoint() {
    mStopTime = 0;
    MR::getMapPartsArgStopTime(&mStopTime, mHost);

    if (mStopTime > 0) {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeStopAtPointBeforeRotate));

    } else {
        if (!sendMsgToHost(ACTMES_MAPPARTS_START_ROTATE_BETWEEN_POINTS)) {
            if (sendMsgToHost(ACTMES_MAPPARTS_START_ROTATE_AT_POINT)) {
                setNerve(GET_NERVE(MapPartsRailMover, HostTypeRotateAtPoint));
            } else {
                setNerve(GET_NERVE(MapPartsRailMover, HostTypeMove));
            }
        }
    }
}

void MapPartsRailMover::reachedEnd() {
    if (mMoveStopType == 1) {
        MR::reverseRailDirection(mHost);
    }

    if (MR::isMoveStartTypePlayerOnStopEnd(mMoveConditionType)) {
        reachedEndPlayerOn();
    } else {
        setStateStopAtEndBeforeRotate();
    }
}

void MapPartsRailMover::reachedEndPlayerOn() {
    if (MR::isOnPlayer(MR::getBodySensor(mHost))) {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeStopAtEndWithPlayerOn));
    } else {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeWaitForRestartByPlayerOn));
    }
}

bool MapPartsRailMover::isReachedEnd() const {
    return (isNerve(GET_NERVE(MapPartsRailMover, HostTypeMove)) || isNerve(GET_NERVE(MapPartsRailMover, HostTypeMoveStart))) &&
           mRailPointPassChecker->isReachedEnd();
}

bool MapPartsRailMover::isDone() const {
    if (mMoveStopType == 0 || mMoveStopType == 3) {
        return mRailPointPassChecker->isReachedEnd();
    }

    return false;
}

void MapPartsRailMover::setStateStopAtEndBeforeRotate() {
    mSpeed = 0.0f;
    mAcceleration = 0.0f;
    MR::setRailCoordSpeed(mHost, 0.0f);
    mStopTime = 0;
    MR::getMapPartsArgStopTime(&mStopTime, mHost);

    if (mStopTime <= 0) {
        if (sendMsgToHost(ACTMES_MAPPARTS_START_ROTATE_AT_POINT)) {
            setNerve(GET_NERVE(MapPartsRailMover, HostTypeRotateAtEndPoint));
        } else {
            restartAtEnd();
        }
    } else {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeStopAtEndBeforeRotate));
    }
}

void MapPartsRailMover::calcTimeToNextRailPoint(f32* pTime) const {
    f32 distance = 0.0f;
    MR::calcDistanceToNextRailPoint(mHost, &distance);

    if (MR::isNearZero(mAcceleration, 0.0001f)) {
        *pTime = MR::abs(distance / mSpeed);
        return;
    }

    f32 accelDistance = mSpeed * mAccelTime + 0.5f * mAcceleration * mAccelTime * mAccelTime;

    if (accelDistance < distance) {
        *pTime = mAccelTime + (distance - accelDistance) / (mAcceleration * mAccelTime);
    } else {
        f32 sqrtDiscriminant = MR::sqrt(mSpeed * mSpeed + 2.0f * mAcceleration * distance);

        f32 t0 = (-mSpeed + sqrtDiscriminant) / mAcceleration;
        f32 t1 = (-mSpeed - sqrtDiscriminant) / mAcceleration;

        *pTime = MR::max(t0, t1);
    }
}

void MapPartsRailMover::endRotateAtPoint() {
    if (isNerve(GET_NERVE(MapPartsRailMover, HostTypeRotateAtPoint))) {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeStopAtPointAfterRotate));
    } else if (isNerve(GET_NERVE(MapPartsRailMover, HostTypeRotateAtEndPoint))) {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeStopAtEndAfterRotate));
    }
}

void MapPartsRailMover::calcMoveSpeed(f32* pMoveSpeed) const {
    if (!MR::isNearZero(mAcceleration, 0.0001f)) {
        *pMoveSpeed = MR::getRailCoordSpeed(mHost);
    } else {
        s32 calcType = 0;
        MR::getMapPartsArgSpeedCalcType(&calcType, mHost);

        if (MR::isMapPartsRailSpeedCalcTypeTime(calcType)) {
            calcMoveSpeedTime(pMoveSpeed);
        } else {
            calcMoveSpeedDirect(pMoveSpeed);
        }
    }
}

void MapPartsRailMover::calcMoveSpeedDirect(f32* pSpeed) const {
    f32 speed = -1.0f;
    MR::getMapPartsArgMoveSpeed(&speed, mHost);

    if (speed < 0.0f) {
        return;
    }

    *pSpeed = speed;
}

void MapPartsRailMover::calcMoveSpeedTime(f32* pTime) const {
    s32 moveTime = -1;
    MR::getMapPartsArgMoveTimeToNextPoint(&moveTime, mHost);

    // A time of 0 steps crosses the whole part in one step (it divided by 0).
    if (moveTime >= 0) {
        s32 curPnt = MR::getCurrentRailPointNo(mHost);

        if (!MR::isRailGoingToEnd(mHost)) {
            curPnt--;
        }

        *pTime = MR::getRailPartLength(mHost, curPnt) / (moveTime > 0 ? moveTime : 1);
    }
}

void MapPartsRailMover::updateAccel() {
    mAccelTime = 0;
    MR::getMapPartsArgAccelTime(&mAccelTime, mHost);

    if (mAccelTime <= 0) {
        mAcceleration = 0.0f;
    } else {
        f32 moveSpeed = -1.0f;
        MR::getMapPartsArgMoveSpeed(&moveSpeed, mHost);
        if (moveSpeed < 0.0f) {
            mAcceleration = 0.0f;
        } else {
            mAcceleration = (moveSpeed - mSpeed) / mAccelTime;
        }
    }
}

bool MapPartsRailMover::tryPassPoint() {
    if (mRailPointPassChecker->isReachedEnd()) {
        if (mMoveStopType == 0) {
            setNerve(GET_NERVE(MapPartsRailMover, HostTypeWait));
            return true;
        } else {
            reachedEnd();
            return true;
        }
    } else if (mRailPointPassChecker->isPassed()) {
        passPoint();
        return true;
    }

    return false;
}

bool MapPartsRailMover::tryRestartAtEnd() {
    bool isMove = false;

    if (isNerve(GET_NERVE(MapPartsRailMover, HostTypeMove)) || isNerve(GET_NERVE(MapPartsRailMover, HostTypeMoveStart))) {
        isMove = true;
    }

    if (isMove) {
        return false;
    }

    if (!MR::isMoveStartTypePlayerOnStopEnd(mMoveConditionType)) {
        return false;
    }

    if (!MR::isOnPlayer(MR::getBodySensor(mHost))) {
        return false;
    }

    if (!isNerve(GET_NERVE(MapPartsRailMover, HostTypeWaitForRestartByPlayerOn))) {
        return false;
    }

    restartAtEnd();
    return true;
}

void MapPartsRailMover::restartAtEnd() {
    if (mMoveStopType == 0) {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeWait));
    } else if (mMoveStopType == 1) {
        if (MR::hasMapPartsMoveStartSignMotion(mSignMotionType)) {
            setNerve(GET_NERVE(MapPartsRailMover, HostTypeMoveStart));
        } else {
            setNerve(GET_NERVE(MapPartsRailMover, HostTypeMove));
        }
    } else if (mMoveStopType == 2) {
        MR::moveCoordToStartPos(mHost);
        mStartPos.set(MR::getRailPos(mHost));
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeMove));
    } else if (mMoveStopType == 3) {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeVanish));
    }
}

void MapPartsRailMover::exeMove() {
    if (isFirstStep()) {
        updateAccel();
        calcMoveSpeed(&mSpeed);
        sendMsgToHost(ACTMES_MAPPARTS_START_ROTATE_BETWEEN_POINTS);
    }

    bool isNotNear = !MR::isNearZero(mAcceleration, 0.0001f);

    if (isNotNear && getStep() < mAccelTime) {
        mSpeed += mAcceleration;
    }

    MR::moveCoord(mHost, mSpeed);
    mStartPos.set(MR::getRailPos(mHost));
}

void MapPartsRailMover::exeMoveStart() {
    if (isFirstStep()) {
        mSignCoord = MR::getRailCoord(mHost);

        if (MR::isExistCollisionParts(mHost)) {
            MR::offUpdateCollisionParts(mHost);
        }
    }

    // The start signal: shake along the rail, 3 steps each way.
    f32 shakeSign;

    if ((getStep() / 3) % 2 == 0) {
        shakeSign = 1.0f;
    } else {
        shakeSign = -1.0f;
    }

    if (MR::isRailGoingToEnd(mHost)) {
        shakeSign *= -1.0f;
    }

    MR::setRailCoord(mHost, mSignCoord + (7.0f * (shakeSign * ((getStep() % 3)))));
    mStartPos.set(MR::getRailPos(mHost));
    if (isStep(MapParts::getMoveStartSignalTime())) {
        MR::setRailCoord(mHost, mSignCoord);

        if (MR::isExistCollisionParts(mHost)) {
            MR::onUpdateCollisionParts(mHost);
        }

        mStartPos.set(MR::getRailPos(mHost));
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeMove));
    }
}

void MapPartsRailMover::exeStopAtPoint() {
    if (isFirstStep()) {
        mSpeed = 0.0f;
        mAcceleration = 0.0f;
        MR::setRailCoordSpeed(mHost, 0.0f);
    }

    if (isStep(mStopTime)) {
        if (isNerve(GET_NERVE(MapPartsRailMover, HostTypeStopAtPointBeforeRotate))) {
            if (sendMsgToHost(ACTMES_MAPPARTS_START_ROTATE_AT_POINT)) {
                setNerve(GET_NERVE(MapPartsRailMover, HostTypeRotateAtPoint));
            } else {
                setNerve(GET_NERVE(MapPartsRailMover, HostTypeStopAtPointAfterRotate));
            }
        } else {
            setNerve(GET_NERVE(MapPartsRailMover, HostTypeMove));
        }
    }
}

void MapPartsRailMover::exeStopAtEnd() {
    if (!tryRestartAtEnd() && isStep(mStopTime)) {
        if (isNerve(GET_NERVE(MapPartsRailMover, HostTypeStopAtEndBeforeRotate))) {
            if (sendMsgToHost(ACTMES_MAPPARTS_START_ROTATE_AT_POINT)) {
                setNerve(GET_NERVE(MapPartsRailMover, HostTypeRotateAtEndPoint));
            } else {
                setNerve(GET_NERVE(MapPartsRailMover, HostTypeStopAtEndAfterRotate));
            }

        } else {
            restartAtEnd();
        }
    }
}

MapPartsRailMover::~MapPartsRailMover() {
    return;
}

void MapPartsRailMover::exeVanish() {
    if (MapPartsFunction::isFirstStep()) {
        MapPartsFunction::sendMsgToHost(ACTMES_MAPPARTS_DISAPPEAR_WITH_BLINK);
    }
}

void MapPartsRailMover::exeStopAtEndWithPlayerOn() {
    if (!MR::isOnPlayer(MR::getBodySensor(mHost))) {
        setNerve(GET_NERVE(MapPartsRailMover, HostTypeWaitForRestartByPlayerOn));
    }
}

void MapPartsRailMover::exeRotateAtEndPoint() {
    return;
}

void MapPartsRailMover::exeRotateAtPoint() {
    return;
}

void MapPartsRailMover::exeWaitForRestartByPlayerOn() {
    return;
}

void MapPartsRailMover::exeWait() {
    return;
}
