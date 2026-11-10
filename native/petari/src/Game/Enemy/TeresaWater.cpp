#include "Game/Enemy/TeresaWater.hpp"
#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/Enemy/WalkerStateBindStarPointer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 100.0f);

namespace {
    NEBULA_TUNABLE_RECORD(MR::ActorMoveParam, sMoveParam, {0.5f, 0.0f, 0.9f, 1.5f});
    NEBULA_TUNABLE(f32, sDefaultMoveLength, 800.0f);
    NEBULA_TUNABLE(f32, sSideFriction, 0.5f);
    NEBULA_TUNABLE(f32, sTargetRadius, 30.0f);
};  // namespace

namespace NrvTeresaWater {
    NEW_NERVE(TeresaWaterNrvMove, TeresaWater, Move);
    NEW_NERVE(TeresaWaterNrvHit, TeresaWater, Hit);
    NEW_NERVE(TeresaWaterNrvStun, TeresaWater, Stun);
    NEW_NERVE_ONEND(TeresaWaterNrvPointerSwoon, TeresaWater, PointerSwoon, PointerSwoon);
};  // namespace NrvTeresaWater

TeresaWater::TeresaWater(const char* pName) : LiveActor(pName), mScaleCtrl(), mBindStarPointer() {
    mFront.set(0.0f, 0.0f, 1.0f);
    mFarPos.set(0.0f, 0.0f, 0.0f);
    mInitPos.set(0.0f, 0.0f, 0.0f);
    mTarget = nullptr;
}

void TeresaWater::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("TeresaWater", nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensor(1);
    Vec3f sensorOffset;
    sensorOffset.x = 0.0f;
    sensorOffset.y = 0.0f;
    sensorOffset.z = 0.0f;
    MR::addHitSensorAtJointEnemySimple(this, "body", "SklRoot", 8, cNum_init_f1, sensorOffset);
    initEffectKeeper(1, nullptr);
    initSound(4, false);
    Vec3f ptrPos;
    ptrPos.set(0.0f, 0.0f, 0.0f);
    MR::initStarPointerTargetAtJoint(this, "SklRoot", cNum_init_f2, ptrPos);

    mScaleCtrl = new AnimScaleController(nullptr);
    mBindStarPointer = new WalkerStateBindStarPointer(this, mScaleCtrl);
    MR::declareCoin(this, 1);
    MR::calcFrontVec(&mFront, this);
    f32 moveLength = ::sDefaultMoveLength;
    MR::getPlacementArg0NoInit(rIter, &moveLength);
    mInitPos.set(mPosition);
    mFarPos.scaleAdd(moveLength, mFront, mInitPos);
    mTarget = &mFarPos;
    MR::initShadowVolumeCylinder(this, cNum_init_f3);
    initNerve(GET_NERVE(TeresaWater, TeresaWaterNrvMove));

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void TeresaWater::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Wait");
    }

    MR::moveAndTurnToTarget(this, &mFront, *mTarget, ::sMoveParam);
    MR::attenuateVelocityExceptDirection(this, mFront, ::sSideFriction);

    if (MR::isNear(this, *mTarget, ::sTargetRadius)) {
        if (mTarget == &mFarPos) {
            mTarget = &mInitPos;
        } else {
            mTarget = &mFarPos;
        }
    }
}

void TeresaWater::exeHit() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Hit");
        mVelocity.zero();
        MR::startSound(this, "SE_EV_TERESA_ATTACK_SUCCESS");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(TeresaWater, TeresaWaterNrvMove));
    }
}

void TeresaWater::exeStun() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Reaction");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(TeresaWater, TeresaWaterNrvMove));
    }
}

void TeresaWater::kill() {
    MR::appearCoinPop(this, mPosition, 1);
    MR::emitEffect(this, "DeathWater");
    MR::startSound(this, "SE_EM_TERESA_DEAD");
    MR::startSound(this, "SE_EV_TERESA_DEAD");
    LiveActor::kill();
}

void TeresaWater::control() {
    mScaleCtrl->updateNerve();

    if (isNerve(GET_NERVE(TeresaWater, TeresaWaterNrvMove))) {
        MR::startLevelSound(this, "SE_EM_LV_TERESA_MOVE");
    }

    if (tryPointerSwoon()) {
        return;
    }
}

void TeresaWater::calcAndSetBaseMtx() {
    Vec3f up;
    MR::calcUpVec(&up, this);
    up.negate();

    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, up, mFront);
    MR::setBaseTRMtx(this, mtx);

    MR::setBaseScale(this, mScaleCtrl->mScale * mScale);
}

void TeresaWater::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isNerve(GET_NERVE(TeresaWater, TeresaWaterNrvHit))) {
        return;
    }

    if (!MR::isSensorPlayerOrRide(pReceiver)) {
        return;
    }

    if (!isNerve(GET_NERVE(TeresaWater, TeresaWaterNrvPointerSwoon)) && MR::sendMsgEnemyAttackStrong(pReceiver, pSender)) {
        setNerve(GET_NERVE(TeresaWater, TeresaWaterNrvHit));
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool TeresaWater::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgStarPieceAttack(msg)) {
        setNerve(GET_NERVE(TeresaWater, TeresaWaterNrvStun));

        return true;
    }

    if (MR::isMsgLockOnStarPieceShoot(msg)) {
        return true;
    }

    if (MR::isMsgSearchlightAttack(msg)) {
        kill();

        return true;
    }

    return false;
}

bool TeresaWater::tryPointerSwoon() {
    if (isNerve(GET_NERVE(TeresaWater, TeresaWaterNrvStun))) {
        return false;
    }

    if (isNerve(GET_NERVE(TeresaWater, TeresaWaterNrvPointerSwoon))) {
        return false;
    }

    if (!mBindStarPointer->tryStartPointBind()) {
        return false;
    }

    setNerve(GET_NERVE(TeresaWater, TeresaWaterNrvPointerSwoon));

    return true;
}

void TeresaWater::endPointerSwoon() {
    mBindStarPointer->kill();
}

void TeresaWater::exePointerSwoon() {
    MR::updateActorStateAndNextNerve(this, mBindStarPointer, GET_NERVE(TeresaWater, TeresaWaterNrvMove));
}
