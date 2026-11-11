#include "Game/Enemy/Gesso.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeFromBox_f1, "exeComeFromBox.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeFromBox_f2, "exeComeFromBox.f2", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeFromBox_f3, "exeComeFromBox.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeFromBox_f4, "exeComeFromBox.f4", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f2, "exeWait.f2", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f3, "exeWait.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f4, "exeWait.f4", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f1, "exeSearch.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f2, "exeSearch.f2", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f3, "exeSearch.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_f4, "exeSearch.f4", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalkCharge_f1, "exeWalkCharge.f1", 0.995f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWalk_f1, "exeWalk.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSink_f1, "exeSink.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLostPlayer_f1, "exeLostPlayer.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLostPlayer_f2, "exeLostPlayer.f2", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeLostPlayer_f3, "exeLostPlayer.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_f1, "exeAttack.f1", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_f2, "exeAttack.f2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_f3, "exeAttack.f3", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeAttack_f4, "exeAttack.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePunchDown_f1, "exePunchDown.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeBack_f1, "exeComeBack.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeBack_f2, "exeComeBack.f2", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeBack_f3, "exeComeBack.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeBack_f4, "exeComeBack.f4", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeBack_f5, "exeComeBack.f5", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeComeBack_f6, "exeComeBack.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_f1, "attackSensor.f1", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveMsgPush_f1, "receiveMsgPush.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_knockOut_f1, "knockOut.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f1, "calcWalkMove.f1", 0.990099f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f2, "calcWalkMove.f2", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f3, "calcWalkMove.f3", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f4, "calcWalkMove.f4", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f5, "calcWalkMove.f5", 2.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f6, "calcWalkMove.f6", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f7, "calcWalkMove.f7", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f8, "calcWalkMove.f8", 2.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f9, "calcWalkMove.f9", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f10, "calcWalkMove.f10", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f11, "calcWalkMove.f11", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f12, "calcWalkMove.f12", 4.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f13, "calcWalkMove.f13", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f14, "calcWalkMove.f14", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f15, "calcWalkMove.f15", 7.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f16, "calcWalkMove.f16", 4.6f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f17, "calcWalkMove.f17", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcWalkMove_f18, "calcWalkMove.f18", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSinkMove_f1, "calcSinkMove.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSinkMove_f2, "calcSinkMove.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSinkMove_f3, "calcSinkMove.f3", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSinkMove_f4, "calcSinkMove.f4", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSinkMove_f5, "calcSinkMove.f5", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSinkMove_f6, "calcSinkMove.f6", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSinkMove_f7, "calcSinkMove.f7", 2.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcSinkMove_f8, "calcSinkMove.f8", 180.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_isNearPlayer_1, "exeWait.isNearPlayer.1", 1400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSearch_turnDirectionToPlayerDegree_1, "exeSearch.turnDirectionToPlayerDegree.1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeSink_isNearPlayer_1, "exeSink.isNearPlayer.1", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_selectNextNerve_isNearPlayer_1, "selectNextNerve.isNearPlayer.1", 2400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_clipAndInitPos_isNearPlayer_1, "clipAndInitPos.isNearPlayer.1", 10000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryChangeHighSpeedMode_isNearPlayer_1, "tryChangeHighSpeedMode.isNearPlayer.1", 1000.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeComeFromBox_1, "exeComeFromBox.1", 10);
NEBULA_TUNABLE_NAMED(s32, cStep_exeWait_1, "exeWait.1", 2);
NEBULA_TUNABLE_NAMED(s32, cStep_exePunchDown_1, "exePunchDown.1", 20);

namespace {
    NEBULA_TUNABLE(s32, sStepForWalk, 43);
};  // namespace

