#include "Game/MapObj/DragonHeadFlower.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initFlowerSensors_f1, "initFlowerSensors.f1", 560.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initFlowerSensors_f2, "initFlowerSensors.f2", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initFlowerSensors_f3, "initFlowerSensors.f3", 650.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initFlowerSensors_f4, "initFlowerSensors.f4", 400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initFlowerSensors_f5, "initFlowerSensors.f5", 350.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParabola_f1, "initParabola.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initParabola_f2, "initParabola.f2", 1.2f);

namespace {
    NEBULA_TUNABLE(f32, sGravityAcc, 0.94f);
    NEBULA_TUNABLE(f32, sBreatheAccel, 3.0f);
    NEBULA_TUNABLE(s32, sBreatheFreq, 40);
    NEBULA_TUNABLE(f32, sStartLaunchRange, 50.0f);
    NEBULA_TUNABLE(f32, sStartForceFixPos, 0.0f);
    NEBULA_TUNABLE(f32, sEndForceFixPos, 1.0f);
    NEBULA_TUNABLE(s32, sDelayStartToCenter, 15);
};  // namespace

namespace NrvDragonHeadFlower {
    NEW_NERVE(DragonHeadFlowerNrvWait, DragonHeadFlower, Wait);
    NEW_NERVE(DragonHeadFlowerNrvSetCenter, DragonHeadFlower, SetCenter);
    NEW_NERVE(DragonHeadFlowerNrvLaunchReady, DragonHeadFlower, LaunchReady);
    NEW_NERVE(DragonHeadFlowerNrvRailMove, DragonHeadFlower, RailMove);
};  // namespace NrvDragonHeadFlower

DragonHeadFlower::DragonHeadFlower(const char* pName)
    : LiveActor(pName), mPlayerSensor(), mParabolaProgress(), mParabolaStartPos(0, 0, 0), mTargetVelocity(0, 0, 0), mCurrentGravity(0, -1, 0),
      mEndGravity(0, 0, 0), mParabolaDirection(0, 0, 0), mParabolaSteps(), mParabolaDistance(), mParabolaAccel(), mParabolaVel() {
    lumacore::setClass(this, lumacore::kClassId< DragonHeadFlower >);
}

namespace {
    inline void initFlowerSensors(DragonHeadFlower* pActor) {
        pActor->initHitSensor(2);
        MR::addHitSensor(pActor, "body", ATYPE_JUMP_HOLE, 8, cNum_initFlowerSensors_f1, Vec3f(0.0f, cNum_initFlowerSensors_f2, cNum_initFlowerSensors_f3));
        MR::addHitSensorMapObj(pActor, "push", 16, cNum_initFlowerSensors_f4, Vec3f(0.0f, cNum_initFlowerSensors_f5, 0.0f));
    }
};  // namespace

void DragonHeadFlower::init(const TableIter& rIter) {
    const char* pModelName = nullptr;

    if (!MR::getObjectName(&pModelName, rIter)) {
        pModelName = "DragonHeadFlower";
    }

    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm(pModelName, nullptr, false);

    MR::connectToSceneMapObj(this);

    initRailRider(rIter);

    ::initFlowerSensors(this);

    initEffectKeeper(2, nullptr);
    initSound(4, false);
    initNerve(GET_NERVE(DragonHeadFlower, DragonHeadFlowerNrvWait));

    appear();

    MR::startBck(this, "Wait");
}

void DragonHeadFlower::control() {
}

void DragonHeadFlower::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    MR::sendMsgPush(pReceiver, pSender);
}

bool DragonHeadFlower::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_SPHERE_PLAYER_BINDED) {
        mPlayerSensor = pSender;
        mTargetVelocity = pSender->mHost->mVelocity;

        MR::zeroVelocity(mPlayerSensor->mHost);

        return true;
    }

    return false;
}

void DragonHeadFlower::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBckWithInterpole(this, "Wait", ::sDelayStartToCenter);
        MR::validateClipping(this);
    }

    if (mPlayerSensor != nullptr) {
        MR::calcGravityVector(this, mPosition, &mCurrentGravity, nullptr, 0);
        setNerve(GET_NERVE(DragonHeadFlower, DragonHeadFlowerNrvSetCenter));
    }
}

