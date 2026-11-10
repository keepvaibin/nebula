#include "Game/NPC/TeresaRacer.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/RaceRail.hpp"
#include "Game/MapObj/GCapture.hpp"
#include "Game/NPC/TalkMessageFunc.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NPCUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 220.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 25.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f6, "init.f6", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f7, "init.f7", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f1, "exeMove.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f2, "exeMove.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f3, "exeMove.f3", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeMove_f4, "exeMove.f4", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRacer_f1, "updateRacer.f1", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRacer_f2, "updateRacer.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_resetRacer_f1, "resetRacer.f1", 180.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_prepRacer_setRailCoordSpeed_1, "prepRacer.setRailCoordSpeed.1", 0.0f);

namespace {
    NEBULA_TUNABLE(f32, sSpeed, 20.0f);
    NEBULA_TUNABLE(f32, sAccele, 0.1f);
    NEBULA_TUNABLE(f32, sCameraDirScale, 0.4f);
    static const s32 sBgmChangeStep = NebulaFrameClock::stepsOfSeconds(8);
    NEBULA_TUNABLE(s32, sNearBgm_Fadetime, 90);
    NEBULA_TUNABLE(s32, sFarBgm_Fadetime, 150);
    NEBULA_TUNABLE(f32, sNearBgmDist, 3000.0f);
    NEBULA_TUNABLE(f32, sNearGoalDist, 500.0f);
    NEBULA_TUNABLE(f32, sRasterParamMax, 2500.0f);
    NEBULA_TUNABLE(s32, sRasterParamX, 300);
    NEBULA_TUNABLE(s32, sRasterParamTime, 16);
    NEBULA_TUNABLE(s32, sRasterParamY, 1000);
};  // namespace

namespace NrvTeresaRacer {
    NEW_NERVE(TeresaRacerNrvWait, TeresaRacer, Wait);
    NEW_NERVE(TeresaRacerNrvPre, TeresaRacer, Pre);
    NEW_NERVE(TeresaRacerNrvReady, TeresaRacer, Ready);
    NEW_NERVE(TeresaRacerNrvMove, TeresaRacer, Move);
    NEW_NERVE(TeresaRacerNrvGoal, TeresaRacer, Goal);
    NEW_NERVE(TeresaRacerNrvPost, TeresaRacer, Post);
    NEW_NERVE(TeresaRacerNrvTalk, TeresaRacer, Talk);
    NEW_NERVE(TeresaRacerNrvTakeOutStar, TeresaRacer, TakeOutStar);
};  // namespace NrvTeresaRacer

TeresaRacer::TeresaRacer(const char* pName) : NPCActor(pName), mTakeOutStar(), mCameraInfo(), mIsGoal(), mBgmState() {
}

void TeresaRacer::init(const TableIter& rIter) {
    NPCActorCaps caps("TeresaRacer");
    caps.setDefault();
    caps.setIndirect();
    caps.mLodCtrl = false;
    caps.mUseShadow = true;
    caps.mSensorSize = cNum_init_f1;
    caps.mSensorOffset.x = 0.0f;
    caps.mSensorOffset.y = cNum_init_f2;
    caps.mSensorOffset.z = cNum_init_f3;
    caps.mWaitNerve = GET_NERVE(TeresaRacer, TeresaRacerNrvWait);

    initialize(rIter, caps);

    MR::getPlacementArg0WithInit(rIter, &mRacerId);
    if (mRacerId == 0 && MR::isPlayerLuigi()) {
        makeActorDead();
    } else if (mRacerId == 1 && !MR::isPlayerLuigi()) {
        makeActorDead();
    } else {
        MR::declarePowerStar(this);

        MR::registerBranchFunc(getMsgCtrl(), TalkMessageFunc(this, &TeresaRacer::branchFunc));
        MR::registerAnimeFunc(getMsgCtrl(), TalkMessageFunc(this, &TeresaRacer::animeFunc));
        MR::registerEventFunc(getMsgCtrl(), TalkMessageFunc(this, &TeresaRacer::eventFunc));

        MR::setDistanceToTalk(getMsgCtrl(), cNum_init_f4);
        RaceManagerFunction::entryRacerOthers(this);
        MR::initMultiActorCamera(this, rIter, &mCameraInfo, "会話");  // conversation

        mParam.setMoveAction("WaitOpen", "WaitOpen");
        mParam.setTalkAction("WaitOpenTalk", "WaitOpenTalk");
        setTalkAction("Chase");
        mParam.mTurnSpeed = cNum_init_f5;
        mRailSpeed = ::sSpeed;
        mRailAccel = ::sAccele;
        mRailPoseRate = cNum_init_f6;
        mParam.mNearDistance = cNum_init_f7;

        mTakeOutStar = new TakeOutStar(this, "TakeOutStarTeresaRacer", "TakeOutStarTeresaRacer", GET_NERVE(TeresaRacer, TeresaRacerNrvTakeOutStar));
    }
}

