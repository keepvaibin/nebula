#include "Game/MapObj/ScrewSwitch.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/MapObjConnector.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sStepForAdjust = 3;
    static const s32 sStepToRumbleCamera = NebulaFrameClock::stepsOfMilliseconds(500);
    static const s32 sStepForScrewStart = NebulaFrameClock::stepsOfMilliseconds(500);
    const s32 sStepForPause = 0;
};  // namespace

namespace NrvScrewSwitch {
    NEW_NERVE(ScrewSwitchNrvWait, ScrewSwitch, Wait);
    NEW_NERVE(ScrewSwitchNrvAdjust, ScrewSwitch, Adjust);
    NEW_NERVE(ScrewSwitchNrvScrew, ScrewSwitch, Screw);
    NEW_NERVE(ScrewSwitchNrvEnd, ScrewSwitch, End);
};  // namespace NrvScrewSwitch

ScrewSwitch::ScrewSwitch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< ScrewSwitch >);
    mCollisionParts = nullptr;
    mBindActor = nullptr;
    mMapObjConnector = nullptr;
    mMapObjConnector = new MapObjConnector(this);
}

void ScrewSwitch::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::needStageSwitchWriteA(this, rIter);
    MR::useStageSwitchSleep(this, rIter);
    initModelManagerWithAnm("ScrewSwitch", nullptr, false);
    MR::connectToSceneMapObjDecorationStrongLight(this);
    MR::initLightCtrl(this);
    initHitSensor(2);
    MR::addBodyMessageSensorMapObj(this);
    Vec3f offset;
    offset.scale(-150.0f, mGravity);
    MR::addHitSensorAtJoint(this, "binder", "Screw", ATYPE_BINDER, 8, 150.0f, offset);
    MtxPtr jointMtx = MR::getJointMtx(this, "Screw");
    HitSensor* Sensor = getSensor("binder");
    MR::initCollisionParts(this, "ScrewCol", Sensor, jointMtx);
    mCollisionParts = MR::createCollisionPartsFromLiveActor(this, "ScrewReceiveCol", getSensor("body"), MR::getJointMtx(this, "ScrewReceive"),
                                                            MR::CollisionScaleType_NotUsingScale);
    MR::validateCollisionParts(mCollisionParts);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::tryRegisterDemoCast(this, rIter);
    initNerve(GET_NERVE(ScrewSwitch, ScrewSwitchNrvWait));
    makeActorAppeared();
}

void ScrewSwitch::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "ScrewSwitchOn");
        MR::setBrkFrameAndStop(this, 0.0f);
    }
}

void ScrewSwitch::exeAdjust() {
    if (MR::isFirstStep(this)) {
        MR::startBrk(this, "ScrewSwitchOn");
        MR::setBrkFrameAndStop(this, 0.0f);
    }

    mBindActor->mPosition.lerp(mBindActor->mPosition, getSensor("binder")->mPosition, static_cast< f32 >(getNerveStep()) / ::sStepForAdjust);
    if (MR::isStep(this, ::sStepForAdjust)) {
        setNerve(GET_NERVE(ScrewSwitch, ScrewSwitchNrvScrew));
    }
}

void ScrewSwitch::exeScrew() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "ScrewSwitchOn");
        MR::startBrk(this, "ScrewSwitchOn");
        MR::startBckPlayer("ScrewSwitchOn");
    }
    if (MR::isLessStep(this, ::sStepForScrewStart)) {
        MR::startLevelSound(this, "SE_OJ_LV_SCREW_SWITCH_MOVE");
    }
    if (MR::isStep(this, ::sStepToRumbleCamera)) {
        MR::startSound(this, "SE_OJ_SCREW_SWITCH_ON");
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        MR::shakeCameraNormal();
    }
    if (MR::isBckStopped(this)) {
        MR::onSwitchA(this);
        setNerve(GET_NERVE(ScrewSwitch, ScrewSwitchNrvEnd));
    }
}

void ScrewSwitch::exeEnd() {
    if (MR::isStep(this, ::sStepForPause)) {
        MR::invalidateHitSensors(this);
        if (MR::isPlayerInRush()) {
            MR::endBindAndPlayerWait(this);
            mBindActor = nullptr;
        }
    }
}

void ScrewSwitch::initAfterPlacement() {
    mMapObjConnector->attachToUnder();
}

void ScrewSwitch::calcAndSetBaseMtx() {
    LiveActor::calcAndSetBaseMtx();
    mMapObjConnector->connect();
}

void ScrewSwitch::control() {
    mCollisionParts->setMtx();
}

bool ScrewSwitch::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgRushBegin(msg) && MR::isSensorPlayer(pSender) && MR::isOnPlayer(getSensor("binder"))) {
        mBindActor = pSender->mHost;
        MR::startSound(mBindActor, "SE_PV_TWIST_START");
        MR::startSound(mBindActor, "SE_PM_SPIN_ATTACK");
        setNerve(GET_NERVE(ScrewSwitch, ScrewSwitchNrvAdjust));
        return true;
    }

    if (MR::isMsgUpdateBaseMtx(msg) && mBindActor && isNerve(GET_NERVE(ScrewSwitch, ScrewSwitchNrvScrew))) {
        updateBindActorMtx();
        return true;
    }

    return false;
}

void ScrewSwitch::updateBindActorMtx() {
    Pos3f pos;
    pos.set(mBindActor->getBaseMtx());
    pos.setTrans(getSensor("binder")->mPosition);
    MR::setBaseTRMtx(mBindActor, pos);
}
