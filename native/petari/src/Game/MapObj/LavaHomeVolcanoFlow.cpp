#include "Game/MapObj/LavaHomeVolcanoFlow.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);

namespace NrvLavaHomeVolcanoFlow {
    NEW_NERVE(HostTypeWait, LavaHomeVolcanoFlow, Wait);
    NEW_NERVE(HostTypeMove, LavaHomeVolcanoFlow, Move);
    NEW_NERVE(HostTypeDone, LavaHomeVolcanoFlow, Wait);
};  // namespace NrvLavaHomeVolcanoFlow

LavaHomeVolcanoFlow::LavaHomeVolcanoFlow(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< LavaHomeVolcanoFlow >);
}

void LavaHomeVolcanoFlow::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("LavaHomeVolcanoFlow", nullptr, false);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f1);
    initNerve(GET_NERVE(LavaHomeVolcanoFlow, HostTypeWait));
    MR::needStageSwitchReadA(this, rIter);
    MR::connectToSceneMapObj(this);
    makeActorAppeared();
    MR::startBtk(this, "LavaHomeVolcanoFlow");
}

void LavaHomeVolcanoFlow::startClipped() {
    LiveActor::startClipped();
    if (isNerve(GET_NERVE(LavaHomeVolcanoFlow, HostTypeMove)) || isNerve(GET_NERVE(LavaHomeVolcanoFlow, HostTypeDone))) {
        MR::deleteEffect(this, "Loop");
    }
}

void LavaHomeVolcanoFlow::endClipped() {
    LiveActor::endClipped();
    if (isNerve(GET_NERVE(LavaHomeVolcanoFlow, HostTypeMove)) || isNerve(GET_NERVE(LavaHomeVolcanoFlow, HostTypeDone))) {
        MR::emitEffect(this, "Loop");
    }
}

void LavaHomeVolcanoFlow::exeWait() {
    if (isNerve(GET_NERVE(LavaHomeVolcanoFlow, HostTypeWait)) && MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(LavaHomeVolcanoFlow, HostTypeMove));
        return;
    }
    if (isNerve(GET_NERVE(LavaHomeVolcanoFlow, HostTypeDone))) {
        MR::startLevelSound(this, "SE_OJ_LV_VOLCANO_ERUPTION");
    }
}

void LavaHomeVolcanoFlow::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "LavaHomeVolcanoFlow");
    }
    MR::startLevelSound(this, "SE_OJ_LV_VOLCANO_EXPL_SIGN");
    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        MR::emitEffect(this, "Active");
        MR::emitEffect(this, "Loop");
        MR::startSound(this, "SE_OJ_VOLCANO_EXPLODE");
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        setNerve(GET_NERVE(LavaHomeVolcanoFlow, HostTypeDone));
    }
}