void TeresaRacer::control() {
    if (mIsTrampledStart) {
        MR::startSound(this, "SE_SM_TERERACER_TRAMPLE");
    }
    NPCActor::control();
}

bool TeresaRacer::branchFunc(u32 /*state*/) {
    return RaceManagerFunction::getRaceRank() == 1;
}

bool TeresaRacer::animeFunc(u32 anime) {
    if (!isNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvReady))) {
        pushNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvReady));
    }

    if (MR::isBckOneTimeAndStopped(this)) {
        popNerve();
        return true;
    }

    switch (anime) {
    case 0:
        MR::tryStartAction(this, "Close");

        mParam.setTalkAction("CloseTalk", "CloseTalk");
        mParam.setMoveAction("CloseTalk", "CloseTalk");
        break;
    case 1:
        mParam.setMoveAction("Defeat", "Defeat");
        mParam.setTalkAction("Defeat", "Defeat");
        popNerve();
        return true;
    case 2:
        MR::tryStartAction(this, "Open");
        mParam.setTalkAction("WaitOpenTalk", "WaitOpenTalk");
        mParam.setMoveAction("WaitOpen", "WaitOpen");
        break;
    case 3:
        MR::tryStartAction(this, "Open");
        mParam.setTalkAction("WaitOpenTalk", "WaitOpenTalk");
        mParam.setMoveAction("WaitOpen", "WaitOpen");
        break;
    default:
        break;
    }

    return false;
}

bool TeresaRacer::eventFunc(u32 /*state*/) {
    return mTakeOutStar->takeOut();
}

void TeresaRacer::exeWait() {
    if (MR::tryTalkNearPlayerAndStartTalkAction(this)) {
        if (mRacerId == -1) {
            MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "会話");
        }
        setNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvPre));
    }
}

void TeresaRacer::exePre() {
    if (MR::isFirstStep(this)) {
        MR::resetGCapture();
    }

    if (!MR::isActionContinuous(this) && MR::tryTalkNearPlayerAtEndAndStartTalkAction(this)) {
        if (MR::tryTalkSelectLeft(getMsgCtrl())) {
            RaceManagerFunction::startRaceWithWipe();
            setNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvReady));
        } else {
            if (mRacerId == -1) {
                MR::endMultiActorCamera(this, mCameraInfo, "会話", false);
            }
            setNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvWait));
        }
    }
}

void TeresaRacer::exeReady() {
}

void TeresaRacer::exePost() {
    if (!MR::isActionContinuous(this) && MR::tryTalkForceAtEndAndStartTalkAction(this)) {
        MR::endMultiActorCamera(this, mCameraInfo, "会話", true);
        if (RaceManagerFunction::getRaceRank() == 1) {
            setNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvTalk));
        } else {
            MR::forceKillPlayerByGroundRace();
            setNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvReady));
        }
    }
}

void TeresaRacer::exeMove() {
    if (MR::isRailReachedNearGoal(this, ::sNearGoalDist) || !MR::isRailGoingToEnd(this)) {
        setNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvGoal));
    } else {
        MR::getCurrentRailPointArg0NoInit(this, &mRailSpeed);  // speed
        Vec3f up;
        MR::extractMtxYDir(getBaseMtx(), &up);
        MR::adjustmentRailCoordSpeed(this, mRailSpeed, mRailAccel);
        MR::moveRailRider(this);
        if (mRacerId == -1) {
            MR::decidePose(this, up, MR::getRailDirection(this), MR::getRailPos(this), 1.0f, cNum_exeMove_f1, cNum_exeMove_f2);
        } else {
            Vec3f dir = MR::getRailDirection(this) + -MR::getCamZdir() * ::sCameraDirScale;

            if (!MR::normalizeOrZero(&dir)) {
                MR::decidePose(this, up, dir, MR::getRailPos(this), 1.0f, cNum_exeMove_f3, cNum_exeMove_f4);
            }
        }
    }

    if (mRacerId != -1 && MR::calcDistanceToPlayer(this) < ::sRasterParamMax) {
        MR::setRasterScroll(::sRasterParamTime, ::sRasterParamX, ::sRasterParamY);
        MR::startAtmosphereLevelSE("SE_AT_LV_DEATH_PROM_RASTER");
    }
    changeBgmState();
}

