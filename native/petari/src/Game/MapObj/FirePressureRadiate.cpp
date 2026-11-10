#include "Game/MapObj/FirePressureRadiate.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_FirePressureRadiate_f1, "FirePressureRadiate.f1", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 70.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 51.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f2, "updateHitSensor.f2", 50.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exePrepareToRadiate_1, "exePrepareToRadiate.1", 34);
NEBULA_TUNABLE_NAMED(s32, cStep_exeRadiate_1, "exeRadiate.1", 25);
NEBULA_TUNABLE_NAMED(s32, cStep_exeRadiateMargin_1, "exeRadiateMargin.1", 50);

namespace NrvFirePressureRadiate {
    NEW_NERVE(FirePressureRadiateNrvRelax, FirePressureRadiate, Relax);
    NEW_NERVE(FirePressureRadiateNrvSyncWait, FirePressureRadiate, SyncWait);
    NEW_NERVE(FirePressureRadiateNrvWait, FirePressureRadiate, Wait);
    NEW_NERVE(FirePressureRadiateNrvPrepareToRadiate, FirePressureRadiate, PrepareToRadiate);
    NEW_NERVE(FirePressureRadiateNrvRadiate, FirePressureRadiate, Radiate);
    NEW_NERVE(FirePressureRadiateNrvRadiateMargin, FirePressureRadiate, RadiateMargin);
};  // namespace NrvFirePressureRadiate

FirePressureRadiate::FirePressureRadiate(const char* pName) : LiveActor(pName) {
    mJointController = nullptr;
    mCannonRotation = 0.0f;
    mWaitTime = NebulaFrameClock::stepsOfSeconds(5);
    mShootTime = NebulaFrameClock::stepsOfSeconds(5);
    mRadiateStep = -1;
    mRadiateLength = 0.0f;
    mRadiateMaxLength = cNum_FirePressureRadiate_f1;
    mGroup = nullptr;
    mIsGroupLeader = 0;
    mRadiateMtx.identity();
}

void FirePressureRadiate::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("FirePressure", nullptr, false);
    MR::connectToSceneNoShadowedMapObjStrongLight(this);
    initHitSensor(3);
    MR::addHitSensorMapObj(this, "body", 8, cNum_init_f1, Vec3f(0.0f, cNum_init_f2, 0.0f));
    MR::addHitSensorAtJointMapObj(this, "cannon", "Cannon1", 8, cNum_init_f3, Vec3f(cNum_init_f4, 0.0f, 0.0f));
    MR::addHitSensorCallbackEnemyAttack(this, "radiate", 8, cNum_init_f5);
    MR::invalidateHitSensor(this, "radiate");
    initEffectKeeper(0, nullptr);
    MR::setEffectHostMtx(this, "Fire", mRadiateMtx.mMtx);
    MR::setEffectHostMtx(this, "FireInd", mRadiateMtx.mMtx);
    initSound(4, false);
    MR::getPlacementArg0NoInit(rIter, &mCannonRotation);
    MR::getPlacementArg1NoInit(rIter, &mWaitTime);
    MR::getPlacementArg2NoInit(rIter, &mShootTime);
    mJointController = MR::createJointDelegatorWithNullChildFunc(this, &FirePressureRadiate::calcJointCannon, "Cannon1");
    MR::initJointTransform(this);
    MR::calcGravity(this);
    MR::setGroupClipping(this, rIter, 16);
    mGroup = MR::joinToGroupArray(this, rIter, "ファイアプレッシャー（放射）軍団", 16);

    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::tryRegisterDemoActionFunctor(this, MR::Functor(this, &FirePressureRadiate::startRelax), nullptr);
    }

    if (MR::useStageSwitchWriteA(this, rIter)) {
        MR::listenStageSwitchOnOffA(this, MR::Functor(this, &FirePressureRadiate::startWait), MR::Functor(this, &FirePressureRadiate::startRelax));
        initNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvRelax));
    } else {
        initNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvWait));
    }

    MR::useStageSwitchSleep(this, rIter);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void FirePressureRadiate::initAfterPlacement() {
    if (mGroup != nullptr) {
        FirePressureRadiate* first = static_cast< FirePressureRadiate* >(MR::getGroupFromArray(this)->getActor(0));

        for (u16 i = 1; i < MR::getGroupFromArray(this)->getObjNum(); i++) {
            FirePressureRadiate* cur = static_cast< FirePressureRadiate* >(MR::getGroupFromArray(this)->getActor(i));

            if (first->mWaitTime <= cur->mWaitTime) {
                first = cur;
            }
        }

        mIsGroupLeader = first == this;
    }
}

void FirePressureRadiate::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mJointController->registerCallBack();
}

