#include "Game/Enemy/JellyfishElectric.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/Spine.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/Color.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LightUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_JellyfishElectric_f1, "JellyfishElectric.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 350.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 230.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.9999f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_waitTurn_f1, "waitTurn.f1", 280.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_waitTurn_f2, "waitTurn.f2", 0.2f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_selectNerveAfterWait_1, "selectNerveAfterWait.1", 280);

namespace NrvJellyfishElectric {
    NEW_NERVE(JellyfishElectricNrvWait, JellyfishElectric, Wait);
    NEW_NERVE(JellyfishElectricNrvWaitWithRightTurn, JellyfishElectric, WaitWithRightTurn);
    NEW_NERVE(JellyfishElectricNrvWaitWithLeftTurn, JellyfishElectric, WaitWithLeftTurn);
    NEW_NERVE(JellyfishElectricNrvDamage, JellyfishElectric, Damage);
    NEW_NERVE(JellyfishElectricNrvDeath, JellyfishElectric, Death);
    NEW_NERVE(JellyfishElectricNrvAttack, JellyfishElectric, Attack);
    NEW_NERVE(JellyfishElectricNrvRailGoal, JellyfishElectric, RailGoal);
    NEW_NERVE_ONEND(JellyfishElectricNrvPointerSwoon, JellyfishElectric, PointerSwoon, PointerSwoon);

};  // namespace NrvJellyfishElectric

namespace {
    static Color8 sPointLightColor(0x9C, 0xFF, 0xFF, 0xFF);
};  // namespace

JellyfishElectric::JellyfishElectric(const char* pName) : LiveActor(pName) {
    mController = nullptr;
    mBindStarPtr = nullptr;
    mWaveStep = 0;
    mFront.set(0.0f, 0.0f, 1.0f);
    mIsDanger = false;
    mIsConnectedRail = 0;
    mRailSpeed = cNum_JellyfishElectric_f1;
    mRailGoalWaitStep = NebulaFrameClock::stepsOfSeconds(1);
    mIsRailTurnBack = 0;
    mClipCenter.set(0.0f, 0.0f, 0.0f);
}

void JellyfishElectric::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("JellyfishElectric", nullptr, false);
    MR::connectToSceneEnemy(this);
    MR::addToAttributeGroupSearchTurtle(this);
    initHitSensor(1);
    Vec3f sensorOffs;
    sensorOffs.set(0.0f, -cNum_init_f1, 0.0f);
    MR::addHitSensorEnemy(this, "body", 8, cNum_init_f2, sensorOffs);
    initBinder(cNum_init_f3, 0.0f, 0);
    MR::offBind(this);
    mController = new AnimScaleController(nullptr);
    mController->setParamTight();
    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    MR::initStarPointerTarget(this, cNum_init_f4, Vec3f(0.0f, 0.0f, 0.0f));
    mController = new AnimScaleController(nullptr);
    mBindStarPtr = new WalkerStateBindStarPointer(this, mController);
    MR::initShadowVolumeSphere(this, cNum_init_f5);
    MR::declareCoin(this, 1);

    if (MR::isConnectedWithRail(rIter)) {
        mIsConnectedRail = true;
        initRailRider(rIter);
        MR::initAndSetRailClipping(&mClipCenter, this, cNum_init_f6, cNum_init_f7);
        MR::moveCoordToNearestPos(this, mPosition);
        MR::getPlacementArg0NoInit(rIter, &mRailSpeed);
        MR::getPlacementArg1NoInit(rIter, &mRailGoalWaitStep);
        s32 railTurnBack = 0;
        MR::getPlacementArg2NoInit(rIter, &railTurnBack);
        mIsRailTurnBack = railTurnBack == 1;
    }

    MR::calcFrontVec(&mFront, this);
    MR::startBrk(this, "Wait");
    initNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWait));
    makeActorAppeared();
}

void JellyfishElectric::kill() {
    MR::appearCoinPop(this, mPosition, 1);
    LiveActor::kill();
}

void JellyfishElectric::control() {
    MR::requestPointLight(this, Vec3f(mPosition), ::sPointLightColor, cNum_control_f1, -1);
    MR::changeShowModelFlagSyncNearClipping(this, cNum_control_f2);
    mController->updateNerve();

    if (!tryPointerSwoon() && !isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvDeath))) {
        if (mIsConnectedRail) {
            MR::moveCoordAndFollowTrans(this, mRailSpeed);
            if (MR::isRailReachedGoal(this)) {
                if (!isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvRailGoal))) {
                    setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvRailGoal));
                    return;
                }
            }
        }

        f32 wave = MR::sinDegree(mWaveStep + 45);
        const Vec3f& rGravity = mGravity;
        mVelocity.scale(wave, rGravity);
        mWaveStep++;
    }
}

void JellyfishElectric::calcAndSetBaseMtx() {
    Pos3f pos;
    MR::makeMtxFrontUpPos(&pos, mFront, -mGravity, mPosition);
    MR::setBaseTRMtx(this, pos);
    Vec3f scale = mController->mScale * mScale;
    MR::setBaseScale(this, scale);
}

void JellyfishElectric::exeWait() {
    if (MR::isFirstStep(this)) {
        if (!mIsDanger && !MR::isBckPlaying(this, "Wait")) {
            MR::startAllAnim(this, "Wait");
        } else {
            if (mIsDanger && !MR::isBckPlaying(this, "DangerWait")) {
                MR::startAllAnim(this, "DangerWait");
                MR::startBas(this, "DangerBrk", false, 0.0f, 0.0f);
            }
        }
    }

    selectNerveAfterWait();
}

