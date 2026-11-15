#include "Game/MapObj/RockCreator.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/Rock.hpp"
#include "Game/Util.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvRockCreator {
    NEW_NERVE(RockCreatorNrvActive, RockCreator, Active);
    NEW_NERVE(RockCreatorNrvDeactive, RockCreator, Deactive);
};  // namespace NrvRockCreator

RockCreator::RockCreator(const char* pName)
    : LiveActor(pName), mRocks(nullptr), mRockSpeed(10.0f), mBreakModelOnRailGoal(false), mSpawnDelay(-1), mNumRocks(0), mSpawnTimer(0),
      mIsValid(true) {
    lumacore::setClass(this, lumacore::kClassId< RockCreator >);
}

void RockCreator::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);
    initMapToolInfo(rIter);
    initRailRider(rIter);
    initNerve(GET_NERVE(RockCreator, RockCreatorNrvActive));

    MR::needStageSwitchReadAppear(this, rIter);
    MR::syncStageSwitchAppear(this);

    if (MR::useStageSwitchReadA(this, rIter)) {
        MR::listenStageSwitchOnA(this, MR::Functor(this, &RockCreator::invalidate));
    }

    Rock::Type rockType = Rock::getType(rIter);
    if (mSpawnDelay < 0) {
        mNumRocks = 1;
        MR::declareStarPiece(this, Rock::getAppearStarPieceNum(rockType));
    } else {
        // A speed of 0 or less crosses the rail in no steps (the quotient would be infinite or negative).
        const f32 travelSteps = mRockSpeed > 0.0f ? MR::getRailTotalLength(this) / mRockSpeed : 0.0f;
        const s32 frames = static_cast< s32 >(travelSteps < 100000.0f ? travelSteps : 100000.0f) + Rock::getAppearFrame();
        mNumRocks = (mSpawnDelay != 0 ? frames / mSpawnDelay : frames < 0 ? -1 : 0) + 2;

        // At least one rock; a spawn delay of a step or two on a long rail holds at most 1024.
        if (mNumRocks < 1) {
            mNumRocks = 1;
        } else if (mNumRocks > 1024) {
            mNumRocks = 1024;
        }
        MR::declareStarPiece(this, Rock::getAppearStarPieceNum(rockType) * 3);
    }

    mRocks = new Rock[mNumRocks];
    for (s32 idx = 0; idx < mNumRocks; idx++) {
        mRocks[idx].mCreator = this;
        mRocks[idx].mMoveSpeed = mRockSpeed;
        mRocks[idx].init(rIter);
        if (mBreakModelOnRailGoal) {
            mRocks[idx].mBreakModelOnRailGoal = true;
        }
    }

    makeActorDead();
}

void RockCreator::appear() {
    if (mIsValid) {
        LiveActor::appear();
        mSpawnTimer = 0;
        MR::invalidateClipping(this);
        setNerve(GET_NERVE(RockCreator, RockCreatorNrvActive));
    }
}

void RockCreator::kill() {
    if (mIsValid) {
        LiveActor::kill();
        for (s32 idx = 0; idx < mNumRocks; idx++) {
            if (!MR::isDead(&mRocks[idx])) {
                mRocks[idx].kill();
            }
        }
    }
}

void RockCreator::invalidate() {
    if (mIsValid) {
        kill();
        mIsValid = false;
    }
}

void RockCreator::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mRockSpeed);

    if (MR::getPlacementArg1NoInit(rIter, &mSpawnDelay)) {
        mSpawnDelay *= NebulaFrameClock::stepsOfSeconds(1);
    }

    if (Rock::getType(rIter) == Rock::NormalRock) {
        MR::getPlacementArg2NoInit(rIter, &mBreakModelOnRailGoal);
    } else {
        // WanWan placements keep the flag in arg 5.
        MR::getPlacementArg5NoInit(rIter, &mBreakModelOnRailGoal);
    }
}

void RockCreator::create() {
    for (s32 idx = 0; idx < mNumRocks; idx++) {
        if (MR::isDead(&mRocks[idx])) {
            mRocks[idx].appear();
            return;
        }
    }
}

void RockCreator::exeDeactive() {
}

void RockCreator::exeActive() {
    if (mSpawnDelay < 0 || isReadyToSpawn()) {
        create();
        if (mSpawnDelay < 0) {
            setNerve(GET_NERVE(RockCreator, RockCreatorNrvDeactive));
            return;
        }
        mSpawnTimer = 0;
    }
    mSpawnTimer++;
}
