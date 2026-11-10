#include "Game/MapObj/LavaBallRisingPlanetLava.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeScaleUp_f1, "exeScaleUp.f1", 1.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeScaleDown_f1, "exeScaleDown.f1", 1.1f);

namespace NrvLavaBallRisingPlanetLava {
    NEW_NERVE(LavaBallRisingPlanetLavaNrvWaitSmall, LavaBallRisingPlanetLava, Wait);
    NEW_NERVE(LavaBallRisingPlanetLavaNrvWaitBig, LavaBallRisingPlanetLava, Wait);
    NEW_NERVE(LavaBallRisingPlanetLavaNrvScaleUp, LavaBallRisingPlanetLava, ScaleUp);
    NEW_NERVE(LavaBallRisingPlanetLavaNrvScaleDown, LavaBallRisingPlanetLava, ScaleDown);
};  // namespace NrvLavaBallRisingPlanetLava

namespace {
    const s32 sWaitStep = NebulaFrameClock::stepsOfSeconds(2);
    const s32 sScaleStep = NebulaFrameClock::stepsOfSeconds(4);
};  // namespace

LavaBallRisingPlanetLava::LavaBallRisingPlanetLava(const char* pName) : MapObjActor(pName) {
}

void LavaBallRisingPlanetLava::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect(nullptr);
    info.setupSound(4);
    info.setupNerve(GET_NERVE(LavaBallRisingPlanetLava, LavaBallRisingPlanetLavaNrvWaitSmall));
    initialize(rIter, info);
}

void LavaBallRisingPlanetLava::exeWait() {
    MR::startLevelSound(this, "SE_AT_LV_MAGMA_WIND_RISING");

    if (!MR::isLessStep(this, ::sWaitStep)) {
        if (isNerve(GET_NERVE(LavaBallRisingPlanetLava, LavaBallRisingPlanetLavaNrvWaitSmall))) {
            setNerve(GET_NERVE(LavaBallRisingPlanetLava, LavaBallRisingPlanetLavaNrvScaleUp));
        } else {
            setNerve(GET_NERVE(LavaBallRisingPlanetLava, LavaBallRisingPlanetLavaNrvScaleDown));
        }
    }
}

void LavaBallRisingPlanetLava::exeScaleUp() {
    updateScale(1.0f, cNum_exeScaleUp_f1, GET_NERVE(LavaBallRisingPlanetLava, LavaBallRisingPlanetLavaNrvWaitBig));
}

void LavaBallRisingPlanetLava::exeScaleDown() {
    updateScale(cNum_exeScaleDown_f1, 1.0f, GET_NERVE(LavaBallRisingPlanetLava, LavaBallRisingPlanetLavaNrvWaitSmall));
}

void LavaBallRisingPlanetLava::updateScale(f32 startScale, f32 endScale, const Nerve* pNextNerve) {
    MR::startLevelSound(this, "SE_AT_LV_MAGMA_WIND_RISING");
    MR::startLevelSound(this, "SE_OJ_LV_LAVA_MOVE_RISING");
    mScale.setAll< f32 >(MR::calcNerveValue(this, ::sScaleStep, startScale, endScale));

    if (MR::isStep(this, ::sScaleStep)) {
        setNerve(pNextNerve);
    }
}