void TeresaRacer::exeGoal() {
    if (MR::isFirstStep(this)) {
        mParam.mTurnWhenNear = true;
        mRailSpeed = 0.0f;
        mParam.mTurnOnTalk = true;
    }

    if (MR::getRailCoordSpeed(this) < 1.0f) {
        MR::tryStartMoveTurnAction(this);
    } else {
        MR::tryStartTurnAction(this);
    }

    if (mRacerId != -1 && MR::calcDistanceToPlayer(this) < ::sRasterParamMax) {
        MR::setRasterScroll(::sRasterParamTime, ::sRasterParamX, ::sRasterParamY);
        MR::startAtmosphereLevelSE("SE_AT_LV_DEATH_PROM_RASTER");
    }
    changeBgmState();
}

void TeresaRacer::exeTalk() {
    MR::tryTalkNearPlayerAndStartTalkAction(this);
}

void TeresaRacer::exeTakeOutStar() {
}

void TeresaRacer::initRacer() {
    AbstractRacer::initRacer();
}

void TeresaRacer::prepRacer(const RaceManager* /*pRaceManager*/) {
    MR::invalidateClipping(this);
    MR::moveCoordToStartPos(this);
    MR::setRailCoordSpeed(this, cNum_prepRacer_setRailCoordSpeed_1);
    MR::followRailPose(this, 1.0f, 1.0f);
    MR::requestMovementOn(this);
    MR::startAction(this, "Chase");
    mParam.mTurnWhenNear = false;
    mParam.mTurnOnTalk = false;
}

void TeresaRacer::startRacer() {
    mIsGoal = false;
    mBgmState = 0;
    setNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvMove));
}

bool TeresaRacer::updateRacer(const RaceManager* pRaceManager) {
    f32 coord = MR::calcNearestRailCoord(pRaceManager->mPlayerRacer, mPosition);
    f32 length = MR::getRailTotalLength(pRaceManager->mPlayerRacer);
    mPrevPosition.set(mCurrPosition);
    mCurrPosition.set(mPosition);
    if (MR::fabs(mRailCoord - coord) < cNum_updateRacer_f1) {
        mRailCoord = coord;
    }

    if (MR::fabs(mRailCoord - length) < cNum_updateRacer_f2) {
        mIsGoal = pRaceManager->isGoal(this);
        return mIsGoal;
    }

    return false;
}

bool TeresaRacer::goalRacer() {
    return false;
}

void TeresaRacer::loseRacer() {
}

void TeresaRacer::resetRacer(const RaceManager* pRaceManager) {
    if (pRaceManager->mCurrentRace == RaceID_TeresaPhantom) {
        MR::setDefaultPose(this);
    } else {
        MR::setNPCActorPos(this, "レース終了後位置テレサ");
        setInitPose();
    }

    turnToPlayer(cNum_resetRacer_f1);
    setNerve(GET_NERVE(TeresaRacer, TeresaRacerNrvPost));
    MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "会話");
    mParam.mTurnWhenNear = true;
    mParam.mTurnOnTalk = true;
}

void TeresaRacer::exitRacer() {
    MR::validateClipping(this);
}

void TeresaRacer::changeBgmState() {
    if (!MR::isLessStep(this, ::sBgmChangeStep) && !mIsGoal) {
        if (MR::calcDistanceToPlayer(this) < ::sNearBgmDist) {
            if (mBgmState != 2) {
                MR::setStageBGMState(2, ::sNearBgm_Fadetime);
            }
            mBgmState = 2;
        } else {
            if (mBgmState != 1) {
                MR::setStageBGMState(1, ::sFarBgm_Fadetime);
            }
            mBgmState = 1;
        }
    }
}
