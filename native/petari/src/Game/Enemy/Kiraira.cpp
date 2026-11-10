#include "Game/Enemy/Kiraira.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/KirairaChain.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 120.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 4000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeDead_f1, "exeDead.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_f1, "exeRecover.f1", 90.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeRecover_f2, "exeRecover.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drift_f1, "drift.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_drift_f2, "drift.f2", 45.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_drift_f3, "drift.f3", 2.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_attackSensor_turnDirectionToTargetDegree_1, "attackSensor.turnDirectionToTargetDegree.1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_receiveOtherMsg_turnDirectionToTargetDegree_1, "receiveOtherMsg.turnDirectionToTargetDegree.1", 20.0f);

namespace {
    NEBULA_TUNABLE(f32, sTurnRate, 2.8f);
    NEBULA_TUNABLE(s32, sStepToStare, 5);
    NEBULA_TUNABLE(f32, sRailGoalOffset, 50.0f);
    NEBULA_TUNABLE(s32, sIntervalToExplode, 10);
    NEBULA_TUNABLE(f32, sEyeHitSize, 600.0f);
    NEBULA_TUNABLE(f32, sMindEyeHitSize, 1500.0f);
    NEBULA_TUNABLE(f32, sCameraShakeDistance, 3000.0f);
    static const f32 sNoCameraShakeDistance = FLOAT_MAX;
    static const s32 sStepToRecoverSign = NebulaFrameClock::stepsOfSeconds(5);
    static const s32 sStepToRecover = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(f32, sEyeSensorInRadius, 1500.0f);
    NEBULA_TUNABLE(f32, sEyeSensorOutRadius, 2000.0f);
};  // namespace

namespace NrvKiraira {
    NEW_NERVE(KirairaNrvWait, Kiraira, Wait);
    NEW_NERVE(KirairaNrvFaceToMario, Kiraira, FaceToMario);
    NEW_NERVE(KirairaNrvFaceToMarioAndStare, Kiraira, FaceToMarioAndStare);
    NEW_NERVE(KirairaNrvExplode, Kiraira, Explode);
    NEW_NERVE(KirairaNrvBeExploded, Kiraira, BeExploded);
    NEW_NERVE(KirairaNrvDead, Kiraira, Dead);
    NEW_NERVE(KirairaNrvRecoverSign, Kiraira, RecoverSign);
    NEW_NERVE(KirairaNrvRecover, Kiraira, Recover);
};  // namespace NrvKiraira

Kiraira::Kiraira(const char* pName)
    : LiveActor(pName), mRailCoordSpeed(5.0f), mRailVec(0.0f, 0.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f), mDriftStep(0.0f), mIsRail(false),
      mIsForceDetonated(false), mEyesOpen(true), mSharedGroup(nullptr), mChain(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< Kiraira >);
}

void Kiraira::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Kiraira", nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensor(3);
    MR::addHitSensorEnemy(this, "body", 8, cNum_init_f1, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEye(this, "eye", 16, ::sEyeHitSize, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorEye(this, "mind_eye", 8, ::sMindEyeHitSize, Vec3f(0.0f, 0.0f, 0.0f));
    initEffectKeeper(0, nullptr);
    initSound(6, false);
    MR::initShadowVolumeSphere(this, cNum_init_f2);
    MR::setShadowDropLength(this, nullptr, cNum_init_f3);
    MR::onCalcShadowOneTime(this, nullptr);
    MR::initLightCtrl(this);
    const char* objName;
    MR::getObjectName(&objName, rIter);
    mIsRail = MR::isEqualString(objName, "KirairaRail");
    if (mIsRail) {
        initRailRider(rIter);
        MR::initAndSetRailClipping(&mRailVec, this, cNum_init_f4, cNum_init_f5);
        MR::getPlacementArg0NoInit(rIter, &mRailCoordSpeed);
        MR::setRailCoordSpeed(this, mRailCoordSpeed);
    }
    initNerve(GET_NERVE(Kiraira, KirairaNrvWait));
    MR::useStageSwitchWriteB(this, rIter);
    MR::setGroupClipping(this, rIter, 16);
    mSharedGroup = MR::joinToGroupArray(this, rIter, "キライラ軍団", 16);
    MR::addToAttributeGroupSearchTurtle(this);
    if (!mIsRail) {
        mChain = new KirairaChain(this);
        mChain->initWithoutIter();
    }
    MR::calcFrontVec(&mFront, this);
    makeActorAppeared();
}

void Kiraira::calcAndSetBaseMtx() {
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, mFront);
    MR::setBaseTRMtx(this, mtx);
}