namespace NrvGesso {
    NEW_NERVE(GessoNrvComeFromBox, Gesso, ComeFromBox);
    NEW_NERVE(GessoNrvWait, Gesso, Wait);
    NEW_NERVE(GessoNrvSearch, Gesso, Search);
    NEW_NERVE(GessoNrvWalkCharge, Gesso, WalkCharge);
    NEW_NERVE(GessoNrvWalk, Gesso, Walk);
    NEW_NERVE(GessoNrvSink, Gesso, Sink);
    NEW_NERVE(GessoNrvLostPlayer, Gesso, LostPlayer);
    NEW_NERVE(GessoNrvAttack, Gesso, Attack);
    NEW_NERVE(GessoNrvPunchDown, Gesso, PunchDown);
    NEW_NERVE(GessoNrvComeBack, Gesso, ComeBack);
    NEW_NERVE_ONEND(GessoNrvPointerSwoon, Gesso, PointerSwoon, PointerSwoon);
    NEW_NERVE(GessoNrvRotate, Gesso, Rotate);

};  // namespace NrvGesso

Gesso::Gesso(const char* pName)
    : LiveActor(pName), mScaleController(), mStateBindStarPointer(), mInitPos(0.0f, 0.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mUp(0.0f, 1.0f, 0.0f),
      mUpTarget(0.0f, 1.0f, 0.0f), mClipStep(), mPushVelocity(0.0f, 0.0f, 0.0f), mIsMarioLeft(), mIsHighSpeedMode() {
    lumacore::setClass(this, lumacore::kClassId< Gesso >);
}

void Gesso::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    mInitPos = mPosition;
    MR::useStageSwitchReadAppear(this, rIter);
    initModelManagerWithAnm("Gesso", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::initLightCtrl(this);
    initHitSensor(1);
    Vec3f sensorOffset;
    sensorOffset.x = 0.0f;
    sensorOffset.y = 0.0f;
    sensorOffset.z = 0.0f;
    MR::addHitSensor(this, "body", ATYPE_KILLER_TARGET_ENEMY, 8, cNum_init_f1, sensorOffset);
    initBinder(cNum_init_f2, 0.0f, 0);
    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    Vec3f pointerOffset;
    pointerOffset.x = 0.0f;
    pointerOffset.y = 0.0f;
    pointerOffset.z = 0.0f;
    MR::initStarPointerTarget(this, cNum_init_f3, pointerOffset);
    mScaleController = new AnimScaleController(nullptr);
    mStateBindStarPointer = new WalkerStateBindStarPointer(this, mScaleController);
    MR::initShadowVolumeSphere(this, cNum_init_f4);
    MR::calcFrontVec(&mFront, this);
    MR::calcUpVec(&mUp, this);
    mUpTarget.set(mUp);
    MR::declareCoin(this, 1);
    MR::addToAttributeGroupSearchTurtle(this);
    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        s32 initialBehavior;
        MR::getPlacementArg0WithInit(rIter, &initialBehavior);
        if (initialBehavior == 0) {
            initNerve(GET_NERVE(Gesso, GessoNrvComeFromBox));
        } else {
            initNerve(GET_NERVE(Gesso, GessoNrvWait));
        }

        makeActorDead();
    } else {
        initNerve(GET_NERVE(Gesso, GessoNrvWait));
        makeActorAppeared();
    }
}

void Gesso::kill() {
    MR::emitEffect(this, "Death");
    MR::appearCoinPop(this, mPosition, 1);
    LiveActor::kill();
}

void Gesso::control() {
    mScaleController->updateNerve();
    if (tryPointerSwoon()) {
        return;
    }

    if (clipAndInitPos()) {
        return;
    }

    Vec3f playerDirection;
    Vec3f up;
    Vec3f offset;
    MR::normalize(*MR::getPlayerPos() - mPosition, &playerDirection);
    MR::turnVecToVecCosOnPlane(&mUp, mUpTarget, mFront, MR::cosDegree(cNum_control_f1));
    if (mVelocity.dot(mPushVelocity) < 0.0f) {
        const Vec3f& rVelocity = mVelocity;
        mVelocity.scaleAdd(-mPushVelocity.dot(rVelocity), mPushVelocity, rVelocity);
    }

    mVelocity.add(mPushVelocity);
    mPushVelocity *= cNum_control_f2;
    if (MR::isNearZero(mPushVelocity)) {
        mPushVelocity.zero();
    }

    up.negate(mGravity);
    offset.scale(cNum_control_f3, up);
    if (!MR::isInWater(this, offset) && mVelocity.dot(up) > 0.0f) {
        const Vec3f& rVelocity = mVelocity;
        mVelocity.scaleAdd(-up.dot(rVelocity), up, rVelocity);
    }
}

