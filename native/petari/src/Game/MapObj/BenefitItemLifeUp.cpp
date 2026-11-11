#include "Game/MapObj/BenefitItemLifeUp.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <nebula_format.h>
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_initModelAndEfx_f1, "initModelAndEfx.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModelAndEfx_f2, "initModelAndEfx.f2", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModelAndEfx_f3, "initModelAndEfx.f3", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initModelAndEfx_f4, "initModelAndEfx.f4", 50.0f);

// Never on a stage where a dark comet shows.
BenefitItemLifeUp::BenefitItemLifeUp(const char* pName) : BenefitItemObj(pName, "KinokoLifeUp") {
    lumacore::setClass(this, lumacore::kClassId< BenefitItemLifeUp >);
    if (MR::isGalaxyDarkCometAppearInCurrentStage()) {
        mIsDisabled = true;
    }
}

void BenefitItemLifeUp::exeCatch() {
    MR::hideModel(this);
    MR::changePlayerItemStatus(PlayerItemStatus_LifeUp);
    runEfx("Get");
    kill();
}

void BenefitItemLifeUp::initModelAndEfx() {
    initModelManagerWithAnm(mModelName, nullptr, false);
    initEffectKeeper(5, "BenefitItemObj");

    initBinder(cNum_initModelAndEfx_f1, cNum_initModelAndEfx_f2, 0);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 4, cNum_initModelAndEfx_f3, Vec3f(0.0f, cNum_initModelAndEfx_f4, 0.0f));
}

// The shared item effects under this item's names: `pName` + "LifeUp".
void BenefitItemLifeUp::runEfx(const char* pName) {
    MR::FormattedName str("%sLifeUp", pName);
    MR::emitEffect(this, str.c_str());
}

void BenefitItemLifeUp::stopEfx(const char* pName) {
    MR::FormattedName str("%sLifeUp", pName);
    MR::emitEffect(this, str.c_str());
}