void Kiraira::exeWait() {
    if (MR::isFirstStep(this)) {
        if (MR::isValidSwitchB(this)) {
            MR::offSwitchB(this);
        }
        closeEyes();
        MR::startBrk(this, "Wait");
    }
    if (MR::isNearPlayer(this, ::sEyeSensorInRadius)) {
        setNerve(GET_NERVE(Kiraira, KirairaNrvFaceToMario));
    } else {
        drift();
    }
}

void Kiraira::exeFaceToMario() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Wait");
    }
    if (!MR::isNearPlayer(this, ::sEyeSensorOutRadius)) {
        setNerve(GET_NERVE(Kiraira, KirairaNrvWait));
    } else {
        MR::turnDirectionToPlayerDegree(this, &mFront, ::sTurnRate);
        if (MR::isFaceToPlayerHorizontalDegree(this, mFront, ::sTurnRate)) {
            MR::startBck(this, "Stop");
            setNerve(GET_NERVE(Kiraira, KirairaNrvFaceToMarioAndStare));
        } else {
            drift();
        }
    }
}

void Kiraira::exeFaceToMarioAndStare() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "Wait");
    }
    if (MR::isStep(this, ::sStepToStare)) {
        openEyes();
    }
    if (!MR::isNearPlayer(this, ::sEyeSensorOutRadius)) {
        setNerve(GET_NERVE(Kiraira, KirairaNrvWait));
    } else {
        MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), ::sTurnRate);
        drift();
    }
}

void Kiraira::exeBeExploded() {
    if (MR::isFirstStep(this)) {
        mIsForceDetonated = true;
    }
    if (MR::isStep(this, ::sIntervalToExplode)) {
        explode();
    }
}

void Kiraira::exeDead() {
    if (MR::isFirstStep(this)) {
        if (MR::isValidSwitchB(this)) {
            MR::onSwitchB(this);
        }
        if (mIsRail) {
            MR::startBck(this, "RailDown");
        } else {
            MR::startBck(this, "Down");
        }
        MR::startBrk(this, "Down");
        MR::setShadowVolumeSphereRadius(this, nullptr, cNum_exeDead_f1);
    }
    if (mSharedGroup != nullptr) {
        mSharedGroup->sendMsgToGroupMember(ACTMES_GROUP_ATTACK, getSensor("body"), "body");
    }
    if (MR::isStep(this, ::sStepToRecoverSign)) {
        setNerve(GET_NERVE(Kiraira, KirairaNrvRecoverSign));
    } else {
        drift();
    }
}

void Kiraira::exeRecoverSign() {
    if (MR::isFirstStep(this)) {
        if (mIsRail) {
            MR::startBck(this, "RailRevivalStart");
        } else {
            MR::startBck(this, "RevivalStart");
        }
        MR::startBrk(this, "RevivalStart");
        MR::validateClipping(this);
    }
    if (MR::isStep(this, ::sStepToRecover)) {
        setNerve(GET_NERVE(Kiraira, KirairaNrvRecover));
    } else {
        drift();
    }
}

void Kiraira::exeRecover() {
    if (MR::isFirstStep(this)) {
        if (mIsRail) {
            MR::startBck(this, "Revival");
        } else {
            MR::startBck(this, "RailRevival");
        }
        MR::startBrk(this, "Revival");
        MR::startSound(this, "SE_OJ_KIRAIRA_RECOVER");

        if (MR::isNearPlayer(this, ::sEyeSensorOutRadius)) {
            openEyes();
        }
        MR::setShadowVolumeSphereRadius(this, nullptr, cNum_exeRecover_f1);
        mIsForceDetonated = false;
    }
    if (MR::isNearPlayer(this, ::sEyeSensorOutRadius)) {
        MR::turnDirectionToTargetDegree(this, &mFront, *MR::getPlayerPos(), cNum_exeRecover_f2);
    }
    if (MR::isBckStopped(this)) {
        if (mChain != nullptr) {
            mChain->mIsCut = false;
        }
        if (!MR::isNearPlayer(this, ::sEyeSensorOutRadius)) {
            setNerve(GET_NERVE(Kiraira, KirairaNrvWait));
        } else {
            setNerve(GET_NERVE(Kiraira, KirairaNrvFaceToMarioAndStare));
        }
    }
}

void Kiraira::exeExplode() {
    if (MR::isFirstStep(this)) {
        explode();
    }
}

bool Kiraira::isDown() const {
    return isNerve(GET_NERVE(Kiraira, KirairaNrvDead)) || isNerve(GET_NERVE(Kiraira, KirairaNrvRecoverSign)) ||
           isNerve(GET_NERVE(Kiraira, KirairaNrvRecover));
}

