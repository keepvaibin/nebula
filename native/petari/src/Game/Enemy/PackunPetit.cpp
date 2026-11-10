#include "Game/Enemy/PackunPetit.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initStarPointer_f1, "initStarPointer.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThreat_f1, "exeThreat.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThreat_f2, "exeThreat.f2", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTurn_f1, "exeTurn.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTurn_f2, "exeTurn.f2", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTurn_f3, "exeTurn.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePunchDown_f1, "exePunchDown.f1", 2.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePunchDown_f2, "exePunchDown.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePunchDown_f3, "exePunchDown.f3", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exePunchDown_f4, "exePunchDown.f4", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f1, "kill.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_kill_f2, "kill.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initBlowModel_f1, "initBlowModel.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_punchDown_f1, "punchDown.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_punchDown_f2, "punchDown.f2", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryTurn_f1, "tryTurn.f1", 90.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeNonActive_isNearPlayerAnyTime_1, "exeNonActive.isNearPlayerAnyTime.1", 5000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_isNearPlayer_1, "exeWait.isNearPlayer.1", 1700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThreat_isNearPlayer_1, "exeThreat.isNearPlayer.1", 1100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeThreat_isNearPlayer_2, "exeThreat.isNearPlayer.2", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_selectNrvWait_isNearPlayer_1, "selectNrvWait.isNearPlayer.1", 1700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryNonActive_isNearPlayerAnyTime_1, "tryNonActive.isNearPlayerAnyTime.1", 5000.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeThreat_1, "exeThreat.1", 50);
NEBULA_TUNABLE_NAMED(s32, cStep_exeAttackStart_1, "exeAttackStart.1", 70);
NEBULA_TUNABLE_NAMED(s32, cStep_exeAttack_1, "exeAttack.1", 12);
NEBULA_TUNABLE_NAMED(s32, cStep_exePunchDown_1, "exePunchDown.1", 20);
NEBULA_TUNABLE_NAMED(s32, cStep_exeSwoon_1, "exeSwoon.1", 250);


namespace NrvPackunPetit {
    NEW_NERVE_ONEND(PackunPetitNrvNonActive, PackunPetit, NonActive, NonActive);
    NEW_NERVE(PackunPetitNrvWait, PackunPetit, Wait);
    NEW_NERVE(PackunPetitNrvFind, PackunPetit, Find);
    NEW_NERVE(PackunPetitNrvThreat, PackunPetit, Threat);
    NEW_NERVE(PackunPetitNrvLeftTurn, PackunPetit, Turn);
    NEW_NERVE(PackunPetitNrvRightTurn, PackunPetit, Turn);
    NEW_NERVE(PackunPetitNrvAttackStart, PackunPetit, AttackStart);
    NEW_NERVE(PackunPetitNrvAttack, PackunPetit, Attack);
    NEW_NERVE(PackunPetitNrvAttackBack, PackunPetit, AttackBack);
    NEW_NERVE(PackunPetitNrvHitWaitForAttack, PackunPetit, HitWaitForAttack);
    NEW_NERVE(PackunPetitNrvHit, PackunPetit, Hit);
    NEW_NERVE(PackunPetitNrvTrampleDown, PackunPetit, TrampleDown);
    NEW_NERVE(PackunPetitNrvPunchDown, PackunPetit, PunchDown);
    NEW_NERVE(PackunPetitNrvSwoonStart, PackunPetit, SwoonStart);
    NEW_NERVE(PackunPetitNrvSwoon, PackunPetit, Swoon);
    NEW_NERVE(PackunPetitNrvSwoonToThreat, PackunPetit, SwoonToThreat);
    NEW_NERVE_ONEND(PackunPetitNrvPointerSwoon, PackunPetit, PointerSwoon, PointerSwoon);
};  // namespace NrvPackunPetit

PackunPetit::PackunPetit(const char* pName)
    : LiveActor(pName), mScaleController(nullptr), mStarPointerState(nullptr), mFront(0.0f, 0.0f, 1.0f), mBlownModel(nullptr), mDontTurn(false) {
}

inline void PackunPetit::initStarPointer(const char* hitSensor) {
    Vec3f offs;
    offs.set(0.0f);
    MR::initStarPointerTargetAtJoint(this, hitSensor, cNum_initStarPointer_f1, offs);
}

