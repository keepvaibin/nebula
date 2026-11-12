#include "Game/MapObj/AirBubbleGenerator.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/AirBubbleHolder.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sGenerateTime = 6;
    const f32 sGenerateOffsetY = 120.0f;
};  // namespace

namespace NrvAirBubbleGenerator {
    NEW_NERVE(AirBubbleGeneratorNrvWait, AirBubbleGenerator, Wait);
    NEW_NERVE(AirBubbleGeneratorNrvGenerate, AirBubbleGenerator, Generate);
};  // namespace NrvAirBubbleGenerator

AirBubbleGenerator::AirBubbleGenerator(const char* pName) : LiveActor(pName), mWaitTime(180), mBubbleLifeTime(-1) {
    lumacore::setClass(this, lumacore::kClassId< AirBubbleGenerator >);
}

void AirBubbleGenerator::init(const TableIter& rIter) {
    MR::createAirBubbleHolder();
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("AirBubbleGenerator", nullptr, false);
    MR::connectToSceneNoSilhouettedMapObj(this);
    initEffectKeeper(0, nullptr);
    initNerve(GET_NERVE(AirBubbleGenerator, AirBubbleGeneratorNrvWait));
    initHitSensor(1);
    MR::addBodyMessageSensorReceiver(this);
    MR::initCollisionParts(this, "AirBubbleGenerator", getSensor(nullptr), nullptr);
    MR::getPlacementArg0NoInit(rIter, &mWaitTime);
    MR::getPlacementArg1NoInit(rIter, &mBubbleLifeTime);
    initSound(2, false);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void AirBubbleGenerator::control() {
}

void AirBubbleGenerator::exeWait() {
    if (mWaitTime != 0 && mWaitTime - 1 == getNerveStep() % mWaitTime) {
        setNerve(GET_NERVE(AirBubbleGenerator, AirBubbleGeneratorNrvGenerate));
    }
}

void AirBubbleGenerator::exeGenerate() {
    Vec3f axisY;

    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_AIR_BUBBLE_APPEAR");
        MR::startBck(this, "Generate");
    }

    if (MR::isGreaterStep(this, ::sGenerateTime)) {
        MR::calcActorAxisY(&axisY, this);
        MR::appearAirBubble(mPosition + axisY * ::sGenerateOffsetY, mBubbleLifeTime);
        setNerve(GET_NERVE(AirBubbleGenerator, AirBubbleGeneratorNrvWait));
    }
}
