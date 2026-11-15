#include "Game/MapObj/LavaProminence.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const s32 sDefaultTimeWait = NebulaFrameClock::stepsOfSeconds(3);
    static const s32 sTimeSign = NebulaFrameClock::stepsOfMilliseconds(1500);
    const f32 sDefaultMoveSpeed = 20.0f;
    // Distance the body's sphere must pass beyond the view before the drop effect is deleted (it is made at the view's edge).
    const f32 sDropClipLeaveMargin = 150.0f;
};  // namespace

namespace NrvLavaProminence {
    NEW_NERVE(HostTypeNrvWaitSwitch, LavaProminence, WaitSwitch);
    NEW_NERVE(HostTypeNrvWait, LavaProminence, Wait);
    NEW_NERVE(HostTypeNrvSign, LavaProminence, Sign);
    NEW_NERVE(HostTypeNrvMoveStartExtra, LavaProminence, MoveStartExtra);
    NEW_NERVE(HostTypeNrvMoveStart, LavaProminence, MoveStart);
    NEW_NERVE(HostTypeNrvMoveLoop, LavaProminence, MoveLoop);
    NEW_NERVE(HostTypeNrvMoveEnd, LavaProminence, MoveEnd);
    NEW_NERVE(HostTypeNrvMoveEndExtra, LavaProminence, MoveEndExtra);
};  // namespace NrvLavaProminence

LavaProminence::LavaProminence(const char* pName)
    : LiveActor(pName), mWaitTime(::sDefaultTimeWait), mMoveSpeed(::sDefaultMoveSpeed), mMoveRailCoord(), mMoveRailEndOffset(),
      mRailStartDir(0, 0, 0), mRailEndDir(0, 0, 0), mRailStartPos(0.0f), mRailEndPos(0.0f), mClipCenter(0, 0, 0), mBloomModel(),
      mRotateQuat(0.0f, 0.0f, 0.0f, 1.0f), mRailDir(0, 0, 1), mEmitDropEffect(true) {
    lumacore::setClass(this, lumacore::kClassId< LavaProminence >);
    mEffectStartMtx.identity();
    mEffectEndMtx.identity();
    mBloomModelMtx.identity();
}

void LavaProminence::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mRotateQuat.setEuler(mRotation);
    MR::getPlacementArg0NoInit(rIter, &mWaitTime);
    MR::getPlacementArg1NoInit(rIter, &mMoveSpeed);
    MR::getPlacementArg3NoInit(rIter, &mMoveRailEndOffset);
    MR::useStageSwitchReadA(this, rIter);
    const char* name = nullptr;
    MR::getObjectName(&name, rIter);
    bool isWithoutShadow = false;
    initModelManagerWithAnm("LavaProminence", nullptr, false);
    MR::startBtk(this, "LavaProminence");

    if (MR::isEqualString(name, "LavaProminenceWithoutShadow")) {
        isWithoutShadow = true;
    }

    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    Vec3f offset;
    offset.set(0.0f);
    MR::addHitSensorMapObjSimple(this, "body", 8, 120.0f, offset);
    initRailRider(rIter);
    MR::calcRailPosAtCoord(&mRailStartPos, this, 0.0f);
    MR::calcRailPosAtCoord(&mRailEndPos, this, MR::getRailTotalLength(this));
    MR::calcRailDirectionAtCoord(&mRailStartDir, this, 0.0f);
    MR::calcRailDirectionAtCoord(&mRailEndDir, this, MR::getRailTotalLength(this));
    initSound(6, false);
    MR::setGroupClipping(this, rIter, 16);
    MR::initAndSetRailClipping(&mClipCenter, this, 300.0f, 300.0f);

    if (MR::isValidSwitchA(this)) {
        initNerve(GET_NERVE(LavaProminence, HostTypeNrvWaitSwitch));
    } else {
        initNerve(GET_NERVE(LavaProminence, HostTypeNrvWait));
    }

    initEffectKeeper(0, nullptr);
    MR::setEffectHostMtx(this, "Sign", mEffectStartMtx);
    MR::setEffectHostMtx(this, "Start", mEffectStartMtx);
    MR::setEffectHostMtx(this, "End", mEffectEndMtx);
    MR::setEffectName(this, "Drop", "DropEffect");

    if (!isWithoutShadow) {
        MR::initShadowVolumeSphere(this, 100.0f);
    }

    mBloomModel = MR::createBloomModel(this, mBloomModelMtx);
    MR::startBtk(mBloomModel, "LavaProminenceBloom");
    makeActorAppeared();
}