void PackunPetit::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("PackunPetit", nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensor(3);
    MR::addHitSensorAtJointMapObj(this, "body", "Head", 8, cNum_init_f1, Vec3f(cNum_init_f2, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemyAttack(this, "attack", "Head", 8, cNum_init_f3, Vec3f(cNum_init_f4, 0.0f, 0.0f));
    MR::addHitSensorMapObj(this, "stalk", 8, cNum_init_f5, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    MR::initLightCtrl(this);
    MR::initShadowFromCSV(this, "Shadow");
    MR::calcGravity(this);
    MR::useStageSwitchWriteDead(this, rIter);
    MR::declareCoin(this, 1);
    MR::declareStarPiece(this, 6);
    initStarPointer("Head");
    mScaleController = new AnimScaleController(nullptr);
    mStarPointerState = new WalkerStateBindStarPointer(this, mScaleController);
    MR::getPlacementArg0NoInit(rIter, &mDontTurn);
    MR::calcFrontVec(&mFront, this);
    initBlowModel();
    initNerve(GET_NERVE(PackunPetit, PackunPetitNrvWait));
    makeActorAppeared();
}

void PackunPetit::initAfterPlacement() {
    MR::initEffectAfterPlacement(this);
}

void PackunPetit::exeNonActive() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensors(this);
        MR::offCalcAnim(this);
        MR::offCalcShadow(this, "JawA");
    }

    if (MR::isNearPlayerAnyTime(this, cNum_exeNonActive_isNearPlayerAnyTime_1)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvWait));
    }
}

void PackunPetit::endNonActive() {
    MR::validateHitSensors(this);
    MR::onCalcAnim(this);
    MR::onCalcShadow(this, "JawA");
}

void PackunPetit::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBckWithInterpole(this, "Wait", 60);
    }

    if (MR::isNearPlayer(this, cNum_exeWait_isNearPlayer_1)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvFind));
    } else {
        tryNonActive();
    }
}

void PackunPetit::exeFind() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Suspect");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvThreat));
    }
}

void PackunPetit::exeThreat() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Threat");
    }

    MR::startLevelSound(this, "SE_EM_LV_PACKUNPETIT_THREAT");

    if (!mDontTurn) {
        if (tryTurn()) {
            return;
        }

        Vec3f* pos = MR::getPlayerPos();
        MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFront, *pos, cNum_exeThreat_f1);
    }

    if (MR::isGreaterStep(this, cStep_exeThreat_1) && MR::isNearPlayer(this, cNum_exeThreat_isNearPlayer_1) && (MR::isFaceToPlayerHorizontalDegree(this, mFront, cNum_exeThreat_f2) || mDontTurn)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvAttackStart));
    } else if (!MR::isNearPlayer(this, cNum_exeThreat_isNearPlayer_2)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvWait));
    }
}

void PackunPetit::exeTurn() {
    if (MR::isFirstStep(this)) {
        if (isNerve(GET_NERVE(PackunPetit, PackunPetitNrvLeftTurn))) {
            MR::startBck(this, "TurnLeft");
        } else {
            MR::startBck(this, "TurnRight");
        }
    }

    f32 step = (cNum_exeTurn_f1 - getNerveStep());

    if (step < cNum_exeTurn_f2) {
        step = cNum_exeTurn_f3;
    }
    Vec3f* pos = MR::getPlayerPos();
    MR::turnDirectionToTargetUseGroundNormalDegree(this, &mFront, *pos, step);
    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvThreat));
    }
}

void PackunPetit::exeAttackStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Attack");
    }

    if (MR::isStep(this, cStep_exeAttackStart_1)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvAttack));
    }
}

void PackunPetit::exeAttack() {
    if (MR::isStep(this, cStep_exeAttack_1)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvAttackBack));
    }
}

void PackunPetit::exeAttackBack() {
    if (MR::isBckStopped(this)) {
        if (!tryTurn()) {
            selectNrvWait();
        }
    }
}

void PackunPetit::exeHitWaitForAttack() {
    if (MR::isFirstStep(this) && !MR::isBckPlaying(this, "Attack")) {
        MR::startBck(this, "Impact");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvHit));
    }
}

void PackunPetit::exeHit() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Hit");
    }

    if (MR::isBckStopped(this) && !tryTurn()) {
        selectNrvWait();
    }
}

void PackunPetit::exeTrampleDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Press");
        MR::startSound(this, "SE_EM_STOMPED_S");
        MR::invalidateHitSensors(this);
    }

    if (MR::isBckStopped(this)) {
        kill();
    }
}

