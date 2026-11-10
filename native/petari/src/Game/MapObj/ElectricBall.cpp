#include "Game/MapObj/ElectricBall.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sMoveRadiusDefault, 750.0f);
    NEBULA_TUNABLE(f32, sMoveAngularSpeedDefault, 0.01f);
    NEBULA_TUNABLE(s32, sNumBallsDefault, 4);
    const s32 sAttackTime = NebulaFrameClock::stepsOfMilliseconds(1500);
    NEBULA_TUNABLE(f32, sSensorRadius, 75.0f);
    NEBULA_TUNABLE(f32, sShadowRadius, 75.0f);
    NEBULA_TUNABLE(s32, sNumBallsMax, 16);
};  // namespace

namespace NrvElectricBall {
    NEW_NERVE(HostTypeWait, ElectricBall, Wait);
    NEW_NERVE(HostTypeMove, ElectricBall, Move);
    NEW_NERVE(HostTypeAttack, ElectricBall, Attack);
};  // namespace NrvElectricBall

ElectricBall::Ball::Ball() : mHost(), mPosition(gZeroVec), mIsActive() {
}

void ElectricBall::Ball::init(LiveActor* pActor) {
    mHost = new PartsModel(pActor, "ビリビリボール", "ElectricBall", nullptr, -1, false);
    mHost->initWithoutIter();
    mHost->makeActorAppeared();

    MR::initShadowVolumeSphere(mHost, ::sShadowRadius);

    MR::onCalcGravity(mHost);
}

void ElectricBall::Ball::updatePosition(const Pos3f& rPos) {
    rPos.mult(mPosition, mHost->mPosition);
}

void ElectricBall::Ball::activate() {
    mIsActive = true;
    MR::emitEffect(mHost, "Electric");
}

ElectricBall::ElectricBall(const char* pName)
    : LiveActor(pName), mBalls(), mClosestBallPos(gZeroVec), mRadius(::sMoveRadiusDefault), mAngularSpeed(::sMoveAngularSpeedDefault), mRailMover() {
    lumacore::setClass(this, lumacore::kClassId< ElectricBall >);
    mBaseMtx.identity();
}

void ElectricBall::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::makeMtxTR(mBaseMtx, this);

    MR::getPlacementArg1NoInit(rIter, &mRadius);
    MR::getPlacementArg2NoInit(rIter, &mAngularSpeed);

    mAngularSpeed *= ::sMoveAngularSpeedDefault;

    MR::connectToSceneMapObjMovement(this);

    initHitSensor(1);
    MR::addHitSensorPosMapObj(this, "body", 8, ::sSensorRadius, &mClosestBallPos, Vec3f(0.0f, 0.0f, 0.0f));

    initSound(4, false);
    initBalls(rIter);

    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);

        mRailMover = new MapPartsRailMover(this);
        mRailMover->init(rIter);
    }

    MR::setClippingTypeSphere(this, mRadius + ::sSensorRadius);
    MR::setGroupClipping(this, rIter, ::sNumBallsMax);

    initNerve(GET_NERVE(ElectricBall, HostTypeWait));

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &ElectricBall::startMove));
    } else {
        startMove();
    }

    makeActorAppeared();
}

void ElectricBall::initAfterPlacement() {
    for (auto& item : mBalls) {
        item.updatePosition(mBaseMtx);
    }

    Ball* ball = getNearestBall();
    mClosestBallPos.set(ball->mHost->mPosition);
}

void ElectricBall::control() {
    if (isNerve(GET_NERVE(ElectricBall, HostTypeMove)) || isNerve(GET_NERVE(ElectricBall, HostTypeAttack))) {
        if (mRailMover != nullptr) {
            mRailMover->movement();

            if (mRailMover->isWorking()) {
                mPosition.set(mRailMover->mStartPos);
            }
        }

        Pos3f mtx;
        mtx.identity();
        mtx.setEulerY(mAngularSpeed * PI_180);
        mBaseMtx.concat(mBaseMtx, mtx);
        mBaseMtx.setTrans(mPosition);

        for (auto& item : mBalls) {
            item.updatePosition(mBaseMtx);
        }

        MR::startLevelSound(this, "SE_OJ_LV_BIRIKYU_MOVE");
    }

    Ball* pBall = getNearestBall();
    mClosestBallPos.set(pBall->mHost->mPosition);
}

void ElectricBall::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!MR::isSensorPlayerOrRide(pReceiver)) {
        return;
    }

    if (!isNerve(GET_NERVE(ElectricBall, HostTypeAttack)) && !isNerve(GET_NERVE(ElectricBall, HostTypeWait)) &&
        MR::sendMsgEnemyAttackElectric(pReceiver, pSender)) {
        setNerve(GET_NERVE(ElectricBall, HostTypeAttack));
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool ElectricBall::receiveMsgPlayerAttack(u32 msg, HitSensor*, HitSensor*) {
    return MR::isMsgStarPieceReflect(msg);
}

void ElectricBall::initBalls(const TableIter& rIter) {
    s32 numBalls = ::sNumBallsDefault;
    MR::getPlacementArg0NoInit(rIter, &numBalls);

    // At least one ball: the nearest ball is asked for every step.
    if (numBalls < 1) {
        numBalls = 1;
    }

    mBalls.init(numBalls);

    for (auto& item : mBalls) {
        item.init(this);
    }

    Vec3f vec(mRadius, 0.0f, 0.0f);
    f32 angleStep = TWO_PI / mBalls.size();

    for (Ball* ball = mBalls.begin(); ball != mBalls.end(); ball++) {
        f32 angle = angleStep * (ball - mBalls.begin());
        MR::rotateVecRadian(&ball->mPosition, vec, Vec3f(0.0f, 1.0f, 0.0f), angle);
    }
}

ElectricBall::Ball* ElectricBall::getNearestBall() {
    f32 minDistance = FLOAT_MAX;
    Ball* nearestBall = mBalls.begin();

    for (Ball* ball = mBalls.begin(); ball != mBalls.end(); ball++) {
        f32 distanceToPlayer = MR::calcDistanceToPlayer(ball->mHost->mPosition);

        if (distanceToPlayer < minDistance) {
            nearestBall = ball;
            minDistance = distanceToPlayer;
        }
    }

    return nearestBall;
}

void ElectricBall::startMove() {
    if (mRailMover != nullptr) {
        mRailMover->start();
    }

    for (auto& item : mBalls) {
        item.activate();
    }

    setNerve(GET_NERVE(ElectricBall, HostTypeMove));
}

void ElectricBall::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(getNearestBall()->mHost, "ElectricHit");
    }

    if (MR::isStep(this, ::sAttackTime)) {
        MR::emitEffect(getNearestBall()->mHost, "ElectricHit");
        setNerve(GET_NERVE(ElectricBall, HostTypeMove));
    }
}
