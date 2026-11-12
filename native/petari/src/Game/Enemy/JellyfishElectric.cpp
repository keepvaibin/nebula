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
#include "Game/System/LumaCore.hpp"

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
    lumacore::setClass(this, lumacore::kClassId< JellyfishElectric >);
    mController = nullptr;
    mBindStarPtr = nullptr;
    mWaveStep = 0;
    mFront.set(0.0f, 0.0f, 1.0f);
    mIsDanger = false;
    mIsConnectedRail = 0;
    mRailSpeed = 5.0f;
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
    sensorOffs.set(0.0f, -60.0f, 0.0f);
    MR::addHitSensorEnemy(this, "body", 8, 350.0f, sensorOffs);
    initBinder(400.0f, 0.0f, 0);
    MR::offBind(this);
    mController = new AnimScaleController(nullptr);
    mController->setParamTight();
    initEffectKeeper(2, nullptr);
    MR::addEffectHitNormal(this, nullptr);
    initSound(4, false);
    MR::initStarPointerTarget(this, 400.0f, Vec3f(0.0f, 0.0f, 0.0f));
    mController = new AnimScaleController(nullptr);
    mBindStarPtr = new WalkerStateBindStarPointer(this, mController);
    MR::initShadowVolumeSphere(this, 230.0f);
    MR::declareCoin(this, 1);

    if (MR::isConnectedWithRail(rIter)) {
        mIsConnectedRail = true;
        initRailRider(rIter);
        MR::initAndSetRailClipping(&mClipCenter, this, 100.0f, 500.0f);
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
    MR::requestPointLight(this, Vec3f(mPosition), ::sPointLightColor, 0.9999f, -1);
    MR::changeShowModelFlagSyncNearClipping(this, 700.0f);
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
    f32 turnDecay = (1.0f - (getNerveStep() / 280.0f));

    if (isNerve(GET_NERVE(JellyfishElectric, JellyfishElectricNrvWaitWithLeftTurn))) {
        turnDirection = 1.0f;
    } else {
        turnDirection = -1.0f;
    }

    MR::rotateVecDegree(&mFront, -mGravity, (turnDirection * (0.2f * turnDecay)));
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
    if (MR::isStep(this, 280)) {
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
