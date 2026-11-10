#include "Game/MapObj/DesertMovingLand.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_math.h>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 2000.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeMoveSign_shakeCameraInfinity_1, "exeMoveSign.shakeCameraInfinity.1", 0.15f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMoveSign_shakeCameraInfinity_2, "exeMoveSign.shakeCameraInfinity.2", 1.2f);

namespace {
    static const char* cDemoName = "砂漠ＵＦＯ上昇";
};  // namespace

namespace NrvDesertMovingLand {
    NEW_NERVE(HostTypeWaitTop, DesertMovingLand, Wait);
    NEW_NERVE(HostTypeWaitBottom, DesertMovingLand, Wait);
    NEW_NERVE(HostTypeMoveDown, DesertMovingLand, MoveDown);
    NEW_NERVE(HostTypeMoveUp, DesertMovingLand, MoveUp);
    NEW_NERVE(HostTypeMoveDownSign, DesertMovingLand, MoveSign);
    NEW_NERVE(HostTypeMoveUpSign, DesertMovingLand, MoveSign);
    NEW_NERVE(HostTypeStop, DesertMovingLand, Stop);
};  // namespace NrvDesertMovingLand

DesertMovingLand::DesertMovingLand(const char* pName) : MapObjActor(pName), mUpperPos(gZeroVec), mLowerPos(gZeroVec) {
    mMoveDownTime = NebulaFrameClock::stepsOfSeconds(12);
    mMoveUpTime = NebulaFrameClock::stepsOfSeconds(12);
    mDemoPlayerMtx.identity();
}

void DesertMovingLand::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info = MapObjActorInitInfo();
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    info.setupHitSensor();
    info.setupNerve(GET_NERVE(DesertMovingLand, HostTypeWaitTop));
    info.setupBaseMtxFollowTarget();
    info.setupFarClipping(-1.0f);
    info.mIsAppearRiddleSE = false;
    initialize(rIter, info);

    if (!MR::isValidSwitchA(this))
        setNerve(GET_NERVE(DesertMovingLand, HostTypeWaitBottom));

    MtxPtr jointMtx = MR::getJointMtx(this, mObjectName);
    HitSensor* pSensorMove = getSensor(0);
    MR::initCollisionParts(this, "Move", pSensorMove, jointMtx);

    if (MR::isDead(this))
        MR::invalidateCollisionParts(this);
    else
        MR::validateCollisionParts(this);

    f32 moveDistance = cNum_init_f1;
    MR::getPlacementArg0NoInit(rIter, &moveDistance);
    MR::getPlacementArg1NoInit(rIter, &mMoveDownTime);
    MR::getPlacementArg2NoInit(rIter, &mMoveUpTime);
    Vec3f up;
    MR::calcUpVec(&up, this);

    if (MR::isValidSwitchA(this)) {
        mUpperPos.set(mPosition);
        mLowerPos.set(mUpperPos - up * moveDistance);
    } else {
        mLowerPos.set(mPosition);
        mUpperPos.set(mLowerPos + up * moveDistance);
    }
    MR::setBodySensorTypePress(this);
}

void DesertMovingLand::control() {
    MapObjActor::control();
    if (MR::isDemoActive(::cDemoName))
        updateDemoPlayerPos();

    MR::startLevelSound(this, "SE_AT_LV_WIND_MOVING_DESERT");
    if (!isNerve(GET_NERVE(DesertMovingLand, HostTypeStop)) && MR::isPlayerOnPress())
        setNerve(GET_NERVE(DesertMovingLand, HostTypeStop));
}

void DesertMovingLand::startDemo() {
    MtxPtr playerBaseMtx = MR::getPlayerDemoActor()->getBaseMtx();
    Mtx34f playerMtx;
    playerMtx.set(playerBaseMtx);
    MtxPtr jointMtx = MR::getJointMtx(this, mObjectName);
    Mtx34f invJointMtx;
    invJointMtx.set(jointMtx);
    invJointMtx.invert(invJointMtx);
    mDemoPlayerMtx.concat(invJointMtx, playerMtx);
}

void DesertMovingLand::updateDemoPlayerPos() {
    MtxPtr jointMtx = MR::getJointMtx(this, mObjectName);
    Pos3f playerMtx;
    playerMtx.set(jointMtx);
    playerMtx.concat(playerMtx, mDemoPlayerMtx);
    playerMtx.getTrans(MR::getPlayerDemoActor()->mPosition);
    MR::setPlayerBaseMtx(playerMtx);
}

