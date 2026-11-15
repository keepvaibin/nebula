#include "Game/MapObj/RailBlock.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const s32 sStartSignTime = NebulaFrameClock::stepsOfSeconds(1);
    const s32 sCycle = 3;
    const f32 sAmplitude = 30.0f;
};  // namespace

namespace NrvRailBlock {
    NEW_NERVE(RailBlockNrvWait, RailBlock, Wait);
    NEW_NERVE(RailBlockNrvMove, RailBlock, Move);
    NEW_NERVE(RailBlockNrvTerminate, RailBlock, Terminate);
};  // namespace NrvRailBlock

RailBlock::RailBlock(const char* pName) : LiveActor(pName), mRailSpeed(10), mMoveTime(100), mIsConnectedToRail() {
    lumacore::setClass(this, lumacore::kClassId< RailBlock >);
}

void RailBlock::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    const char* pObjName = nullptr;
    MR::getObjectName(&pObjName, rIter);
    initModelManagerWithAnm(pObjName, nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addHitSensorMapObjPress(this, "body", 16, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::initCollisionParts(this, pObjName, getSensor("body"), nullptr);
    MR::setClippingTypeSphere(this, MR::getCollisionBoundingSphereRange(this));
    initSound(4, false);

    mIsConnectedToRail = MR::isConnectedWithRail(rIter);
    if (mIsConnectedToRail) {
        initRailRider(rIter);
        MR::moveCoordToNearestPos(this, mPosition);
    }

    MR::setGroupClipping(this, rIter, 16);

    MR::getPlacementArg0NoInit(rIter, &mRailSpeed);
    MR::getPlacementArg1NoInit(rIter, &mMoveTime);

    initNerve(GET_NERVE(RailBlock, RailBlockNrvMove));

    if (MR::useStageSwitchReadB(this, rIter)) {
        MR::listenStageSwitchOnOffB(this, MR::Functor(this, &RailBlock::startMove), MR::Functor(this, &RailBlock::stopMove));
        setNerve(GET_NERVE(RailBlock, RailBlockNrvWait));
    }

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void RailBlock::startMove() {
    setNerve(GET_NERVE(RailBlock, RailBlockNrvMove));
}

void RailBlock::stopMove() {
    setNerve(GET_NERVE(RailBlock, RailBlockNrvWait));
}

void RailBlock::exeMove() {
    if (!mIsConnectedToRail) {
        return;
    }

    MR::moveCoordAndFollowTrans(this, mRailSpeed);
    MR::startLevelSound(this, "SE_OJ_LV_TSUKIDASHI_MOVE");
    if (MR::isRailReachedGoal(this)) {
        MR::startLevelSound(this, "SE_OJ_TSUKIDASHI_STOP");
        setNerve(GET_NERVE(RailBlock, RailBlockNrvTerminate));
    }
}

// Shakes at the rail end for sStartSignTime steps before it turns back.
void RailBlock::exeTerminate() {
    if (getNerveStep() >= mMoveTime - ::sStartSignTime) {
        s32 step = getNerveStep() - mMoveTime + ::sStartSignTime;
        f32 rate = static_cast< f32 >(step % ::sCycle) / ::sCycle;

        f32 coord = (step / ::sCycle) % 2 == 0 ? ::sAmplitude * rate : ::sAmplitude * (1.0f - rate);
        if (MR::isRailGoingToEnd(this)) {
            coord = MR::getRailTotalLength(this) - coord;
        }

        MR::calcRailPosAtCoord(&mPosition, this, coord);
        MR::startLevelSound(this, "SE_OJ_LV_TSUKIDASHI_VIB");
    }

    if (getNerveStep() >= mMoveTime) {
        MR::reverseRailDirection(this);
        MR::startSound(this, "SE_OJ_TSUKIDASHI_START");
        setNerve(GET_NERVE(RailBlock, RailBlockNrvMove));
    }
}

void RailBlock::exeWait() {
}