void DragonHeadFlower::exeSetCenter() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Breathe");
        MR::invalidateClipping(this);
    }

    if (getNerveStep() <= 0) {
        return;
    }

    Vec3f targetPos(mPlayerSensor->mPosition);

    Vec3f launchPos;
    MR::copyJointPos(this, "Point", &launchPos);

    targetPos += mTargetVelocity;

    f32 t = MR::clamp(static_cast< f32 >(getNerveStep() - ::sBreatheFreq) / ::sBreatheFreq, ::sStartForceFixPos, ::sEndForceFixPos);

    targetPos = targetPos * (1.0f - t) + launchPos.multInLine(t);

    Vec3f offset(mPlayerSensor->mHost->mPosition - mPlayerSensor->mPosition);
    mPlayerSensor->mHost->mPosition.set(targetPos + offset);

    Vec3f targetDir(launchPos - targetPos);

    f32 targetVel;
    MR::separateScalarAndDirection(&targetVel, &targetDir, targetDir);

    mTargetVelocity += targetDir * ::sBreatheAccel;
    mTargetVelocity.x *= ::sGravityAcc;
    mTargetVelocity.y *= ::sGravityAcc;
    mTargetVelocity.z *= ::sGravityAcc;

    if (targetVel < ::sStartLaunchRange) {
        setNerve(GET_NERVE(DragonHeadFlower, DragonHeadFlowerNrvLaunchReady));
    }
}

void DragonHeadFlower::exeLaunchReady() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "In");

        MR::hidePlayer();
        MR::hideModel(mPlayerSensor->mHost);
    }

    Vec3f launchPos;
    MR::copyJointPos(this, "Point", &launchPos);
    Vec3f offset(mPlayerSensor->mHost->mPosition - mPlayerSensor->mPosition);
    mPlayerSensor->mHost->mPosition.set(launchPos + offset);

    if (!MR::isBckStopped(this)) {
        return;
    }

    mParabolaProgress = 0.0f;

    initParabola(mPlayerSensor->mHost->mPosition);

    setNerve(GET_NERVE(DragonHeadFlower, DragonHeadFlowerNrvRailMove));
}

void DragonHeadFlower::exeRailMove() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Out");

        MR::shakeCameraNormal();

        MR::emitEffect(this, "Launch");

        MR::startSoundPlayer("SE_PV_JUMP_JOY");

        MR::showPlayer();
        MR::showModel(mPlayerSensor->mHost);
    }

    f32 along = mParabolaDistance * mParabolaProgress;
    f32 fall = mParabolaProgress * (mParabolaVel + mParabolaAccel * mParabolaProgress);

    Vec3f targetPos(mParabolaStartPos + mEndGravity * fall + mParabolaDirection * along);
    Vec3f prevPos(mPlayerSensor->mHost->mPosition);

    mPlayerSensor->mHost->mPosition.set(targetPos);

    mParabolaProgress += 1.0f / mParabolaSteps;

    if (mParabolaProgress >= 1.0f) {
        Vec3f* velocity = &mPlayerSensor->mHost->mVelocity;
        velocity->set(targetPos - prevPos);
        mPlayerSensor->receiveMessage(ACTMES_END_JUMP_HOLE, getSensor("body"));

        mPlayerSensor = nullptr;

        setNerve(GET_NERVE(DragonHeadFlower, DragonHeadFlowerNrvWait));
    }
}

void DragonHeadFlower::initParabola(const Vec3f& rStartPos) {
    MR::moveCoordToStartPos(this);
    Vec3f railStartPos(MR::getRailPos(this));

    MR::moveCoordToEndPos(this);
    Vec3f railEndPos(MR::getRailPos(this));

    MR::calcGravityVector(this, railEndPos, &mEndGravity, nullptr, 0);

    if (MR::isNearZero(mEndGravity)) {
        mEndGravity.set(0.0f, -1.0f, 0.0f);
    }

    f32 max = mEndGravity.dot(railStartPos - rStartPos);
    f32 end = mEndGravity.dot(railEndPos - rStartPos);
    mParabolaDirection = railEndPos - rStartPos - mEndGravity * end;

    MR::separateScalarAndDirection(&mParabolaDistance, &mParabolaDirection, mParabolaDirection);
    MR::calcParabolicFunctionParam(&mParabolaAccel, &mParabolaVel, max, end);

    mParabolaStartPos = rStartPos;

    mParabolaSteps = MR::sqrt(MR::abs((mParabolaAccel * cNum_initParabola_f1) / cNum_initParabola_f2));
}