void FirePressureRadiate::exeRelax() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "FireShotStart");
        MR::setBckFrame(this, 0.0f);
        MR::forceDeleteEffectAll(this);
        MR::invalidateHitSensor(this, "radiate");
    }
}

void FirePressureRadiate::exeSyncWait() {
    if (mIsGroupLeader) {
        if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
            mGroup->sendMsgToGroupMember(ACTMES_GROUP_MOVE_START, getSensor("body"), "body");
        }
    }
}

void FirePressureRadiate::exePrepareToRadiate() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "FireShotStart");
    }

    if (MR::isStep(this, cStep_exePrepareToRadiate_1)) {
        setNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvRadiate));
    }
}

void FirePressureRadiate::exeRadiate() {
    MR::startLevelSound(this, "SE_OJ_LV_F_PRESSURE_RADIATE");

    if (MR::isBckOneTimeAndStopped(this)) {
        calcRadiateEffectMtx();
        MR::startBck(this, "FireShot");
    }

    if (MR::isStep(this, cStep_exeRadiate_1)) {
        MR::validateHitSensor(this, "radiate");
        mRadiateStep = 0;
    }

    if (MR::isGreaterEqualStep(this, mShootTime)) {
        setNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvRadiateMargin));
    }
}

void FirePressureRadiate::exeRadiateMargin() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "FireShotEnd");
    }

    if (MR::isStep(this, cStep_exeRadiateMargin_1)) {
        MR::invalidateHitSensor(this, "radiate");

        if (mGroup != nullptr) {
            setNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvSyncWait));
        } else {
            setNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvWait));
        }

        if (mIsGroupLeader) {
            mGroup->sendMsgToGroupMember(ACTMES_GROUP_MOVE_STOP, getSensor("body"), "body");
        }
    }
}

void FirePressureRadiate::exeWait() {
    if (MR::isStep(this, mWaitTime)) {
        setNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvPrepareToRadiate));
    }
}

void FirePressureRadiate::control() {
    if (mRadiateStep != -1) {
        if (mRadiateStep < 70) {
            f32 liner = MR::getLinerValue(mRadiateStep / cNum_control_f1, cNum_control_f2, mRadiateMaxLength, 1.0f);
            mRadiateLength = liner;
            mRadiateStep = mRadiateStep + 1;
        } else {
            mRadiateStep = -1;
            mRadiateLength = mRadiateMaxLength;
        }
    }
}

void FirePressureRadiate::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorEnemyAttack(pSender) && MR::isSensorPlayerOrRide(pReceiver)) {
        MR::sendMsgEnemyAttackFire(pReceiver, pSender);
    } else if (MR::isSensorMapObj(pSender) && (MR::isSensorPlayer(pReceiver) || MR::isSensorEnemy(pReceiver))) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool FirePressureRadiate::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_GROUP_MOVE_STOP) {
        setNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvSyncWait));

        return true;
    } else if (msg == ACTMES_GROUP_MOVE_START) {
        setNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvWait));

        return true;
    }

    return false;
}

void FirePressureRadiate::startWait() {
    if (isNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvWait))) {
        setNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvWait));
    }
}

void FirePressureRadiate::startRelax() {
    if (!isNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvRelax))) {
        setNerve(GET_NERVE(FirePressureRadiate, FirePressureRadiateNrvRelax));
    }
}

void FirePressureRadiate::updateHitSensor(HitSensor* pSensor) {
    Vec3f direction;
    mRadiateMtx.getXDir(direction);
    Vec3f position;
    mRadiateMtx.getTrans(position);
    Vec3f start;
    NebulaVecScaleAdd(&direction, &position, &start, cNum_updateHitSensor_f1);
    Vec3f end;
    NebulaVecScaleAdd(&direction, &start, &end, mRadiateLength - cNum_updateHitSensor_f2);
    MR::calcPerpendicFootToLineInside(&pSensor->mPosition, *MR::getPlayerPos(), start, end);
}

void FirePressureRadiate::calcRadiateEffectMtx() {
    MtxPtr jointMtx = MR::getJointMtx(this, "Cannon3");
    Pos3f effectMtx(jointMtx);
    Vec3f trans;
    effectMtx.getTrans(trans);
    mRadiateMtx.set(effectMtx);
    mRadiateMtx.mMtx[0][3] = trans.x;
    mRadiateMtx.mMtx[1][3] = trans.y;
    mRadiateMtx.mMtx[2][3] = trans.z;
}

bool FirePressureRadiate::calcJointCannon(Pos3f* pOutPos, const JointControllerInfo&) {
    Pos3f mtx;
    Vec3f axis(0.0f, 0.0f, 1.0f);
    mtx.makeRotate(axis, MR::toRadian(mCannonRotation));
    pOutPos->concat(*pOutPos, mtx);
    return true;
}
