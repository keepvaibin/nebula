#include "Game/MapObj/SurprisedGalaxy.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvSurprisedGalaxy {
    NEW_NERVE(SurprisedGalaxyNrvExit, SurprisedGalaxy, Exit);
    NEW_NERVE(SurprisedGalaxyNrvWait, SurprisedGalaxy, Wait);
};  // namespace NrvSurprisedGalaxy

namespace {
    // "SurpXxxGalaxy" opens XxxGalaxy; the plain name opens the surfing galaxy.
    const u32 sObjNamePrefixLength = sizeof("Surp") - 1;
};  // namespace

SurprisedGalaxy::SurprisedGalaxy(const char* pName) : LiveActor(pName), mGalaxyName() {
    lumacore::setClass(this, lumacore::kClassId< SurprisedGalaxy >);
}

void SurprisedGalaxy::init(const TableIter& rIter) {
    const char* pObjName;
    MR::getObjectName(&pObjName, rIter);
    mGalaxyName = MR::isEqualString(pObjName, "SurprisedGalaxy") ? "SurfingLv1Galaxy" : pObjName + ::sObjNamePrefixLength;

    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("MiniSurprisedGalaxy", nullptr, false);
    MR::connectToSceneMapObj(this);
    f32 boundingRadius = 0.0f;
    MR::calcModelBoundingRadius(&boundingRadius, this);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "Body", 8, boundingRadius, Vec3f(0.0f, 0.0f, 0.0f));
    MR::setClippingFar200m(this);
    initNerve(GET_NERVE(SurprisedGalaxy, SurprisedGalaxyNrvWait));
    MR::useStageSwitchSleep(this, rIter);
    MR::startAction(this, "MiniSurprisedGalaxy");
    MR::registerDemoSimpleCastAll(this);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        makeActorDead();
        MR::syncStageSwitchAppear(this);
    } else {
        makeActorAppeared();

        if (MR::isGalaxyAppearGreenDriver(mGalaxyName)) {
            if (!MR::isOnGameEventFlagGreenDriver()) {
                makeActorDead();
            }
        } else if (!MR::isOnGameEventFlagViewCompleteEndingMarioAndLuigi()) {
            makeActorDead();
        }
    }
}

void SurprisedGalaxy::attackSensor(HitSensor* /*pSender*/, HitSensor* pReceiver) {
    if (MR::isSensorPlayerOrRide(pReceiver) && !isNerve(GET_NERVE(SurprisedGalaxy, SurprisedGalaxyNrvExit))) {
        setNerve(GET_NERVE(SurprisedGalaxy, SurprisedGalaxyNrvExit));
    }
}

void SurprisedGalaxy::exeExit() {
    if (MR::isFirstStep(this)) {
        MR::submitTrigSE();
        MR::closeSystemWipeWhiteFade();
        MR::stopStageBGM(90);
        MR::stopSubBGM(90);
        MR::tryStartDemo(this, "ギャラクシー移動");
    }

    if (MR::isSystemWipeActive()) {
        return;
    }

    MR::onGameEventFlagGalaxyOpen(mGalaxyName);
    MR::endDemo(this, "ギャラクシー移動");
    MR::permitTrigSE();
    MR::requestStartScenarioSelect(mGalaxyName);
}

void SurprisedGalaxy::exeWait() {
}

bool SurprisedGalaxy::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_IS_RUSH_TAKEOVER && isNerve(GET_NERVE(SurprisedGalaxy, SurprisedGalaxyNrvWait))) {
        setNerve(GET_NERVE(SurprisedGalaxy, SurprisedGalaxyNrvExit));
    }

    return false;
}
