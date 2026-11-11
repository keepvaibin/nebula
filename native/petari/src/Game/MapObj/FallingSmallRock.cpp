#include "Game/MapObj/FallingSmallRock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 800.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 800.0f);

namespace NrvFallingSmallRock {
    NEW_NERVE(HostTypeWait, FallingSmallRock, Wait);
    NEW_NERVE(HostTypeMove, FallingSmallRock, Move);
};  // namespace NrvFallingSmallRock

FallingSmallRock::FallingSmallRock(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< FallingSmallRock >);
}

void FallingSmallRock::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initSound(4, false);
    initBinder(cNum_init_f1, cNum_init_f2, 0);
    initEffectKeeper(0, "FallingSmallRock");
    MR::needStageSwitchReadA(this, rIter);
    MR::setClippingTypeSphere(this, cNum_init_f3);
    initNerve(GET_NERVE(FallingSmallRock, HostTypeWait));
    MR::connectToSceneMapObjMovement(this);
    makeActorAppeared();
}

void FallingSmallRock::startClipped() {
    LiveActor::startClipped();
    if (isNerve(GET_NERVE(FallingSmallRock, HostTypeMove))) {
        MR::deleteEffectAll(this);
    }
}

void FallingSmallRock::endClipped() {
    LiveActor::endClipped();
    if (isNerve(GET_NERVE(FallingSmallRock, HostTypeMove))) {
        MR::emitEffect(this, "FallingSmallRock");
    }
}

void FallingSmallRock::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "FallingSmallRock");
    }

    MR::startLevelSound(this, "SE_AT_LV_FALLING_ROCK");

    if (MR::isBinded(this)) {
        makeActorDead();
    }
}

void FallingSmallRock::exeWait() {
    if (MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(FallingSmallRock, HostTypeMove));
    }
}