void Kiraira::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isDown() && (pSender == getSensor("mind_eye")) && pReceiver->mType == ATYPE_JET_TURTLE) {
        MR::turnDirectionToTargetDegree(this, &mFront, pReceiver->mPosition, cNum_attackSensor_turnDirectionToTargetDegree_1);
        openEyes();
    }
    if (!isDown() && MR::isSensorEnemy(pSender) && MR::isSensorPlayerOrRide(pReceiver)) {
        if (MR::sendMsgEnemyAttackExplosion(pReceiver, pSender)) {
            setNerve(GET_NERVE(Kiraira, KirairaNrvExplode));
            return;
        }
    }
    if (isDown() && (pSender == getSensor("body")) && MR::isSensorPlayerOrRide(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Kiraira::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isDown()) {
        return false;
    }
    if (MR::isMsgJetTurtleAttack(msg) && MR::isSensorEnemy(pReceiver)) {
        mIsForceDetonated = true;
        setNerve(GET_NERVE(Kiraira, KirairaNrvExplode));
        return true;
    }
    if (MR::isSensorEnemy(pReceiver) && MR::isMsgPlayerHitAll(msg)) {
        if (MR::sendMsgEnemyAttackExplosion(pSender, pReceiver)) {
            setNerve(GET_NERVE(Kiraira, KirairaNrvExplode));
            return true;
        }
    }
    if (MR::isSensorEnemy(pReceiver) && MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(Kiraira, KirairaNrvExplode));
        return true;
    }

    return false;
}

bool Kiraira::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (!isDown() && MR::isMsgExplosionAttack(msg) && MR::isSensorEnemy(pReceiver)) {
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(Kiraira, KirairaNrvBeExploded));
        return true;
    }

    return false;
}

bool Kiraira::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (isDown()) {
        return false;
    }
    if (msg == ACTMES_GROUP_ATTACK) {
        MR::turnDirectionToTargetDegree(this, &mFront, pSender->mPosition, cNum_receiveOtherMsg_turnDirectionToTargetDegree_1);
        openEyes();
        return true;
    }

    return false;
}

void Kiraira::openEyes() {
    if (!mEyesOpen) {
        MR::startBtp(this, "EyeOpen");
        MR::emitEffect(this, "SearchLight");
        MR::startSound(this, "SE_OJ_KIRAIRA_STARE");
    }
    mEyesOpen = true;
}

void Kiraira::closeEyes() {
    if (mEyesOpen) {
        MR::startBtp(this, "EyeClose");
        MR::deleteEffect(this, "SearchLight");
    }
    mEyesOpen = false;
}

void Kiraira::drift() {
    if (mIsRail) {
        bool dead, alive;
        alive = dead = true;

        if (!isNerve(GET_NERVE(Kiraira, KirairaNrvDead)) && !isNerve(GET_NERVE(Kiraira, KirairaNrvRecoverSign))) {
            alive = false;
        }

        if (!alive && !isNerve(GET_NERVE(Kiraira, KirairaNrvRecover))) {
            dead = false;
        }

        if (!dead) {
            driftOnRail();
        } else if (isNerve(GET_NERVE(Kiraira, KirairaNrvRecover))) {
            mPosition.set(MR::getRailPos(this));
        } else if (mChain != nullptr) {
            mPosition.set(*mChain->getCutPos());
        }
    } else {
        mVelocity.scale(cNum_drift_f1 * MR::sinDegree(cNum_drift_f2 + cNum_drift_f3 * mDriftStep), mGravity);
    }

    mDriftStep++;
}

void Kiraira::driftOnRail() {
    MR::moveCoordAndFollowTrans(this, mRailCoordSpeed);
    if (MR::isRailReachedNearGoal(this, ::sRailGoalOffset)) {
        MR::reverseRailDirection(this);
    }
    MR::startLevelSound(this, "SE_OJ_LV_KIRAIRA_CHAIN");
}

void Kiraira::explode() {
    closeEyes();
    MR::startRumbleWithShakeCameraStrong(this, "強", "中", ::sCameraShakeDistance, ::sNoCameraShakeDistance);
    if (mIsForceDetonated) {
        MR::sendMsgExplosionToNearActor(getSensor("eye"), -1.0f);
    }
    MR::emitEffect(this, "Explosion");
    MR::startSound(this, "SE_OJ_KIRAIRA_EXPLODE");
    if (mChain != nullptr) {
        mChain->mIsCut = true;
    }
    setNerve(GET_NERVE(Kiraira, KirairaNrvDead));
}
