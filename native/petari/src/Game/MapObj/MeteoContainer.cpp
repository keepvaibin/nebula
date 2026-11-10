#include "Game/MapObj/MeteoContainer.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 150.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeDestroy_1, "exeDestroy.1", 2);
NEBULA_TUNABLE_NAMED(s32, cStep_exeDestroy_2, "exeDestroy.2", 3);

namespace NrvMeteoContainer {
    NEW_NERVE(MeteoContainerNrvWait, MeteoContainer, Wait);
    NEW_NERVE(MeteoContainerNrvDestroy, MeteoContainer, Destroy);
};  // namespace NrvMeteoContainer

MeteoContainer::MeteoContainer(const char* pName) : LiveActor(pName) {
}

void MeteoContainer::init(const TableIter& rIter) {
    const char* objName = nullptr;

    if (!MR::getObjectName(&objName, rIter)) {
        objName = "MeteoContainer";
    }

    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm(objName, nullptr, false);
    MR::connectToSceneEnemy(this);
    initHitSensor(1);
    Vec3f sensorOffs;
    sensorOffs.x = 0.0f;
    sensorOffs.y = cNum_init_f1;
    sensorOffs.z = 0.0f;
    MR::addHitSensorMapObj(this, "body", 16, cNum_init_f2, sensorOffs);
    initSound(4, false);
    initNerve(GET_NERVE(MeteoContainer, MeteoContainerNrvWait));
    MR::initShadowVolumeSphere(this, cNum_init_f3);
    MR::onCalcShadowOneTime(this, nullptr);
    MR::needStageSwitchWriteDead(this, rIter);
    initEffectKeeper(1, nullptr);
    appear();
}

void MeteoContainer::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(MeteoContainer, MeteoContainerNrvWait));
}

void MeteoContainer::kill() {
    MR::startSound(this, "SE_OJ_MTO_CONTAINER_BREAK");
    if (MR::isValidSwitchDead(this)) {
        MR::onSwitchDead(this);
        MR::startSystemSE("SE_SY_READ_RIDDLE_S");
    }

    LiveActor::kill();
}

void MeteoContainer::control() {
}

void MeteoContainer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        pReceiver->receiveMessage(ACTMES_PUSH, pSender);
    }
}

bool MeteoContainer::receiveMsgPush(HitSensor* pSender, HitSensor* /*pReceiver*/) {
    return MR::isSensorPlayer(pSender);
}

bool MeteoContainer::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_TORNADO_ATTACK && isNerve(GET_NERVE(MeteoContainer, MeteoContainerNrvWait))) {
        setNerve(GET_NERVE(MeteoContainer, MeteoContainerNrvDestroy));

        return true;
    }

    return false;
}

void MeteoContainer::exeWait() {
}

void MeteoContainer::exeDestroy() {
    if (MR::isStep(this, cStep_exeDestroy_1)) {
        MR::stopScene(10);
    }

    if (MR::isStep(this, cStep_exeDestroy_2)) {
        MR::shakeCameraNormal();
        kill();
    }
}
