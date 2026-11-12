#include "Game/Enemy/Birikyu.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvBirikyu {
    NEW_NERVE(HostTypeMove, Birikyu, Move);
    NEW_NERVE(HostTypeMoveCircle, Birikyu, MoveCircle);
    NEW_NERVE(HostTypeAttack, Birikyu, Attack);
    NEW_NERVE(HostTypeAttackWait, Birikyu, AttackWait);
    NEW_NERVE(HostTypeWaitAtEdge, Birikyu, WaitAtEdge);
    NEW_NERVE(HostTypeStopPointing, Birikyu, StopPointing);
};  // namespace NrvBirikyu

Birikyu::Birikyu(const char* pName)
    : LiveActor(pName), mObjName(), mClipCenter(gZeroVec), mCircleCenter(gZeroVec), mHasShadow(), mHasRail(), mCircleAxis(0.0f, 1.0f, 0.0f), mCircleFront(0.0f, 0.0f, 1.0f), mCircleAngle(), mSpeed(10.0f) {
    lumacore::setClass(this, lumacore::kClassId< Birikyu >);
}

void Birikyu::init(const TableIter& rIter) {
    MR::getObjectName(&mObjName, rIter);
    initModelManagerWithAnm(mObjName, nullptr, false);
    MR::connectToSceneEnemy(this);
    initFromJmpArgs(rIter);
    initRail(rIter);
    MR::initDefaultPos(this, rIter);
    initCollision();
    initShadow();
    initEffectKeeper(0, nullptr);
    MR::setGroupClipping(this, rIter, 64);
    MR::joinToGroupArray(this, rIter, nullptr, 64);
    initSound(4, false);
    Vec3f offset;
    offset.x = 0.0f;
    offset.y = 0.0f;
    offset.z = 0.0f;
    MR::initStarPointerTarget(this, 100.0f, offset);

    if (mHasRail) {
        initNerve(GET_NERVE(Birikyu, HostTypeMove));
    } else {
        initNerve(GET_NERVE(Birikyu, HostTypeMoveCircle));
    }

    appear();
}

void Birikyu::initAfterPlacement() {
    if (mHasRail) {
        MR::moveCoordAndTransToNearestRailPos(this);
    } else {
        mCircleCenter.set(mPosition);
        Pos3f matrix;
        matrix.identity();
        MR::makeMtxRotate(matrix.toMtxPtr(), mRotation);
        matrix.getYDir(mCircleAxis);
        MR::normalize(&mCircleAxis);
        matrix.getZDir(mCircleFront);
        MR::normalize(&mCircleFront);
        mPosition.set(mCircleCenter + mCircleFront * 400.0f);
    }
}

void Birikyu::appear() {
    LiveActor::appear();
    MR::emitEffect(this, mObjName);
}

f32 Birikyu::getHitRadius() const {
    return 120.0f;
}

const char* Birikyu::getCenterJointName() const {
    return "Root";
}

void Birikyu::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayerOrRide(pReceiver) || MR::isSensorEnemy(pReceiver)) {
        if (MR::sendMsgEnemyAttackElectric(pReceiver, pSender)) {
            MR::sendMsgToGroupMember(ACTMES_GROUP_MOVE_STOP, this, getSensor("body"), "body");
            setNerve(GET_NERVE(Birikyu, HostTypeAttack));
        } else {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool Birikyu::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return MR::isMsgStarPieceReflect(msg);
}

bool Birikyu::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_GROUP_MOVE_STOP) {
        bool bool4 = isNerve(GET_NERVE(Birikyu, HostTypeMove)) || isNerve(GET_NERVE(Birikyu, HostTypeMoveCircle));

        if (bool4) {
            setNerve(GET_NERVE(Birikyu, HostTypeAttackWait));

            return true;
        }
    }

    if (msg == ACTMES_GROUP_MOVE_START) {
        bool bool5 = isNerve(GET_NERVE(Birikyu, HostTypeMove)) || isNerve(GET_NERVE(Birikyu, HostTypeMoveCircle));

        if (!bool5) {
            goMove();

            return true;
        }
    }

    return false;
}

void Birikyu::startClipped() {
    LiveActor::startClipped();
    MR::forceDeleteEffect(this, mObjName);
}

void Birikyu::endClipped() {
    LiveActor::endClipped();
    MR::emitEffect(this, mObjName);
}

void Birikyu::initFromJmpArgs(const TableIter& rIter) {
    MR::getPlacementArg0NoInit(rIter, &mSpeed);
    s32 num = -1;
    MR::getPlacementArg2NoInit(rIter, &num);
    mHasShadow = num == 1;
}

void Birikyu::initRail(const TableIter& rIter) {
    mHasRail = MR::isConnectedWithRail(rIter);
    if (mHasRail) {
        initRailRider(rIter);
        MR::initAndSetRailClipping(&mClipCenter, this, 100.0f, 500.0f);
    }
}

void Birikyu::initCollision() {
    initHitSensor(1);
    Vec3f offset(0.0f, 0.0f, 0.0f);
    f32 radius = getHitRadius();
    radius *= mScale.x;
    MR::addHitSensorAtJointEnemy(this, "body", getCenterJointName(), 16, radius, offset);
}