void PackunPetit::exePunchDown() {
    if (MR::isFirstStep(this)) {
        MR::invalidateHitSensor(this, "body");
        MR::invalidateHitSensor(this, "attack");
        mBlownModel->makeActorAppeared();
        MR::copyJointPos(this, "Head", &mBlownModel->mPosition);
        MR::startBck(mBlownModel, "BlowHead");
        MR::startBck(this, "Blow");
        MR::startBlowHitSound(this);
    }

    mBlownModel->mVelocity.scaleAdd(cNum_exePunchDown_f1, mGravity, mBlownModel->mVelocity);

    if (!MR::isHiddenModel(mBlownModel) && (MR::isStep(this, cStep_exePunchDown_1) || MR::checkStrikeBallToMap(mBlownModel->mPosition, cNum_exePunchDown_f2))) {
        MR::emitEffect(mBlownModel, "Death");
        MR::appearStarPiece(this, mBlownModel->mPosition, 3, cNum_exePunchDown_f3, cNum_exePunchDown_f4, false);
        MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
        MR::hideModel(mBlownModel);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        kill();
    }
}

void PackunPetit::exeSwoonStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SwoonStart");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvSwoon));
    }
}

void PackunPetit::exeSwoon() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Swoon");
    }

    MR::startLevelSound(this, "SE_EM_LV_SWOON_S");

    if (MR::isStep(this, cStep_exeSwoon_1)) {
        selectNrvWait();
    }
}

void PackunPetit::exeSwoonToThreat() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "SwoonToThreat");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvThreat));
    }
}

void PackunPetit::exePointerSwoon() {
    if (MR::updateActorState(this, mStarPointerState)) {
        selectNrvWait();
    }
}

void PackunPetit::endPointerSwoon() {
    mStarPointerState->kill();
}

void PackunPetit::kill() {
    if (!MR::isDead(mBlownModel)) {
        if (!MR::isHiddenModel(mBlownModel)) {
            MR::emitEffect(mBlownModel, "Death");
            MR::appearStarPiece(this, mBlownModel->mPosition, 3, cNum_kill_f1, cNum_kill_f2, false);
            MR::startSound(mBlownModel, "SE_OJ_STAR_PIECE_BURST");
        }

        mBlownModel->kill();
    } else {
        if (!MR::isValidSwitchDead(this)) {
            MR::appearCoinPop(this, mPosition, 1);
        }
    }

    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
    }

    MR::emitEffect(this, "Death");
    MR::startSound(this, "SE_EM_EXPLODE_S");
    LiveActor::kill();
}

void PackunPetit::calcAndSetBaseMtx() {
    Vec3f up;
    MR::calcUpVec(&up, this);
    Pos3f mtx;
    mtx.identity();
    MR::makeMtxUpFrontPos(&mtx, up, mFront, mPosition);
    MR::setBaseTRMtx(this, mtx);
    Vec3f scale = mScaleController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void PackunPetit::control() {
    mScaleController->updateNerve();
    tryPointerSwoon();
}

void PackunPetit::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        bool isTrampleOrPunch =
            isNerve(GET_NERVE(PackunPetit, PackunPetitNrvTrampleDown)) || isNerve(GET_NERVE(PackunPetit, PackunPetitNrvPunchDown));
        if (!isTrampleOrPunch && !isNerve(GET_NERVE(PackunPetit, PackunPetitNrvPointerSwoon)) && MR::isSensorEnemyAttack(pSender)) {
            if (MR::sendMsgEnemyAttackStrong(pReceiver, pSender) && !MR::isPlayerHipDropFalling()) {
                MR::emitEffectHitBetweenSensors(this, pSender, pReceiver, 0.0f, nullptr);
                setNerve(GET_NERVE(PackunPetit, PackunPetitNrvHitWaitForAttack));
                return;
            }
            MR::sendMsgPush(pReceiver, pSender);
            return;
        }
    }

    if (isNerve(GET_NERVE(PackunPetit, PackunPetitNrvPointerSwoon)) && MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
        return;
    }

    if (!MR::isSensorEnemy(pReceiver)) {
        return;
    }

    MR::sendMsgPush(pReceiver, pSender);
}

bool PackunPetit::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    bool isTrampleOrPunch = isNerve(GET_NERVE(PackunPetit, PackunPetitNrvTrampleDown)) || isNerve(GET_NERVE(PackunPetit, PackunPetitNrvPunchDown));

    if (isTrampleOrPunch) {
        return false;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvSwoonStart));
        return true;
    }

    bool isWaitOrHit = isNerve(GET_NERVE(PackunPetit, PackunPetitNrvHitWaitForAttack)) || isNerve(GET_NERVE(PackunPetit, PackunPetitNrvHit));

    if (isWaitOrHit) {
        return false;
    }

    if (MR::isMsgInvincibleAttack(msg) || MR::isMsgFireBallAttack(msg)) {
        punchDown(pSender, pReceiver);
        return true;
    }

    if (isNerve(GET_NERVE(PackunPetit, PackunPetitNrvAttack))) {
        return false;
    }

    if (MR::isMsgPlayerTrample(msg)) {
        MR::tryRumbleDefaultHit(this, 0);
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvTrampleDown));
        return true;
    }

    if (MR::isMsgPlayerHipDrop(msg)) {
        MR::tryRumbleDefaultHit(this, 0);
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvTrampleDown));
        return true;
    }

    if (MR::isMsgPlayerHitAll(msg)) {
        MR::stopSceneForDefaultHit(5);
        punchDown(pSender, pReceiver);
        return true;
    }

    return false;
}