void Gesso::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::makeMtxUpFrontPos(&mtx, mUp, mFront, mPosition);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void Gesso::exeComeFromBox() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "FromBox");
        MR::startSound(this, "SE_EM_GESSO_SWIM_L");
        MR::invalidateHitSensors(this);
        mPosition.scaleAdd(-cNum_exeComeFromBox_f1, mGravity, mPosition);
        mUpTarget.set(0.0f, 1.0f, cNum_exeComeFromBox_f2);
        MR::normalize(&mUpTarget);
        mUp.set(mUpTarget);
    }

    if (MR::isLessStep(this, cStep_exeComeFromBox_1)) {
        calcAndSetVelocity(0.0f, cNum_exeComeFromBox_f3, cNum_exeComeFromBox_f4);
    } else {
        mVelocity.zero();
    }

    if (MR::isActionEnd(this)) {
        mVelocity.zero();
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(Gesso, GessoNrvWait));
    }
}

void Gesso::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Wait");
        calcAndSetUpVecTarget(0.0f, 0.0f, 0.0f);
    }

    if (MR::isStep(this, cStep_exeWait_1)) {
        MR::invalidateClipping(this);
    }

    mVelocity.scale(cNum_exeWait_f1 * (MR::sinDegree(cNum_exeWait_f2 + (cNum_exeWait_f3 * getNerveStep()))), mGravity);
    MR::rotateVecDegree(&mFront, mGravity, cNum_exeWait_f4);
    if (MR::isNearPlayer(this, cNum_exeWait_isNearPlayer_1) && MR::isPlayerInWaterMode()) {
        MR::validateClipping(this);
        setNerve(GET_NERVE(Gesso, GessoNrvSearch));
    }
}

void Gesso::exeSearch() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Search");
        MR::startSound(this, "SE_EM_GESSO_FIND");
        calcAndSetUpVecTarget(0.0f, 0.0f, 0.0f);
    }

    mVelocity.scale(cNum_exeSearch_f1 * (MR::sinDegree(cNum_exeSearch_f2 + (cNum_exeSearch_f3 * getNerveStep()))), mGravity);
    MR::turnDirectionToPlayerDegree(this, &mFront, cNum_exeSearch_turnDirectionToPlayerDegree_1);
    if (MR::isFaceToPlayerHorizontalDegree(this, mFront, cNum_exeSearch_f4) && MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Gesso, GessoNrvWalkCharge));
    }
}

void Gesso::exeWalkCharge() {
    if (MR::isFirstStep(this)) {
        if (tryChangeHighSpeedMode()) {
            MR::startAction(this, "WalkFast");
            mUpTarget.set(0.0f, 1.0f, 1.0f);
            MR::normalize(&mUpTarget);
        } else {
            MR::startAction(this, "Walk");
            mUpTarget.set(0.0f, 1.0f, 1.0f);
            MR::normalize(&mUpTarget);
        }
    }

    mVelocity.mult(cNum_exeWalkCharge_f1);
    if (MR::isStep(this, ::sStepForWalk)) {
        mIsMarioLeft = isMarioLeft();
        setNerve(GET_NERVE(Gesso, GessoNrvWalk));
    }
}

void Gesso::exeWalk() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_GESSO_SWIM_L");
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), cNum_exeWalk_f1);
    if (calcWalkMove(getNerveStep())) {
        selectNextNerve();
    }
}

void Gesso::exeSink() {
    if (MR::isFirstStep(this)) {
        if (MR::isNearPlayer(this, cNum_exeSink_isNearPlayer_1)) {
            MR::startAction(this, "SinkFast");
        } else {
            MR::startAction(this, "Sink");
        }
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), cNum_exeSink_f1);
    if (MR::isBindedGround(this)) {
        setNerve(GET_NERVE(Gesso, GessoNrvWalkCharge));
    } else if (calcSinkMove(getNerveStep())) {
        selectNextNerve();
    }
}

