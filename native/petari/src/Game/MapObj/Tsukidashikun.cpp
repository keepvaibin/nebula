#include "Game/MapObj/Tsukidashikun.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEBULA_TUNABLE(f32, sDefaultMoveSpeed, 10.0f);
    const s32 sDefaultWaitTime = NebulaFrameClock::stepsOfSeconds(2);
    const s32 sStepForSign = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(f32, sRumblePadLength, 700.0f);
};  // namespace

namespace NrvTsukidashikun {
    NEW_NERVE(TsukidashikunNrvRelax, Tsukidashikun, Relax);
    NEW_NERVE(TsukidashikunNrvWaitBack, Tsukidashikun, Wait);
    NEW_NERVE(TsukidashikunNrvWaitForward, Tsukidashikun, Wait);
    NEW_NERVE(TsukidashikunNrvSignBack, Tsukidashikun, Sign);
    NEW_NERVE(TsukidashikunNrvSignForward, Tsukidashikun, Sign);
    NEW_NERVE(TsukidashikunNrvMoveBack, Tsukidashikun, Move);
    NEW_NERVE(TsukidashikunNrvMoveForward, Tsukidashikun, Move);
};  // namespace NrvTsukidashikun

Tsukidashikun::Tsukidashikun(const char* pName) : MapObjActor(pName), mMoveSpeed(::sDefaultMoveSpeed), mWaitTimer(::sDefaultWaitTime) {
    lumacore::setClass(this, lumacore::kClassId< Tsukidashikun >);
}

void Tsukidashikun::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect(nullptr);
    info.setupSound(4);
    info.setupGroupClipping(8);
    info.setupNerve(GET_NERVE(Tsukidashikun, TsukidashikunNrvRelax));
    initialize(rIter, info);
    MR::initLightCtrl(this);
    MR::getPlacementArg0NoInit(rIter, &mMoveSpeed);
    MR::getPlacementArg1NoInit(rIter, &mWaitTimer);
    MR::moveCoordToNearestPos(this, mPosition);
}

void Tsukidashikun::exeRelax() {
    if (MR::isFirstStep(this)) {
        MR::startBva(this, "BSign");
    }
}

void Tsukidashikun::exeWait() {
    const bool isBack = isNerve(GET_NERVE(Tsukidashikun, TsukidashikunNrvWaitBack));

    if (MR::isFirstStep(this)) {
        MR::startBva(this, isBack ? "BWait" : "FWait");
    }

    if (MR::isGreaterEqualStep(this, mWaitTimer)) {
        setNerve(isBack ? static_cast< const Nerve* >(GET_NERVE(Tsukidashikun, TsukidashikunNrvSignForward))
                        : GET_NERVE(Tsukidashikun, TsukidashikunNrvSignBack));
    }
}

void Tsukidashikun::exeSign() {
    const bool isForward = isNerve(GET_NERVE(Tsukidashikun, TsukidashikunNrvSignForward));

    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Sign");
        MR::startBva(this, isForward ? "FSign" : "BSign");
    }

    Vec3f pos;
    MR::calcPerpendicFootToLineInside(&pos, *MR::getPlayerPos(), MR::getRailPointPosStart(this), MR::getRailPointPosEnd(this));

    if (MR::isNearPlayer(pos, ::sRumblePadLength)) {
        MR::tryRumblePadWeak(this, PLAYER_1);
    }

    MR::startLevelSound(this, "SE_OJ_LV_TSUKIDASHI_VIB");

    if (MR::isStep(this, ::sStepForSign)) {
        MR::setBckFrameAndStop(this, 0.0f);
        setNerve(isForward ? static_cast< const Nerve* >(GET_NERVE(Tsukidashikun, TsukidashikunNrvMoveForward))
                           : GET_NERVE(Tsukidashikun, TsukidashikunNrvMoveBack));
    }
}

void Tsukidashikun::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_OJ_TSUKIDASHI_START");
    }

    MR::moveCoordAndFollowTrans(this, mMoveSpeed);
    MR::startLevelSound(this, "SE_OJ_LV_TSUKIDASHI_MOVE");

    if (MR::isRailReachedGoal(this)) {
        MR::startLevelSound(this, "SE_OJ_TSUKIDASHI_STOP");
        MR::reverseRailDirection(this);
        setNerve(isNerve(GET_NERVE(Tsukidashikun, TsukidashikunNrvMoveForward))
                     ? static_cast< const Nerve* >(GET_NERVE(Tsukidashikun, TsukidashikunNrvWaitForward))
                     : GET_NERVE(Tsukidashikun, TsukidashikunNrvWaitBack));
    }
}

void Tsukidashikun::connectToScene(const MapObjActorInitInfo& /*rInfo*/) {
    MR::connectToSceneCollisionMapObjStrongLight(this);
}

void Tsukidashikun::initCaseUseSwitchB(const MapObjActorInitInfo& /*rInfo*/) {
    MR::listenStageSwitchOnB(this, MR::Functor(this, &Tsukidashikun::startMove));
}

void Tsukidashikun::initCaseNoUseSwitchB(const MapObjActorInitInfo& /*rInfo*/) {
    startMove();
}

void Tsukidashikun::startMove() {
    setNerve(GET_NERVE(Tsukidashikun, TsukidashikunNrvWaitBack));
}