bool PackunPetit::receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    bool isTrampleOrPunch = isNerve(GET_NERVE(PackunPetit, PackunPetitNrvTrampleDown)) || isNerve(GET_NERVE(PackunPetit, PackunPetitNrvPunchDown));

    if (isTrampleOrPunch) {
        return false;
    }

    if (MR::isMsgToEnemyAttackBlow(msg)) {
        punchDown(pSender, pReceiver);
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvPunchDown));
        return true;
    }

    if (MR::isMsgToEnemyAttackTrample(msg)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvTrampleDown));
        return true;
    }

    return false;
}

bool PackunPetit::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    bool isSwoon = isNerve(GET_NERVE(PackunPetit, PackunPetitNrvSwoonStart)) || isNerve(GET_NERVE(PackunPetit, PackunPetitNrvSwoon));

    if (!isSwoon) {
        return false;
    }

    if (!MR::isSensorPlayer(pSender)) {
        return false;
    }

    if (MR::isMsgPlayerKick(msg)) {
        MR::stopSceneForDefaultHit(5);
        punchDown(pSender, pReceiver);
        return true;
    }

    return false;
}

void PackunPetit::initBlowModel() {
    mBlownModel = MR::createModelObjMapObjStrongLight("吹っ飛びモデル", "PackunPetitHead", nullptr);
    mBlownModel->initWithoutIter();
    MR::initShadowVolumeSphere(mBlownModel, cNum_initBlowModel_f1);
    MR::invalidateClipping(mBlownModel);
    MR::initLightCtrl(mBlownModel);
    mBlownModel->makeActorDead();
}

void PackunPetit::punchDown(HitSensor* pSender, HitSensor* pReceiver) {
    // Knocked away from the hit at 20, up at 40.
    Vec3f away;
    away.sub(pReceiver->mPosition, pSender->mPosition);
    away.orthogonalize(mGravity);
    MR::normalize(&away);

    Vec3f velocity;
    velocity.scale(cNum_punchDown_f1, away);
    velocity.scaleAdd(-cNum_punchDown_f2, mGravity, velocity);
    mBlownModel->mVelocity.set(velocity);
    setNerve(GET_NERVE(PackunPetit, PackunPetitNrvPunchDown));
}

void PackunPetit::selectNrvWait() {
    if (!MR::isNearPlayer(this, cNum_selectNrvWait_isNearPlayer_1)) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvWait));
    } else if (isNerve(GET_NERVE(PackunPetit, PackunPetitNrvSwoon))) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvSwoonToThreat));
    } else {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvThreat));
    }
}

bool PackunPetit::tryNonActive() {
    if (MR::isNearPlayerAnyTime(this, cNum_tryNonActive_isNearPlayerAnyTime_1)) {
        return false;
    }

    setNerve(GET_NERVE(PackunPetit, PackunPetitNrvNonActive));
    return true;
}

bool PackunPetit::tryTurn() {
    if (mDontTurn) {
        return false;
    }

    if (MR::isFaceToPlayerHorizontalDegree(this, mFront, cNum_tryTurn_f1)) {
        return false;
    }
    Vec3f playerDist;
    playerDist.sub(*MR::getPlayerPos(), mPosition);
    Vec3f sideVec;
    MR::calcSideVec(&sideVec, this);

    if (playerDist.dot(sideVec) > 0.0f) {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvLeftTurn));
    } else {
        setNerve(GET_NERVE(PackunPetit, PackunPetitNrvRightTurn));
    }

    return true;
}

bool PackunPetit::tryPointerSwoon() {
    if (isNerve(GET_NERVE(PackunPetit, PackunPetitNrvPointerSwoon))) {
        return false;
    }

    bool isSwoon = isNerve(GET_NERVE(PackunPetit, PackunPetitNrvSwoonStart)) || isNerve(GET_NERVE(PackunPetit, PackunPetitNrvSwoon));

    if (isSwoon) {
        return false;
    }

    bool isTrampleOrPunch = isNerve(GET_NERVE(PackunPetit, PackunPetitNrvTrampleDown)) || isNerve(GET_NERVE(PackunPetit, PackunPetitNrvPunchDown));

    if (isTrampleOrPunch) {
        return false;
    }

    if (!mStarPointerState->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(PackunPetit, PackunPetitNrvPointerSwoon));
    return true;
}