void DesertMovingLand::exeWait() {
    if (MR::isValidSwitchA(this) && MR::isOnSwitchA(this) && isNerve(GET_NERVE(DesertMovingLand, HostTypeWaitTop))) {
        startDemo();
        setNerve(GET_NERVE(DesertMovingLand, HostTypeMoveDownSign));
    } else if (MR::isOnSwitchB(this) && isNerve(GET_NERVE(DesertMovingLand, HostTypeWaitBottom))) {
        startDemo();
        setNerve(GET_NERVE(DesertMovingLand, HostTypeMoveUpSign));
    }
}

void DesertMovingLand::exeMoveDown() {
    if (MR::isFirstStep(this)) {
        Vec3f velocity(mLowerPos - mUpperPos);
        // A time of 0 or less (placement arg 1) crosses the whole way in one step.
        velocity.setLength(velocity.length() / (mMoveDownTime > 0 ? mMoveDownTime : 1));
        mVelocity.set(velocity);
        MR::startAtmosphereSE("SE_OJ_DESERT_LAND_MOVE_ST");
    }
    MR::startAtmosphereLevelSE("SE_OJ_LV_DESERT_LAND_MOVE");
    if (MR::isLessStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::stopShakingCamera(this);
    }

    if (MR::isStep(this, mMoveDownTime)) {
        MR::offSwitchB(this);
        mVelocity.zero();
        mPosition.set(mLowerPos);
        MR::startAtmosphereSE("SE_OJ_DESERT_LAND_MOVE_ED");
        setNerve(GET_NERVE(DesertMovingLand, HostTypeWaitBottom));
    }
}

void DesertMovingLand::exeMoveUp() {
    if (MR::isFirstStep(this)) {
        Vec3f velocity(mUpperPos - mLowerPos);
        velocity.setLength(velocity.length() / (mMoveUpTime > 0 ? mMoveUpTime : 1));
        mVelocity.set(velocity);
        MR::startAtmosphereSE("SE_OJ_DESERT_LAND_MOVE_ST");
    }
    MR::startAtmosphereLevelSE("SE_OJ_LV_DESERT_LAND_MOVE");

    if (MR::isLessStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        MR::stopShakingCamera(this);
    }

    if (MR::isStep(this, mMoveUpTime)) {
        if (MR::isValidSwitchA(this))
            MR::offSwitchA(this);

        mVelocity.zero();
        mPosition.set(mUpperPos);
        MR::startAtmosphereSE("SE_OJ_DESERT_LAND_MOVE_ED");
        setNerve(GET_NERVE(DesertMovingLand, HostTypeWaitTop));
    }
}

void DesertMovingLand::exeMoveSign() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Vibration");

        if (MR::isDemoActive("砂ＵＦＯ下降"))
            MR::stopStageBGM(60);

        MR::shakeCameraInfinity(this, cNum_exeMoveSign_shakeCameraInfinity_1, cNum_exeMoveSign_shakeCameraInfinity_2);
    }

    MR::startLevelSound(this, "SE_OJ_LV_DESERT_LAND_SIGN");
    MR::tryRumblePadMiddle(this, PLAYER_1);
    if (MR::isBckStopped(this)) {
        if (isNerve(GET_NERVE(DesertMovingLand, HostTypeMoveUpSign)))
            setNerve(GET_NERVE(DesertMovingLand, HostTypeMoveUp));
        else {
            if (isNerve(GET_NERVE(DesertMovingLand, HostTypeMoveDownSign))) {
                if (MR::isDemoActive("砂ＵＦＯ下降"))
                    MR::startEventBGM(MR::EventBgmID_Hurry);

                setNerve(GET_NERVE(DesertMovingLand, HostTypeMoveDown));
            }
        }
    }
}

void DesertMovingLand::exeStop() {
    if (MR::isFirstStep(this))
        MR::invalidateHitSensors(this);

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500)))
        mVelocity.zero();
}

void DesertMovingLand::connectToScene(const MapObjActorInitInfo& /*rInfo*/) {
    MR::connectToSceneCollisionMapObj(this);
}

void DesertMovingLand::initCaseNoUseSwitchB(const MapObjActorInitInfo& /*rInfo*/) {
}

void DesertMovingLand::initCaseUseSwitchB(const MapObjActorInitInfo& /*rInfo*/) {
}

void DesertMovingLand::initCaseUseSwitchA(const MapObjActorInitInfo& /*rInfo*/) {
}
