#include "Game/MapObj/MeteorStrikeLauncher.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MeteorStrike.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 4096.0f);

namespace NrvMeteorStrikeLauncher {
    NEW_NERVE(MeteorStrikeLauncherNrvCreate, MeteorStrikeLauncher, Create);
    NEW_NERVE(MeteorStrikeLauncherNrvInterval, MeteorStrikeLauncher, Interval);
};  // namespace NrvMeteorStrikeLauncher

namespace {
    NEBULA_TUNABLE(f32, sClipRadius, 200.0f);
};  // namespace

MeteorStrikeLauncher::MeteorStrikeLauncher(const char* pName)
    : LiveActor(pName), mMeteorStrikeArray(), mMeteorStrikeCount(), mIntervalStep(-1), mCreateStep(), mIsMeteorStrike(), mIsValidMultiMeteor() {
}

void MeteorStrikeLauncher::init(const TableIter& rIter) {
    const char* pName = mName;
    setName("メテオストライクランチャー");

    initMapToolInfo(rIter);

    MR::connectToSceneMapObjMovement(this);

    initRailRider(rIter);
    MR::moveCoordAndTransToRailPoint(this, 0);

    initNerve(GET_NERVE(MeteorStrikeLauncher, MeteorStrikeLauncherNrvCreate));

    MR::needStageSwitchReadAppear(this, rIter);
    MR::syncStageSwitchAppear(this);

    if (mIsMeteorStrike && mIsValidMultiMeteor) {
        f32 speed = MeteorStrike::getSpeed(rIter);
        // A count that is not a number, or is 4096 and over, keeps the one meteor (the cast of such a float is undefined).
        f32 count = MR::getRailTotalLength(this) / (speed * mIntervalStep);
        mMeteorStrikeCount = (count > -1.0f && count < cNum_init_f1) ? static_cast< s32 >(count) + 2 : 1;
    } else if (mIsMeteorStrike || mIntervalStep < 0) {
        mMeteorStrikeCount = 1;
    } else {
        mMeteorStrikeCount = 2;
    }

    // A speed or interval of zero or less makes the count negative (or undefined): keep the one meteor.
    if (mMeteorStrikeCount < 1) {
        mMeteorStrikeCount = 1;
    }

    mMeteorStrikeArray = new MeteorStrike*[mMeteorStrikeCount];

    for (s32 idx = 0; idx < mMeteorStrikeCount; idx++) {
        mMeteorStrikeArray[idx] = new MeteorStrike(pName);
        mMeteorStrikeArray[idx]->init(rIter);
    }

    makeActorDead();
}

void MeteorStrikeLauncher::appear() {
    LiveActor::appear();

    MR::invalidateClipping(this);

    setNerve(GET_NERVE(MeteorStrikeLauncher, MeteorStrikeLauncherNrvCreate));
}

void MeteorStrikeLauncher::kill() {
    LiveActor::kill();

    if (mIntervalStep < 0) {
        return;
    }

    for (s32 idx = 0; idx < mMeteorStrikeCount; idx++) {
        if (!MR::isDead(mMeteorStrikeArray[idx])) {
            mMeteorStrikeArray[idx]->kill();
        }
    }
}

void MeteorStrikeLauncher::initMapToolInfo(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    MR::useStageSwitchReadAppear(this, rIter);

    if (MR::getPlacementArg1NoInit(rIter, &mIntervalStep)) {
        mIntervalStep *= NebulaFrameClock::stepsOfSeconds(1);
    }

    mIsMeteorStrike = MR::isEqualObjectName(rIter, "MeteorStrike");

    MR::getPlacementArg2NoInit(rIter, &mIsValidMultiMeteor);
}

MeteorStrike* MeteorStrikeLauncher::getUnusedMeteorStrike() {
    for (s32 idx = 0; idx < mMeteorStrikeCount; idx++) {
        if (MR::isDead(mMeteorStrikeArray[idx])) {
            return mMeteorStrikeArray[idx];
        }
    }

    return nullptr;
}

bool MeteorStrikeLauncher::create() {
    MeteorStrike* pUnusedStrike = getUnusedMeteorStrike();
    if (pUnusedStrike == nullptr) {
        mCreateStep++;
        return false;
    }

    if (!mIsMeteorStrike) {
        pUnusedStrike->appear();
        return true;
    }

    if (mIsValidMultiMeteor) {
        Vec3f movedPos;
        pUnusedStrike->getMovedPos(&movedPos, 0);

        if (MR::isJudgedToClipFrustum(movedPos, ::sClipRadius)) {
            pUnusedStrike->appear();
        }

        return true;
    }

    // Starts the meteor part way along its fall, at the latest step that is still out of view.
    // With no interval (0 or less) the step counts up by 1 and ends past the meteor's last step.
    for (; mCreateStep >= 0; mCreateStep -= (mIntervalStep != 0 ? mIntervalStep : -1)) {
        Vec3f movedPos;
        if (!pUnusedStrike->getMovedPos(&movedPos, mCreateStep)) {
            if (mIntervalStep <= 0) {
                break;
            }
            continue;
        }

        if (!MR::isJudgedToClipFrustum(movedPos, ::sClipRadius)) {
            continue;
        }

        pUnusedStrike->appear(mCreateStep);
        return true;
    }

    mCreateStep = 0;

    return false;
}

void MeteorStrikeLauncher::exeCreate() {
    if (MR::isFirstStep(this)) {
        mCreateStep = 0;
    }

    if (!create()) {
        return;
    }

    if (mIntervalStep < 0) {
        kill();
        return;
    }

    setNerve(GET_NERVE(MeteorStrikeLauncher, MeteorStrikeLauncherNrvInterval));
}

void MeteorStrikeLauncher::exeInterval() {
    if (MR::isStep(this, mIntervalStep)) {
        setNerve(GET_NERVE(MeteorStrikeLauncher, MeteorStrikeLauncherNrvCreate));
    }
}