void Gesso::exeLostPlayer() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "CoolDown");
        calcAndSetUpVecTarget(0.0f, 0.0f, 0.0f);
    }

    mVelocity.scale(cNum_exeLostPlayer_f1 * (MR::sinDegree(cNum_exeLostPlayer_f2 + (cNum_exeLostPlayer_f3 * getNerveStep()))), mGravity);
    if (MR::isActionEnd(this)) {
        setNerve(GET_NERVE(Gesso, GessoNrvWait));
    }
}

void Gesso::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "Attack");
        MR::startSound(this, "SE_EM_GESSO_INK");
        calcAndSetUpVecTarget(0.0f, 0.0f, 0.0f);
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        MR::startAction(this, "Wait");
    }

    if (MR::isBckPlaying(this, "Attack")) {
        mVelocity.mult(cNum_exeAttack_f1);
    } else {
        mVelocity.scale(cNum_exeAttack_f2 * (MR::sinDegree(cNum_exeAttack_f3 + (cNum_exeAttack_f4 * getNerveStep()))), mGravity);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(4))) {
        setNerve(GET_NERVE(Gesso, GessoNrvWait));
    }
}

void Gesso::exePunchDown() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::stopScene(5);
        MR::startAction(this, "PunchDown");
        MR::startSound(this, "SE_EM_GESSO_HIT_PUNCH");
        MR::startBlowHitSound(this);
    }

    MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), cNum_exePunchDown_f1);
    if (MR::isStep(this, cStep_exePunchDown_1) || MR::isBinded(this)) {
        MR::startSound(this, "SE_EM_GESSO_DEAD");
        kill();
    }
}

void Gesso::exeComeBack() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "CoolDown");
    }

    if (MR::isBckPlaying(this, "CoolDown")) {
        mVelocity.scale(cNum_exeComeBack_f1 * (MR::sinDegree(cNum_exeComeBack_f2 + (cNum_exeComeBack_f3 * getNerveStep()))), mGravity);
    } else {
        if (MR::isBckOneTimeAndStopped(this)) {
            MR::startAction(this, "Sink");
        }

        if (MR::isPlayerInWaterMode()) {
            setNerve(GET_NERVE(Gesso, GessoNrvWait));
        } else {
            MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), cNum_exeComeBack_f4);
            Vec3f homeDir;
            Vec3f diff = mInitPos - mPosition;
            MR::normalize(diff, &homeDir);
            mVelocity.scale(cNum_exeComeBack_f5, homeDir);
            if (MR::isNear(this, mInitPos, cNum_exeComeBack_f6)) {
                setNerve(GET_NERVE(Gesso, GessoNrvWait));
            }
        }
    }
}

void Gesso::exeRotate() {
    if (MR::isFirstStep(this)) {
        MR::startAction(this, "StarPiece");
        mVelocity.zero();
    }

    MR::startLevelSound(this, "SE_EM_LV_GESSO_STAR_PIECE_HIT");
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Gesso, GessoNrvWait));
    }
}

void Gesso::exePointerSwoon() {
    MR::updateActorStateAndNextNerve(this, mStateBindStarPointer, GET_NERVE(Gesso, GessoNrvWait));
}

void Gesso::endPointerSwoon() {
    mStateBindStarPointer->kill();
}

void Gesso::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(Gesso, GessoNrvPunchDown)) && MR::isSensorEnemy(pSender) && MR::isSensorPlayer(pReceiver) &&
        !isNerve(GET_NERVE(Gesso, GessoNrvPointerSwoon)) && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
        MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
        Vec3f recoilDir;
        Vec3f diff = pSender->mPosition - pReceiver->mPosition;
        MR::normalize(diff, &recoilDir);
        mVelocity.scale(cNum_attackSensor_f1, recoilDir);
        setNerve(GET_NERVE(Gesso, GessoNrvAttack));
    }
}

bool Gesso::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Gesso, GessoNrvPunchDown))) {
        return false;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(Gesso, GessoNrvRotate));
        return true;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        knockOut(pSender, pReceiver);
        return true;
    }

    return false;
}

