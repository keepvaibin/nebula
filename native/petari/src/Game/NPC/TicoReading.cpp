#include "Game/NPC/TicoReading.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvTicoReading {
    NEW_NERVE(TicoReadingNrvWait, TicoReading, Wait);
};  // namespace NrvTicoReading

TicoReading::TicoReading(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< TicoReading >);
}

void TicoReading::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("Tico", nullptr, false);
    MR::connectToSceneNpc(this);
    MR::initLightCtrl(this);
    s32 animFrame = 0;
    MR::getPlacementArg0NoInit(rIter, &animFrame);
    MR::startBrk(this, "ColorChange");
    MR::setBrkFrameAndStop(this, animFrame);
    initEffectKeeper(0, nullptr);
    MR::initShadowFromCSV(this, "Shadow");
    MR::tryRegisterDemoCast(this, rIter);
    initNerve(GET_NERVE(TicoReading, TicoReadingNrvWait));
    makeActorDead();
}

void TicoReading::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(TicoReading, TicoReadingNrvWait));
}

void TicoReading::kill() {
    MR::forceDeleteEffectAll(this);
    LiveActor::kill();
}

void TicoReading::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "DemoRosettaReadingWait");
        MR::setBckFrameAtRandom(this);
    }
}
