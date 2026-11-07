#include "Game/MapObj/BenefitItemLifeUp.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include <nebula_format.h>
#include "Game/Util/StringUtil.hpp"

BenefitItemLifeUp::BenefitItemLifeUp(const char* pName) : BenefitItemObj(pName, "KinokoLifeUp") {
    if (MR::isGalaxyDarkCometAppearInCurrentStage()) {
        _D8 = 1;
    }
}

void BenefitItemLifeUp::exeCatch() {
    MR::hideModel(this);

    MR::changePlayerItemStatus(0xA);

    runEfx("Get");

    kill();
}

void BenefitItemLifeUp::initModelAndEfx() {
    initModelManagerWithAnm(_120, nullptr, false);
    initEffectKeeper(5, "BenefitItemObj", false);

    initBinder(50.0f, 50.0f, 0);
    initHitSensor(1);
    MR::addHitSensorMapObj(this, "body", 4, 50.0f, Vec3f(0.0f, 50.0f, 0.0f));
}

void BenefitItemLifeUp::runEfx(const char* pName) {
    MR::FormattedName str("%sLifeUp", pName);
    MR::emitEffect(this, str.c_str());
}

void BenefitItemLifeUp::stopEfx(const char* pName) {
    MR::FormattedName str("%sLifeUp", pName);
    MR::emitEffect(this, str.c_str());
}
