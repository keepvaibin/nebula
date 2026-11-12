#include "Game/Map/RaceRail.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // A player further than this from the rail is not judged; a coord jump this long is not taken.
    const f32 cRaceJudgeLength = 10000.0f;
    const f32 cGoalReach = 100.0f;
    // The Penguin race starts on the water: cast from cWaterCheckHeight above, cWaterCheckLength down, and sink by cWaterSinkDepth.
    const f32 cWaterCheckHeight = 500.0f;
    const f32 cWaterCheckLength = 1000.0f;
    const f32 cWaterSinkDepth = 15.0f;
    const f32 cGroundCheckLength = 1000.0f;
}  // namespace

RaceRail::RaceRail(const char* pName) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< RaceRail >);
}

void RaceRail::init(const TableIter& rIter) {
    mPlayerRacer = new PlayerRacer("レース判定用レール");
    mPlayerRacer->init(rIter);
    RaceManagerFunction::entryRacerPlayer(mPlayerRacer);
}

PlayerRacer::PlayerRacer(const char* pName) : LiveActor(pName), mCameraInfo() {
    lumacore::setClass(this, lumacore::kClassId< PlayerRacer >);
}

void PlayerRacer::appear() {
    RaceManagerFunction::startRaceImmediately();
}

void PlayerRacer::init(const TableIter& rIter) {
    MR::connectToSceneMapObjMovement(this);
    initRailRider(rIter);
    mCameraInfo = MR::createActorCameraInfo(rIter);
    MR::initActorCamera(this, rIter, &mCameraInfo);

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }

    MR::invalidateClipping(this);
}

void PlayerRacer::initRacer() {
    AbstractRacer::initRacer();
    MR::startBckPlayer("Watch");
}

void PlayerRacer::prepRacer(const RaceManager* pRaceManager) {
    MR::moveCoordToStartPos(this);
    MR::moveTransToCurrentRailPos(this);

    Vec3f gravity;
    MR::calcGravityVector(this, mPosition, &gravity, nullptr, 0);

    Pos3f mtx;
    Vec3f pos;

    switch (pRaceManager->mCurrentRace) {
    case RaceID_Penguin: {
        Vec3f checkStart = mPosition - gravity * ::cWaterCheckHeight;
        Vec3f waterPos(mPosition);
        MR::getFirstPolyOnLineToWaterSurface(&waterPos, nullptr, checkStart, gravity * ::cWaterCheckLength);
        waterPos -= gravity * ::cWaterSinkDepth;
        MR::makeMtxFrontUpPos(&mtx, MR::getRailDirection(this), -gravity, waterPos);
        MR::startBckPlayer("SwimDrift", 1);
        break;
    }
    case RaceID_TeresaPhantom: {
        pos = mPosition;
        MR::convertPosOnGround(&pos, gravity * ::cGroundCheckLength);
        MR::makeMtxFrontUpPos(&mtx, MR::getRailDirection(this), -gravity, pos);
        MR::startBckPlayer("BattleWait", 1);
        break;
    }
    case RaceID_TeresaDeathPromenade: {
        pos = mPosition;
        MR::makeMtxFrontUpPos(&mtx, MR::getRailDirection(this), -gravity, pos);
        MR::startBckPlayer("Wait", 1);
        MR::onFollowDemoEffect();
        break;
    }
    }

    MR::tryPlayerKillTakingActor();
    MR::setPlayerBaseMtx(mtx);
    MR::startActorCameraTargetPlayer(this, mCameraInfo);
}

void PlayerRacer::startRacer() {
    MR::endActorCamera(this, mCameraInfo, true);
}

// Progress takes the player's nearest rail coord only when it moved less than cRaceJudgeLength; inside one tagged
// rail section (point arg0 not -1) it is taken outright.
bool PlayerRacer::updateRacer(const RaceManager* pRaceManager) {
    PlayerRacer* pRail = pRaceManager->mPlayerRacer;
    f32 nearestCoord = MR::calcNearestRailCoord(pRail, *MR::getPlayerCenterPos());
    f32 railLength = MR::getRailTotalLength(pRail);

    mPrevPosition.set(mCurrPosition);
    mCurrPosition.set(*MR::getPlayerCenterPos());

    Vec3f railPos;
    MR::calcNearestRailPos(&railPos, pRail, *MR::getPlayerCenterPos());

    if ((railPos - *MR::getPlayerCenterPos()).length() > ::cRaceJudgeLength) {
        return false;
    }

    if (MR::fabs(mRailCoord - nearestCoord) < ::cRaceJudgeLength) {
        mRailCoord = nearestCoord;
    }

    s32 sectionBefore;
    MR::getCurrentRailPointArg0WithInit(this, &sectionBefore);
    MR::setRailCoord(this, mRailCoord);
    s32 sectionAfter;
    MR::getCurrentRailPointArg0WithInit(this, &sectionAfter);

    if (sectionBefore != -1 && sectionBefore == sectionAfter) {
        mRailCoord = nearestCoord;
    }

    if (MR::fabs(mRailCoord - railLength) < ::cGoalReach && pRaceManager->isGoal(this)) {
        MR::tryRumblePadVeryStrong(this, PLAYER_1);
        return true;
    }

    return false;
}

void PlayerRacer::resetRacer(const RaceManager* pRaceManager) {
    MR::tryPlayerKillTakingActor();
    MR::setPlayerPosOnGround("レース終了後位置");

    switch (pRaceManager->mCurrentRace) {
    case RaceID_Penguin:
    case RaceID_TeresaPhantom:
    case RaceID_SurfingTrial:
    case RaceID_SurfingChallenge:
        MR::startBckPlayer("Watch", 0);
        break;
    case RaceID_TeresaDeathPromenade:
        MR::onFollowDemoEffect();
        MR::startBckPlayer(pRaceManager->mRank != 1 ? "Sad" : "Wait", 0);
        break;
    }

    MR::progressPlayerBckOnPause();
    MR::resetPlayerStatus();
    MR::resetPlayerEffect();
}