bool Gesso::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(Gesso, GessoNrvPunchDown))) {
        return false;
    }

    if (MR::isMsgExplosionAttack(msg)) {
        knockOut(pSender, pReceiver);
        return true;
    }

    if (pSender->mType == ACTMES_RUSHDROP) {
        knockOut(pSender, pReceiver);
        return false;
    }

    return false;
}

bool Gesso::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isSensorEnemy(pSender) || MR::isSensorMapObj(pSender)) {
        Vec3f pushDir;
        pushDir.sub(mPosition, pSender->mHost->mPosition);
        MR::normalizeOrZero(&pushDir);
        mPushVelocity.scaleAdd(cNum_receiveMsgPush_f1, mPushVelocity, pushDir);
        return true;
    }

    return false;
}

void Gesso::selectNextNerve() {
    if (!MR::isPlayerInWaterMode()) {
        setNerve(GET_NERVE(Gesso, GessoNrvComeBack));
    } else if (!MR::isNearPlayer(this, cNum_selectNextNerve_isNearPlayer_1)) {
        setNerve(GET_NERVE(Gesso, GessoNrvLostPlayer));
    } else if (isMarioUp()) {
        setNerve(GET_NERVE(Gesso, GessoNrvWalkCharge));
    } else if (!isNerve(GET_NERVE(Gesso, GessoNrvSink))) {
        setNerve(GET_NERVE(Gesso, GessoNrvSink));
    }
}

void Gesso::knockOut(HitSensor* pSender, HitSensor* pReceiver) {
    Vec3f knockDir;
    Vec3f diff = pReceiver->mPosition - pSender->mPosition;
    MR::normalize(diff, &knockDir);
    mVelocity.scale(cNum_knockOut_f1, knockDir);
    MR::turnDirectionToTarget(this, &mFront, pSender->mPosition, -1.0f);
    setNerve(GET_NERVE(Gesso, GessoNrvPunchDown));
}

bool Gesso::clipAndInitPos() {
    if (!MR::isNearPlayer(this, cNum_clipAndInitPos_isNearPlayer_1) || MR::isJudgedToClipFrustum(mPosition, getSensor(nullptr)->getRadius())) {
        if (mClipStep >= NebulaFrameClock::stepsOfSeconds(5)) {
            mPosition.set(mInitPos);
            setNerve(GET_NERVE(Gesso, GessoNrvWait));
            MR::validateClipping(this);
            mClipStep = 0;
            return true;
        }

        mClipStep++;
        return false;
    }

    mClipStep = 0;
    return false;
}

bool Gesso::calcWalkMove(s32 step) {
    f32 verticalSpeed, sideSign, angle;

    if (mIsMarioLeft) {
        sideSign = 1.0f;
    } else {
        sideSign = -1.0f;
    }

    angle = cNum_calcWalkMove_f1 * step;

    if (mIsHighSpeedMode) {
        verticalSpeed = MR::cosDegree(angle);
        Vec3f* vel = &mVelocity;
        Vec3f upVec;
        Vec3f sideVec;
        verticalSpeed = cNum_calcWalkMove_f2 * verticalSpeed;
        MR::calcUpVec(&upVec, this);
        MR::calcSideVec(&sideVec, this);
        mVelocity.scale(verticalSpeed, upVec);
        vel->scaleAdd((sideSign * cNum_calcWalkMove_f3) / cNum_calcWalkMove_f4, sideVec, *vel);
        vel->scaleAdd(cNum_calcWalkMove_f5, mFront, *vel);
    } else {
        verticalSpeed = MR::cosDegree(angle);
        Vec3f* vel = &mVelocity;
        Vec3f upVec;
        Vec3f sideVec;
        verticalSpeed = cNum_calcWalkMove_f6 * verticalSpeed;
        MR::calcUpVec(&upVec, this);
        MR::calcSideVec(&sideVec, this);
        mVelocity.scale(verticalSpeed, upVec);
        vel->scaleAdd(sideSign * cNum_calcWalkMove_f7, sideVec, *vel);
        vel->scaleAdd(cNum_calcWalkMove_f8, mFront, *vel);
    }

    if (MR::isFirstStep(this)) {
        if (mIsHighSpeedMode) {
            calcAndSetUpVecTarget((cNum_calcWalkMove_f9 * sideSign) / cNum_calcWalkMove_f10, MR::cosDegree(angle) * cNum_calcWalkMove_f11, cNum_calcWalkMove_f12);
        } else {
            calcAndSetUpVecTarget((cNum_calcWalkMove_f13 * sideSign) / cNum_calcWalkMove_f14, MR::cosDegree(angle) * cNum_calcWalkMove_f15, cNum_calcWalkMove_f16);
        }
    }

    if (angle >= cNum_calcWalkMove_f17) {
        mUpTarget.set(0.0f, 1.0f, 1.0f);
        MR::normalize(&mUpTarget);
    }

    return angle >= cNum_calcWalkMove_f18;
}