void LavaProminence::appear() {
    LiveActor::appear();
}

void LavaProminence::kill() {
    LiveActor::kill();
}

void LavaProminence::initAfterPlacement() {
    Vec3f initPosVec(mPosition);
    Vec3f upVec;

    mPosition.set(mRailStartPos);
    MR::calcGravityVectorOrZero(this, &upVec, nullptr, 0);

    if (MR::isNearZero(upVec)) {
        MR::calcUpVec(&upVec, this);
        upVec = -upVec;
    }

    MR::makeMtxUpNoSupportPos(&mEffectStartMtx, -upVec, mRailStartPos);
    mPosition.set(mRailEndPos);
    MR::calcGravityVectorOrZero(this, &upVec, nullptr, 0);

    if (MR::isNearZero(upVec)) {
        MR::calcUpVec(&upVec, this);
        upVec = -upVec;
    }

    MR::makeMtxUpNoSupportPos(&mEffectEndMtx, -upVec, mRailEndPos);
    mPosition.set(initPosVec);
}

void LavaProminence::startClipped() {
    LiveActor::startClipped();
    MR::deleteEffect(this, "DropEffect");
    mEmitDropEffect = true;
    MR::forceDeleteEffectAll(this);

    if (!isNerve(GET_NERVE(LavaProminence, HostTypeNrvWaitSwitch))) {
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvWait));
    }
}

void LavaProminence::moveOnRail() {
    mMoveRailCoord += mMoveSpeed;
    MR::setRailCoord(this, MR::clamp(mMoveRailCoord, 0.0f, MR::getRailTotalLength(this)));
    mRailDir.set(MR::getRailDirection(this));
    MR::moveTransToCurrentRailPos(this);
    updateEffectClipping(isNrvMove());
    MR::calcGravity(this);
}

void LavaProminence::updateEffectClipping(bool isMoving) {
    // The drops go when the body is a margin outside the view and come back at its edge: one test for both deleted and
    // made the effect again at every crossing of a camera or body at the edge, restarting its particles.
    const f32 radius = getSensor("body")->getRadius();
    if (!mEmitDropEffect) {
        if (!isMoving || MR::isJudgedToClipFrustum(mPosition, radius + ::sDropClipLeaveMargin)) {
            MR::deleteEffect(this, "DropEffect");
            mEmitDropEffect = true;
        }
    } else if (isMoving && !MR::isJudgedToClipFrustum(mPosition, radius)) {
        MR::emitEffect(this, "DropEffect");
        mEmitDropEffect = false;
    }
}

void LavaProminence::exeWaitSwitch() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::deleteEffect(this, "DropEffect");
        mEmitDropEffect = true;
    }

    if (MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvWait));
    }
}

void LavaProminence::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::deleteEffect(this, "DropEffect");
        mEmitDropEffect = true;
        mMoveRailCoord = 0.0f;
        MR::setRailCoord(this, 0.0f);
        MR::moveTransToCurrentRailPos(this);
    }

    if (MR::isValidSwitchA(this) && !MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvWaitSwitch));
    } else if (MR::isStep(this, mWaitTime)) {
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvSign));
    }
}

void LavaProminence::exeSign() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Sign");
        MR::hideModel(this);
        MR::deleteEffect(this, "DropEffect");
        mEmitDropEffect = true;
    }

    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_SIGN");

    if (MR::isStep(this, ::sTimeSign)) {
        MR::deleteEffect(this, "Sign");
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvMoveStartExtra));
    }
}