void Birikyu::initShadow() {
    if (mHasShadow) {
        f32 num = getHitRadius();
        MR::initShadowVolumeSphere(this, (0.8f * (num * mScale.x)));
        MR::onCalcShadowDropPrivateGravity(this, nullptr);
        MR::onCalcShadow(this, nullptr);
        MR::onCalcGravity(this);
        MR::setShadowDropPositionPtr(this, nullptr, &getSensor("body")->mPosition);
    }
}

bool Birikyu::tryStopPointing() {
    if (MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_MOVE_STOP, this, getSensor("body"), "body");
        setNerve(GET_NERVE(Birikyu, HostTypeStopPointing));

        return true;
    }

    return false;
}

void Birikyu::goMove() {
    if (mHasRail) {
        setNerve(GET_NERVE(Birikyu, HostTypeMove));
    } else {
        setNerve(GET_NERVE(Birikyu, HostTypeMoveCircle));
    }
}

void Birikyu::exeMove() {
    MR::startLevelSound(this, "SE_OJ_LV_BIRIKYU_MOVE");

    if (!tryStopPointing()) {
        if (MR::isRailReachedGoal(this)) {
            MR::reverseRailDirection(this);
            s32 arg = 0;
            MR::getCurrentRailPointArg0NoInit(this, &arg);

            if (arg > 0) {
                setNerve(GET_NERVE(Birikyu, HostTypeWaitAtEdge));
                return;
            } else {
                MR::emitEffect(this, "Clash");
            }
        }

        MR::moveCoordAndFollowTrans(this, mSpeed);
    }
}

void Birikyu::exeMoveCircle() {
    MR::startLevelSound(this, "SE_OJ_LV_BIRIKYU_MOVE");
    if (!tryStopPointing()) {
        mCircleAngle = MR::repeat(mCircleAngle + (mSpeed / 400.0f), 0.0f, TWO_PI);
        Pos3f rotation;
        rotation.identity();
        rotation.makeRotate(mCircleAxis, mCircleAngle);
        Vec3f offset = mCircleFront * 400.0f;
        rotation.mult(offset, offset);
        Vec3f circlePos = (mCircleCenter + offset);
        mPosition.set(circlePos);
    }
}

void Birikyu::exeWaitAtEdge() {
    MR::startLevelSound(this, "SE_OJ_LV_BIRIKYU_MOVE");
    s32 arg = 0;
    MR::getCurrentRailPointArg0NoInit(this, &arg);

    if (MR::isStep(this, arg)) {
        setNerve(GET_NERVE(Birikyu, HostTypeMove));
    }
}

void Birikyu::exeAttack() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        MR::emitEffect(this, "Hit");
    }

    MR::startLevelSound(this, "SE_OJ_LV_BIRIKYU_MOVE");

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::sendMsgToGroupMember(ACTMES_GROUP_MOVE_START, this, getSensor("body"), "body");
        goMove();
    }
}

void Birikyu::exeAttackWait() {
}

void Birikyu::exeStopPointing() {
    if (MR::isFirstStep(this)) {
        MR::startPointerHitSound();
        if (MR::isRegisteredEffect(this, "Touch")) {
            MR::emitEffect(this, "Touch");
        }
    }

    MR::startPointerFreezeLevelSound(this);

    if (!MR::isStarPointerPointing2POnPressButton(this, "弱", true, false)) {
        if (MR::isRegisteredEffect(this, "Touch")) {
            MR::deleteEffect(this, "Touch");
        }

        MR::sendMsgToGroupMember(ACTMES_GROUP_MOVE_START, this, getSensor("body"), "body");
        goMove();
    }
}

BirikyuWithFace::BirikyuWithFace(const char* pName) : Birikyu(pName) {
    lumacore::setClass(this, lumacore::kClassId< BirikyuWithFace >);
    mIsFaceReversed = false;
}

void BirikyuWithFace::init(const TableIter& rIter) {
    Birikyu::init(rIter);
    s32 num = -1;
    MR::getPlacementArg1NoInit(rIter, &num);
    mIsFaceReversed = num == 1;
}

void BirikyuWithFace::calcAndSetBaseMtx() {
    Pos3f baseMtx;
    baseMtx.identity();
    if (mHasRail) {
        Vec3f railDir(MR::getRailDirection(this));
        if (mIsFaceReversed) {
            railDir.negate();
        }

        if (!MR::isRailGoingToEnd(this)) {
            railDir.negate();
        }

        Vec3f axis(mCircleAxis);
        if (MR::isSameDirection(axis, railDir)) {
            MR::makeMtxSideFront(&baseMtx, railDir, mCircleFront);
        } else {
            MR::makeMtxSideUp(&baseMtx, railDir, axis);
        }
    } else {
        baseMtx.makeRotate(mCircleAxis, mCircleAngle);
    }

    baseMtx.mMtx[0][3] = mPosition.x;
    baseMtx.mMtx[1][3] = mPosition.y;
    baseMtx.mMtx[2][3] = mPosition.z;
    MR::setBaseTRMtx(this, baseMtx);
}

f32 BirikyuWithFace::getHitRadius() const {
    return 50.0f;
}

const char* BirikyuWithFace::getCenterJointName() const {
    return "Center";
}
