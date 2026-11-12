#include "Game/MapObj/TimerMoveWall.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActor.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sDefaultTimer = NebulaFrameClock::stepsOfSeconds(5);
    const f32 sDefaultMoveLength = 1000.0f;
};  // namespace

namespace NrvTimerMoveWall {
    NEW_NERVE(TimerMoveWallNrvWait, TimerMoveWall, Wait);
    NEW_NERVE(TimerMoveWallNrvMoveFront, TimerMoveWall, Move);
    NEW_NERVE(TimerMoveWallNrvMoveBack, TimerMoveWall, Move);
};  // namespace NrvTimerMoveWall

TimerMoveWall::TimerMoveWall(const char* pName) : MapObjActor(pName), mTimer(::sDefaultTimer), mMoveLength(::sDefaultMoveLength) {
    lumacore::setClass(this, lumacore::kClassId< TimerMoveWall >);
}

void TimerMoveWall::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    info.setupSound(4);
    info.setupNerve(GET_NERVE(TimerMoveWall, TimerMoveWallNrvWait));
    initialize(rIter, info);
    MR::getPlacementArg0NoInit(rIter, &mTimer);
    MR::getPlacementArg1NoInit(rIter, &mMoveLength);
}

void TimerMoveWall::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::validateClipping(this);
    }
}

void TimerMoveWall::exeMove() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        Vec3f frontVec;
        MR::calcFrontVec(&frontVec, this);

        if (isNerve(GET_NERVE(TimerMoveWall, TimerMoveWallNrvMoveBack))) {
            frontVec.negate();
        }

        // A timer under one step moves the whole length in one step.
        mVelocity.scale(mTimer > 0 ? mMoveLength / mTimer : mMoveLength, frontVec);
        MR::startSound(this, "SE_OJ_TRIBBL_T_MVWALL_ST");
    }

    MR::startLevelSound(this, "SE_OJ_LV_TRIBBL_T_MVWALL_MV");

    if (MR::isStep(this, mTimer)) {
        mVelocity.zero();

        if (isNerve(GET_NERVE(TimerMoveWall, TimerMoveWallNrvMoveFront))) {
            setNerve(GET_NERVE(TimerMoveWall, TimerMoveWallNrvMoveBack));
        } else {
            MR::startSound(this, "SE_OJ_TRIBBL_T_MVWALL_ED");
            setNerve(GET_NERVE(TimerMoveWall, TimerMoveWallNrvWait));
        }
    }
}

void TimerMoveWall::initCaseUseSwitchB(const MapObjActorInitInfo& /*rInfo*/) {
    MR::listenStageSwitchOnB(this, MR::Functor(this, &TimerMoveWall::startMove));
}

void TimerMoveWall::startMove() {
    setNerve(GET_NERVE(TimerMoveWall, TimerMoveWallNrvMoveFront));
}