void LavaProminence::exeMoveStartExtra() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Start");
        MR::startSound(this, "SE_OJ_PROMINENCE_START");
        mMoveRailCoord = 0.0f;
        MR::setRailCoord(this, 0.0f);
        mPosition.set(mRailStartPos - mRailStartDir * 300.0f);
        mRailDir.set(mRailStartDir);
        MR::showModel(this);
        MR::emitEffect(this, "DropEffect");
        mEmitDropEffect = false;
    }

    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_SIGN");
    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_MOVE");
    mPosition.add(mRailStartDir * mMoveSpeed);
    MR::calcGravity(this);
    // A speed of 0 or less (placement arg 1) ends the start at once, not at 300 / 0.
    s32 endStartStep = mMoveSpeed > 0.0f ? static_cast< s32 >(300.0f / mMoveSpeed) : 0;

    if (MR::isGreaterStep(this, endStartStep - 1)) {
        MR::deleteEffect(this, "Start");
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvMoveLoop));
    }
}

void LavaProminence::exeMoveStart() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Start");
        MR::startSound(this, "SE_OJ_PROMINENCE_START");
        mMoveRailCoord = 0.0f;
        MR::setRailCoord(this, 0.0f);
        MR::moveTransToCurrentRailPos(this);
        MR::showModel(this);
        MR::emitEffect(this, "DropEffect");
        mEmitDropEffect = false;
    }

    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_SIGN");
    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_MOVE");
    moveOnRail();

    if (mMoveRailCoord >= 300.0f) {
        MR::deleteEffect(this, "Start");
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvMoveLoop));
    }
}

void LavaProminence::exeMoveLoop() {
    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_MOVE");
    moveOnRail();

    if (mMoveRailCoord >= MR::getRailTotalLength(this) - mMoveRailEndOffset) {
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvMoveEndExtra));
    }
}

void LavaProminence::exeMoveEnd() {
    if (MR::isFirstStep(this)) {
        MR::setRailCoord(this, mMoveRailCoord);
        MR::emitEffect(this, "End");
    }

    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_MOVE");
    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_END");
    moveOnRail();

    if (mMoveRailCoord >= MR::getRailTotalLength(this)) {
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvMoveEndExtra));
    }
}

void LavaProminence::exeMoveEndExtra() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "End");
    }

    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_MOVE");
    MR::startLevelSound(this, "SE_OJ_LV_PROMINENCE_END");
    mPosition.add(mRailEndDir * mMoveSpeed);

    if (90000.0f < mRailEndPos.squared(mPosition)) {
        MR::hideModel(this);
        MR::deleteEffect(this, "DropEffect");
        mEmitDropEffect = true;
        MR::deleteEffect(this, "End");
        setNerve(GET_NERVE(LavaProminence, HostTypeNrvWait));
    }
}

void LavaProminence::calcAndSetBaseMtx() {
    Pos3f mtx;
    Vec3f vec(mRailDir);
    Vec3f front;
    Quat4f quat;

    MR::normalizeOrZero(&vec);

    if (!MR::isNearZero(vec)) {
        mRotateQuat.getZDir(front);
        quat.setRotate(front, vec);
        mRotateQuat.mult(quat);
    }

    mtx.makeQuat(mRotateQuat);
    mtx.setTrans(mPosition);
    MR::setBaseTRMtx(this, mtx);
    mBloomModelMtx.set(mtx);
}

void LavaProminence::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayerOrRide(pReceiver)) {
        MR::sendMsgEnemyAttackFireStrong(pReceiver, pSender);
    }
}

bool LavaProminence::isNrvMove() {
    return isNerve(GET_NERVE(LavaProminence, HostTypeNrvMoveStartExtra)) || isNerve(GET_NERVE(LavaProminence, HostTypeNrvMoveLoop)) ||
           isNerve(GET_NERVE(LavaProminence, HostTypeNrvMoveEndExtra));
}
