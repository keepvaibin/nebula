#include "Game/MapObj/DesertMovingLand.hpp"
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
    mMoveDownTime = 720;
    mMoveUpTime = 720;
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

    f32 arg = 2000.0f;
    MR::getPlacementArg0NoInit(rIter, &arg);
    MR::getPlacementArg1NoInit(rIter, &mMoveDownTime);
    MR::getPlacementArg2NoInit(rIter, &mMoveUpTime);
    Vec3f stack_3C;
    MR::calcUpVec(&stack_3C, this);

    if (MR::isValidSwitchA(this)) {
        mUpperPos.set(mPosition);
        mLowerPos.set(mUpperPos - stack_3C * arg);
    } else {
        mLowerPos.set(mPosition);
        mUpperPos.set(mLowerPos + stack_3C * arg);
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
    Mtx34f stack_38;
    stack_38.set(playerBaseMtx);
    MtxPtr jointMtx = MR::getJointMtx(this, mObjectName);
    Mtx34f stack_8;
    stack_8.set(jointMtx);
    stack_8.invert(stack_8);
    mDemoPlayerMtx.concat(stack_8, stack_38);
}

void DesertMovingLand::updateDemoPlayerPos() {
    MtxPtr jointMtx = MR::getJointMtx(this, mObjectName);
    Pos3f stack_8;
    stack_8.set(jointMtx);
    stack_8.concat(stack_8, mDemoPlayerMtx);
    stack_8.getTrans(MR::getPlayerDemoActor()->mPosition);
    MR::setPlayerBaseMtx(stack_8);
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
        Vec3f stack_8(mLowerPos - mUpperPos);
        stack_8.setLength(stack_8.length() / mMoveDownTime);
        mVelocity.set(stack_8);
        MR::startAtmosphereSE("SE_OJ_DESERT_LAND_MOVE_ST");
    }
    MR::startAtmosphereLevelSE("SE_OJ_LV_DESERT_LAND_MOVE");
    if (MR::isLessStep(this, 90)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    if (MR::isStep(this, 90)) {
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
        Vec3f stack_8(mUpperPos - mLowerPos);
        stack_8.setLength(stack_8.length() / mMoveUpTime);
        mVelocity.set(stack_8);
        MR::startAtmosphereSE("SE_OJ_DESERT_LAND_MOVE_ST");
    }
    MR::startAtmosphereLevelSE("SE_OJ_LV_DESERT_LAND_MOVE");

    if (MR::isLessStep(this, 90)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    if (MR::isStep(this, 90)) {
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

        MR::shakeCameraInfinity(this, 0.15f, 1.2f);
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

    if (MR::isStep(this, 30))
        mVelocity.zero();
}

void DesertMovingLand::connectToScene(const MapObjActorInitInfo& rInfo) {
    MR::connectToSceneCollisionMapObj(this);
}

void DesertMovingLand::initCaseNoUseSwitchB(const MapObjActorInitInfo& rInfo) {
}

void DesertMovingLand::initCaseUseSwitchB(const MapObjActorInitInfo& rInfo) {
}

void DesertMovingLand::initCaseUseSwitchA(const MapObjActorInitInfo& rInfo) {
}

DesertMovingLand::~DesertMovingLand(){};