void JellyfishElectric::exeDamage() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_EM_JELLYELEC_DAMAGE");
        MR::startAllAnim(this, "Damage");
    }

    if (MR::isBckStopped(this)) {
        mIsDanger = 1;
        MR::startAllAnim(this, "DangerWait");
        MR::startBas(this, "DangerBrk", false, 0.0f, 0.0f);
        setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWait));
    }
}

void JellyfishElectric::exeDeath() {
    if (MR::isFirstStep(this)) {
        MR::onBind(this);
        MR::invalidateHitSensors(this);
        MR::stopScene(5);
        MR::startAllAnim(this, "Death");
    }

    MR::startLevelSound(this, "SE_EM_LV_JELLYELEC_PRE_DEAD");
    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500)) || MR::isBinded(this)) {
        MR::startSound(this, "SE_EM_JELLYELEC_DEAD");
        kill();
    }
}

void JellyfishElectric::exeAttack() {
    if (MR::isFirstStep(this)) {
        MR::startAllAnim(this, "Attack");
    }

    MR::startLevelSound(this, "SE_EM_LV_JELLYELEC_ATTACK");

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWait));
    }
}

void JellyfishElectric::exeRailGoal() {
    if (MR::isStep(this, mRailGoalWaitStep)) {
        if (mIsRailTurnBack) {
            MR::moveCoordAndTransToRailPoint(this, 0);
            const Vec3f& dir = MR::getRailDirection(this);
            mFront.x = dir.x;
            mFront.y = dir.y;
            mFront.z = dir.z;
            mIsDanger = 0;
        } else {
            MR::reverseRailDirection(this);
        }

        setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWait));
    }
}

void JellyfishElectric::exePointerSwoon() {
    if (MR::isFirstStep(this)) {
        MR::deleteEffectAll(this);
    }

    MR::updateActorStateAndNextNerve(this, mBindStarPtr, GET_NERVE(JellyfishElectric, JellyfishElectricNrvWait));
}

void JellyfishElectric::exeWaitWithLeftTurn() {
    waitTurn();
    selectNerveAfterWait();
}

void JellyfishElectric::exeWaitWithRightTurn() {
    waitTurn();
    selectNerveAfterWait();
}

void JellyfishElectric::endPointerSwoon() {
    mBindStarPtr->kill();
}

void JellyfishElectric::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemy(pSender) && MR::isSensorPlayer(pReceiver)) {
        tryToAttackElectric(pReceiver, pSender);
    } else if (MR::isSensorEnemy(pReceiver) || MR::isSensorMapObj(pReceiver)) {
        if (MR::isSensorEnemy(pSender)) {
            MR::sendMsgPush(pReceiver, pSender);
        }
    }
}

bool JellyfishElectric::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgStarPieceAttack(msg)) {
        mController->startHitReaction();
        return true;
    } else if (MR::isMsgJetTurtleAttack(msg)) {
        knockOut();
        return true;
    } else if (MR::isMsgInvincibleAttack(msg)) {
        mIsDanger = 1;
        knockOut();
        return true;
    } else {
        if (MR::isMsgPlayerSpinAttack(msg)) {
            return tryToAttackElectric(pSender, pReceiver);
        }
    }

    return false;
}

bool JellyfishElectric::receiveMsgEnemyAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgExplosionAttack(msg) && !isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvDeath))) {
        knockOut();
        return true;
    }

    return false;
}

void JellyfishElectric::waitTurn() {
    f32 turnDirection;
    f32 turnDecay = (1.0f - (getNerveStep() / cNum_waitTurn_f1));

    if (isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWaitWithLeftTurn))) {
        turnDirection = 1.0f;
    } else {
        turnDirection = -1.0f;
    }

    MR::rotateVecDegree(&mFront, -mGravity, (turnDirection * (cNum_waitTurn_f2 * turnDecay)));
}

void JellyfishElectric::knockOut() {
    if (!mIsDanger) {
        setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvDamage));
    } else {
        setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvDeath));
    }
}

bool JellyfishElectric::tryToAttackElectric(HitSensor* pSender, HitSensor* pReceiver) {
    if (!isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvPointerSwoon)) && MR::sendMsgEnemyAttackElectric(pSender, pReceiver)) {
        MR::emitEffectHitBetweenSensors(this, pReceiver, pSender, 0.0f, nullptr);
        setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvAttack));
    }

    MR::sendMsgPush(pSender, pReceiver);
    return true;
}

bool JellyfishElectric::tryPointerSwoon() {
    if (isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvPointerSwoon))) {
        return false;
    }

    if (isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvDamage))) {
        return false;
    }

    if (isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvDeath))) {
        return false;
    }

    if (isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvAttack))) {
        return false;
    }

    if (isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvRailGoal))) {
        return false;
    }

    if (!mBindStarPtr->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvPointerSwoon));
    return true;
}

bool JellyfishElectric::selectNerveAfterWait() {
    if (MR::isStep(this, cStep_selectNerveAfterWait_1)) {
        s32 rand;

        if (isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWait))) {
            rand = MR::getRandom(0, 3);
        } else {
            rand = 0;
        }

        if (rand == 0) {
            setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWait));
        } else if (rand == 1) {
            setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWaitWithRightTurn));
        } else {
            setNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWaitWithLeftTurn));
        }

        return true;
    }

    return false;
}
