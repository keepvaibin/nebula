#include "Game/MapObj/AstroCore.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/GameLightSignal.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/AstroDemoFunction.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE_LIST(f32, sSensorSizeTable, , {100.0f, 300.0f, 350.0f, 420.0f, 480.0f, 540.0f, 600.0f, 0.0f});
    NEBULA_TUNABLE(s32, sGrowTimeLag, 40);
};  // namespace

namespace NrvAstroCore {
    NEW_NERVE(AstroCoreNrvWait, AstroCore, Wait);
    NEW_NERVE(AstroCoreNrvGrow, AstroCore, Grow);
};  // namespace NrvAstroCore

AstroCore::AstroCore(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< AstroCore >);
}

void AstroCore::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    info.setupNerve(GET_NERVE(AstroCore, AstroCoreNrvWait));
    info.setupHitSensor();
    info.setupHitSensorParam(8, ::sSensorSizeTable[0], Vec3f(0.0f, 0.0f, 0.0f));
    info.setupFarClipping(-1.0f);
    info.setupSound(4);
    info.setupNoAppearRiddleSE();
    initialize(rIter, info);
    AstroDemoFunction::tryRegisterGrandStarReturnWithFunctionAndSimpleCast(this, rIter, MR::Functor(this, &AstroCore::startDemo));
    GameLightSignal::registerStarCore(this);
}

void AstroCore::exeWait() {
    if (MR::isFirstStep(this)) {
        setStateBeforeGrow();
    }
}

void AstroCore::exeGrow() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::shakeCameraNormalWeak();
        MR::emitEffect(this, "ShockWave");
    }

    if (MR::isStep(this, ::sGrowTimeLag)) {
        MR::startSound(this, "SE_OJ_ASTRO_CORE_GROW");
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::shakeCameraNormalWeak();
        startAnimGrow();
    }
}

bool AstroCore::receiveOtherMsg(u32 /*msg*/, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    return false;
}

void AstroCore::startDemo() {
    setNerve(GET_NERVE_ANON(NrvAstroCore::AstroCoreNrvGrow));
}

void AstroCore::startAnimGrow() {
    s32 stage = MR::clamp(AstroDemoFunction::getOpenedAstroDomeNum() + 1, 0, 6);
    MR::startAllAnim(this, MR::FormattedName("Revival%d", stage).c_str());
    getSensor(nullptr)->mRadius = ::sSensorSizeTable[stage];
}

void AstroCore::setStateBeforeGrow() {
    s32 stage = MR::clamp(AstroDemoFunction::getOpenedAstroDomeNum(), 0, 6);
    const MR::FormattedName animName("Revival%d", stage);
    MR::startAllAnim(this, animName.c_str());
    MR::setAllAnimFrameAtEnd(this, animName.c_str());
    getSensor(nullptr)->mRadius = ::sSensorSizeTable[stage];
}

void AstroCore::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        MR::sendMsgPush(pReceiver, pSender);
    }
}