bool Gesso::calcSinkMove(s32 step) {
    f32 verticalSpeed, sideSign, angle;

    if (mIsMarioLeft) {
        sideSign = 1.0f;
    } else {
        sideSign = -1.0f;
    }

    angle = cNum_calcSinkMove_f1 + cNum_calcSinkMove_f2 * step;
    if (angle >= cNum_calcSinkMove_f3) {
        angle = cNum_calcSinkMove_f4;
    }

    verticalSpeed = MR::cosDegree(angle) * cNum_calcSinkMove_f5;
    Vec3f* vel = &mVelocity;
    Vec3f upVec;
    Vec3f sideVec;
    MR::calcUpVec(&upVec, this);
    MR::calcSideVec(&sideVec, this);

    mVelocity.scale(verticalSpeed, upVec);
    vel->scaleAdd(sideSign * cNum_calcSinkMove_f6, sideVec, *vel);
    vel->scaleAdd(cNum_calcSinkMove_f7, mFront, *vel);
    mUpTarget.set(0.0f, 1.0f, 1.0f);
    MR::normalize(&mUpTarget);

    return angle >= cNum_calcSinkMove_f8;
}

void Gesso::calcAndSetVelocity(f32 side, f32 up, f32 front) {
    Vec3f* vel = &mVelocity;
    Vec3f sideVec;
    Vec3f upVec;
    MR::calcUpVec(&upVec, this);
    MR::calcSideVec(&sideVec, this);
    mVelocity.scale(up, upVec);
    vel->scaleAdd(side, sideVec, *vel);
    vel->scaleAdd(front, mFront, *vel);
}

void Gesso::calcAndSetUpVecTarget(f32 x, f32 y, f32 z) {
    if (x == 0.0f && y == 0.0f && z == 0.0f) {
        mUpTarget.set(0.0f, 1.0f, 0.0f);
        return;
    }

    mUpTarget.set(x, y, z);
    MR::normalize(&mUpTarget);
    return;
}

bool Gesso::isMarioUp() const {
    Vec3f* pos = MR::getPlayerPos();
    Vec3f playerDist;
    playerDist.sub(*pos, mPosition);
    Vec3f upVec;
    upVec.negate(mGravity);
    return playerDist.dot(upVec) > 0.0f;
}

bool Gesso::isMarioLeft() const {
    Vec3f* pos = MR::getPlayerPos();
    Vec3f playerDist;
    playerDist.sub(*pos, mPosition);
    Vec3f sideVec;
    MR::calcSideVec(&sideVec, this);
    return playerDist.dot(sideVec) > 0.0f;
}

bool Gesso::tryChangeHighSpeedMode() {
    bool change = false;
    if (MR::isNearPlayer(this, cNum_tryChangeHighSpeedMode_isNearPlayer_1) && !MR::isPlayerInWaterMode()) {
        change = true;
    }

    mIsHighSpeedMode = change;
    return change;
}

bool Gesso::tryPointerSwoon() {
    if (isNerve(GET_NERVE(Gesso, GessoNrvPointerSwoon))) {
        return false;
    }

    if (isNerve(GET_NERVE(Gesso, GessoNrvPunchDown))) {
        return false;
    }

    if (isNerve(GET_NERVE(Gesso, GessoNrvRotate))) {
        return false;
    }

    if (!mStateBindStarPointer->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(Gesso, GessoNrvPointerSwoon));
    return true;
}
