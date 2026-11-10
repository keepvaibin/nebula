#include "Game/NPC/Syati.hpp"
#include "Game/LiveActor/ActorCameraInfo.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/RaceManager.hpp"
#include "Game/MapObj/PrizeRing.hpp"
#include "Game/NPC/TalkMessageCtrl.hpp"
#include "Game/Util.hpp"
#include "Game/Util/JointController.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f2, "init.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f3, "init.f3", 230.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f4, "init.f4", 150.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_init_f5, "init.f5", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 450.0f);
NEBULA_TUNABLE_NAMED(f64, cNum_exeSwim_f1, "exeSwim.f1", 0.5);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideOnShore_f1, "exeHideOnShore.f1", 700.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initTalking_f1, "initTalking.f1", 2500.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateSwimCommon_f1, "updateSwimCommon.f1", 5000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoseByRail_f1, "updatePoseByRail.f1", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updatePoseByRail_f2, "updatePoseByRail.f2", 0.04f);
NEBULA_TUNABLE_NAMED(f64, cNum_updatePoseByRail_f3, "updatePoseByRail.f3", 0.04);
NEBULA_TUNABLE_NAMED(f32, cNum_isReadyToEmitRing_f1, "isReadyToEmitRing.f1", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_emitRing_f1, "emitRing.f1", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_emitRing_f2, "emitRing.f2", 57.29578f);
NEBULA_TUNABLE_NAMED(f32, cNum_emitRing_f3, "emitRing.f3", 57.29578f);
NEBULA_TUNABLE_NAMED(f32, cNum_emitRing_f4, "emitRing.f4", 57.29578f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_exeCountDown_setBckRate_1, "exeCountDown.setBckRate.1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWaitTalkNormal_isNearPlayer_1, "exeWaitTalkNormal.isNearPlayer.1", -1.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeTalkNormal_isNearPlayer_1, "exeTalkNormal.isNearPlayer.1", -1.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeWaitStarAppeared_1, "exeWaitStarAppeared.1", 1);

namespace {
    const char* const cBckForRipple[] = {"WaitReverse", "Talk", "StartWait", "StartTalk", "Delight", nullptr};

    NEBULA_TUNABLE_RECORD(Vec, sMarioMoveLocalOffsetRing, {6.0f, -33.0f, -776.0f});
    NEBULA_TUNABLE_RECORD(Vec, sMarioMoveLocalOffsetDeepSea, {6.0f, -176.0f, -590.0f});

    NEBULA_TUNABLE_RECORD(Vec, sTalkOffset, {16.0f, 192.0f, -300.0f});
    NEBULA_TUNABLE_RECORD(Vec, sTalkOffsetOnShore, {61.0f, 384.0f, 250.0f});
    NEBULA_TUNABLE_RECORD(Vec, sTalkOffsetDeepSea, {47.0f, 179.0f, -300.0f});
    NEBULA_TUNABLE_RECORD(Vec, sTalkOffsetDelightDeepSea, {152.0f, 240.0f, 250.0f});

    const char* const cBckForTurnSplash[] = {"Turn", "TurnReverse", nullptr};
    const char* const cBckForTurn[] = {"Turn", "TurnReverse", nullptr};
};  // namespace

namespace NrvSyati {
    NEW_NERVE(SyatiWait, Syati, Wait);
    NEW_NERVE(SyatiFadeoutStartEvent, Syati, FadeoutBeforeTalk);
    NEW_NERVE(SyatiWaitBlankStartEvent, Syati, WaitBlank);
    NEW_NERVE(SyatiFadeinStartEvent, Syati, FadeinBeforeTalk);
    NEW_NERVE(SyatiTalkStartMission, Syati, TalkStartMission);
    NEW_NERVE(SyatiReadyToStart, Syati, ReadyToStart);
    NEW_NERVE(SyatiCountDown, Syati, CountDown);
    NEW_NERVE(SyatiSwim, Syati, Swim);
    NEW_NERVE(SyatiEmitRing, Syati, EmitRing);
    NEW_NERVE(SyatiWaitStarAppeared, Syati, WaitStarAppeared);
    NEW_NERVE(SyatiReachToEnd, Syati, ReachToEnd);
    NEW_NERVE(SyatiWaitAllRingDisappear, Syati, WaitAllRingDisappear);
    NEW_NERVE(SyatiFadeoutRetryEvent, Syati, FadeoutBeforeTalk);
    NEW_NERVE(SyatiWaitBlankRetryEvent, Syati, WaitBlank);
    NEW_NERVE(SyatiFadeinRetryEvent, Syati, FadeinBeforeTalk);
    NEW_NERVE(SyatiTalkRetryMission, Syati, TalkRetryMission);
    NEW_NERVE(SyatiForceKill, Syati, ForceKill);
    NEW_NERVE(SyatiHideOnShore, Syati, HideOnShore);
    NEW_NERVE(SyatiWaitOnShore, Syati, WaitOnShore);
    NEW_NERVE(SyatiWaitTalkNormal, Syati, WaitTalkNormal);
    NEW_NERVE(SyatiTalkNormal, Syati, TalkNormal);
    NEW_NERVE(SyatiWaitDemoStart, Syati, Start);
};  // namespace NrvSyati

Syati::Syati(const char* pName)
    : LiveActor(pName), mClippingRange(), mClippingVec(0.0f, 0.0f, 0.0f), mTalkMessageCtrl(), mActorCameraInfo(), mHideOnShoreMode(-1),
      mPlayerPoseSetterInWater(), mBlinkTimer(0xF0), mJointControlDelegator(), mRaceManagerLayout(), mPrizeRingGroup(), mNumRings(5),
      mStarSpawnType(-1), mSwimMode(-1), mPrizeRingLife(-1), mPrizeRingCount(), mCurrentRailPointNo(-1) {
    lumacore::setClass(this, lumacore::kClassId< Syati >);
    mPoseQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
    mMarioMoveLocalOffsetVec.zero();
    mBalloonFollowMtx.identity();
    mPrizeRingGroup = new LiveActorGroup("リング保持", 5);
}

void Syati::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::useStageSwitchReadA(this, rIter);
    MR::getPlacementArg2NoInit(rIter, &mSwimMode);
    initPose();
    initModelManagerWithAnm("Syati", nullptr, false);
    mJointControlDelegator = MR::createJointDelegatorWithNullChildFunc(this, &Syati::calcHeadJoint, "Head");
    const char* pObjectName = "";
    MR::getObjectName(&pObjectName, rIter);

    if (MR::isEqualString(pObjectName, "Syati")) {
        initRings(rIter);
        MR::declarePowerStar(this);
        initRailRider(rIter);
        MR::moveCoordAndTransToRailPoint(this, 0);
        mRaceManagerLayout = new RaceManagerLayout("レース管理用レイアウト");
        mRaceManagerLayout->initWithoutIter();
    }

    initTalking(rIter);
    MR::connectToSceneNoSilhouettedMapObj(this);
    initHitSensor(3);
    MR::addHitSensorAtJointEnemySimple(this, "head", "Jaw", 4, cNum_init_f1, Vec3f(0.0f, -cNum_init_f2, 0.0f));
    MR::addHitSensorEnemySimple(this, "body", 4, cNum_init_f3, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemySimple(this, "tail", "Spin2", 4, cNum_init_f4, Vec3f(0.0f, 0.0f, 0.0f));
    MR::calcModelBoundingRadius(&mClippingRange, this);
    MR::setClippingTypeSphere(this, mClippingRange);
    MR::validateClipping(this);
    initEffectKeeper(1, nullptr);

    if (mSwimMode) {
        MR::initEffectSyncBck(this, "Ripple", ::cBckForRipple);
        MR::initEffectSyncBck(this, "TurnSplash", ::cBckForTurnSplash);
    } else {
        MR::initEffectSyncBck(this, "Turn", ::cBckForTurn);
    }

    MR::initShadowVolumeFlatModel(this, "ShadowVolumeOval");
    MR::setShadowDropLength(this, nullptr, cNum_init_f5);
    MR::onCalcShadowOneTime(this, nullptr);
    MR::calcGravity(this);
    initSound(8, false);

    if (MR::isExistRail(this)) {
        initNerve(GET_NERVE(Syati, SyatiWait));
    } else {
        initNerve(GET_NERVE(Syati, SyatiHideOnShore));
    }

    makeActorAppeared();
}

void Syati::exeWait() {
    if (MR::isFirstStep(this)) {
        const char* pStr = mSwimMode == 0 ? "WaitDeepSea" : "WaitReverse";

        MR::startBck(this, pStr);
        MR::startBtk(this, pStr);
        MR::startBva(this, "Open");
    }

    if (MR::isOnSwitchA(this)) {
        MR::requestStartDemoMarioPuppetable(this, "開始デモ", GET_NERVE(Syati, SyatiFadeoutStartEvent), GET_NERVE(Syati, SyatiWaitDemoStart));
    }
}

void Syati::exeFadeoutBeforeTalk() {
    if (MR::isFirstStep(this)) {
        MR::closeWipeFade(-1);
        return;
    }

    if (!MR::isWipeActive()) {
        if (isNerve(GET_NERVE(Syati, SyatiFadeoutStartEvent))) {
            setNerve(GET_NERVE(Syati, SyatiWaitBlankStartEvent));
        } else {
            setNerve(GET_NERVE(Syati, SyatiWaitBlankRetryEvent));
        }
    }
}

void Syati::exeWaitBlank() {
    if (MR::isFirstStep(this)) {
        mMarioMoveLocalOffsetVec.set(mSwimMode == 0 ? ::sMarioMoveLocalOffsetDeepSea : ::sMarioMoveLocalOffsetRing);
        mPlayerPoseSetterInWater->mStep = 0;
        mPlayerPoseSetterInWater->update();

        MR::tryPlayerKillTakingActor();
        MR::startBckPlayer("SwimWait");
        MR::makeQuatFromRotate(&mPoseQuat, this);
    }

    if (MR::isStep(this, 0x1E)) {
        if (isNerve(GET_NERVE(Syati, SyatiWaitBlankStartEvent))) {
            setNerve(GET_NERVE(Syati, SyatiFadeinStartEvent));
        } else {
            setNerve(GET_NERVE(Syati, SyatiFadeinRetryEvent));
        }
    }
}

void PlayerPoseSetterInWater::update() {
    mStep++;
    mStep = (mStep + 90) % 90;


    Pos3f syatiMtx;
    syatiMtx.setQuat(mSyatiQuat);
    syatiMtx.setTrans(mSyatiPos);

    Vec3f offsetPos;
    offsetPos.zero();
    if (mOffset != nullptr) {
        offsetPos.set(*mOffset);
    }

    syatiMtx.mult(offsetPos, offsetPos);

    Vec3f forward = mSyatiPos;
    Vec3f syatiUp;
    syatiMtx.getYDir(syatiUp);
    syatiUp.scale(cNum_update_f1);
    forward.add(syatiUp);
    forward.sub(offsetPos);

    Vec3f offset;
    offset.zero();

    Pos3f baseMtx;
    const Vec3f& pos = offsetPos + offset;
    MR::makeMtxUpFrontPos(&baseMtx, forward, *MR::getPlayerGravity(), pos);
    MR::setPlayerBaseMtx(baseMtx);
}

void Syati::exeFadeinBeforeTalk() {
    if (MR::isFirstStep(this)) {
        MR::openWipeFade(-1);
        MR::startMultiActorCameraTargetSelf(this, mActorCameraInfo, "会話");
        MR::startBck(this, "Talk");
        MR::startBtk(this, "Talk");

        if (isNerve(GET_NERVE(Syati, SyatiFadeinRetryEvent)) && MR::isEqualStageName("OceanPhantomCaveGalaxy") &&
            !MR::isPlayingStageBgmName("STM_GALAXY_05")) {
            MR::stopStageBGM(0x3C);
        }
    }

    mPlayerPoseSetterInWater->update();

    if (!MR::isWipeActive()) {
        if (isNerve(GET_NERVE(Syati, SyatiFadeinStartEvent))) {
            setNerve(GET_NERVE(Syati, SyatiTalkStartMission));
        } else {
            setNerve(GET_NERVE(Syati, SyatiTalkRetryMission));
        }
    }
}

void Syati::exeTalkStartMission() {
    if (MR::isFirstStep(this)) {
        setupBalloonFollowMtx(Vec3f(mSwimMode == 0 ? ::sTalkOffsetDeepSea : ::sTalkOffset));
    }

    updateBlink();
    mPlayerPoseSetterInWater->update();

    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mTalkMessageCtrl)) {
        MR::endMultiActorCamera(this, mActorCameraInfo, "会話", false);
        setNerve(GET_NERVE(Syati, SyatiReadyToStart));
    }
}

void Syati::exeReadyToStart() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        resetScore();
        MR::requestMovementOn(mRaceManagerLayout);
        MR::startMultiActorCameraTargetSelf(this, mActorCameraInfo, "開始デモ");
        MR::startBck(this, "TurnReverse");
        MR::startBtk(this, "Normal");
        MR::startBva(this, "Open");
        MR::startSound(this, "SE_SM_SYATI_TURN");
        MR::startBckPlayer("SwimWait");
        MR::onCalcShadow(this, nullptr);
    }

    mPlayerPoseSetterInWater->update();

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Syati, SyatiCountDown));
    }
}

void Syati::exeCountDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Swim");
        mRaceManagerLayout->appear();
        mRaceManagerLayout->playCountAndGo();
        mRaceManagerLayout->hideRecordPane();
        mRaceManagerLayout->hideBestRecordPane();
    }

    if (getNerveStep() % 0x3C == 0) {
        MR::startSystemSE("SE_SY_RACE_COUNT_DOWN");
    }

    MR::setBckRate(this, cNum_exeCountDown_setBckRate_1);
    updateSwimCommon();
    mPlayerPoseSetterInWater->update();

    if (!mRaceManagerLayout->isPlayCountAnim()) {
        MR::endDemo(this, "開始デモ");
        MR::endMultiActorCamera(this, mActorCameraInfo, "開始デモ", true);
        MR::startSystemSE("SE_SY_RACE_START");
        setNerve(GET_NERVE(Syati, SyatiSwim));
    }
}

void Syati::exeSwim() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Swim");
    }

    MR::setBckRate(this, cNum_exeSwim_f1);
    updateSwimCommon();

    if (isReadyToEmitRing()) {
        setNerve(GET_NERVE(Syati, SyatiEmitRing));
    } else if (MR::isRailReachedGoal(this)) {
        setNerve(GET_NERVE(Syati, SyatiReachToEnd));
    }
}

void Syati::exeEmitRing() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Screw");
        MR::startSound(this, "SE_SM_SYATI_ROLL");
        emitRing();
    }

    updateSwimCommon();

    if (isReadyToEmitRing()) {
        setNerve(GET_NERVE(Syati, SyatiEmitRing));
    } else if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Syati, SyatiSwim));
    }
}

void Syati::exeWaitStarAppeared() {
    if (MR::isFirstStep(this)) {
        MR::invalidateClipping(this);
        MR::startBckWithInterpole(this, "Star", 0);
        MR::startBtk(this, "Normal");
        calcAnim();

        Vec3f starPosition;
        MR::copyJointPos(this, "Head", &starPosition);
        MR::requestAppearPowerStar(this, mStarSpawnType, starPosition);
    }

    if (MR::isGreaterStep(this, cStep_exeWaitStarAppeared_1) && MR::isStageStatePowerStarAppeared()) {
        kill();
    }
}

void Syati::exeReachToEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "Turn");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(Syati, SyatiWaitAllRingDisappear));
    }
}

void Syati::exeWaitAllRingDisappear() {
    if (MR::isFirstStep(this)) {
        const char* pStr = mSwimMode == 0 ? "WaitDeepSea" : "WaitReverse";

        MR::startBck(this, pStr);
        MR::startBtk(this, pStr);
        MR::resetNode(mTalkMessageCtrl);
        MR::forwardNode(mTalkMessageCtrl);
    }

    if (mPrizeRingGroup->getLivingActorNum() == 0 && mPrizeRingCount != mNumRings) {
        MR::requestStartDemoMarioPuppetable(this, "再挑戦デモ", GET_NERVE(Syati, SyatiFadeoutRetryEvent), GET_NERVE(Syati, SyatiWaitDemoStart));
    }
}

void Syati::exeTalkRetryMission() {
    if (MR::isFirstStep(this)) {
        setupBalloonFollowMtx(Vec3f(mSwimMode == 0 ? ::sTalkOffsetDeepSea : ::sTalkOffset));
        MR::startBck(this, "Failure");
        MR::startBtk(this, "Failure");

        if (MR::isEqualStageName("OceanPhantomCaveGalaxy") && !MR::isPlayingStageBgmName("STM_GALAXY_05")) {
            MR::startStageBGM("MBGM_GALAXY_05", false);
        }
    }

    mPlayerPoseSetterInWater->update();
    updateBlink();

    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mTalkMessageCtrl)) {
        MR::endDemo(this, "再挑戦デモ");
        MR::endMultiActorCamera(this, mActorCameraInfo, "会話", true);
        setNerve(GET_NERVE(Syati, SyatiForceKill));
    }
}

void Syati::exeForceKill() {
    if (MR::isFirstStep(this)) {
        MR::forceKillPlayerByWaterRace();
    }
}

void Syati::exeHideOnShore() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::invalidateHitSensors(this);
        MR::setDistanceToTalk(mTalkMessageCtrl, cNum_exeHideOnShore_f1);

        const Vec* pVec = mSwimMode == 0 ? &::sTalkOffsetDelightDeepSea : &::sTalkOffsetOnShore;

        setupBalloonFollowMtx(Vec3f(*pVec));
    }

    switch (mHideOnShoreMode) {
    case 0:
        if (MR::isStageStatePowerStarAppeared()) {
            setNerve(GET_NERVE(Syati, SyatiWaitOnShore));
        }
        break;
    case 1:
        setNerve(GET_NERVE(Syati, SyatiWaitTalkNormal));
        break;
    }
}

void Syati::exeWaitOnShore() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::validateHitSensors(this);
        const char* pStr = mSwimMode == 0 ? "DelightDeepSea" : "Delight";

        MR::startBck(this, pStr);
        MR::startBtk(this, pStr);
        MR::startBva(this, "Open");
        MR::onCalcShadowOneTime(this, nullptr);
    }

    updateBlink();
    MR::tryTalkNearPlayer(mTalkMessageCtrl);
}

void Syati::exeWaitTalkNormal() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::validateHitSensors(this);
        MR::startBckWithInterpole(this, "StartWait", 0xF);
        MR::startBtk(this, "StartWait");
        MR::startBva(this, "Open");
    }

    updateBlink();
    MR::tryTalkNearPlayer(mTalkMessageCtrl);

    if (MR::isNearPlayer(mTalkMessageCtrl, cNum_exeWaitTalkNormal_isNearPlayer_1)) {
        setNerve(GET_NERVE(Syati, SyatiTalkNormal));
    }
}

void Syati::exeTalkNormal() {
    if (MR::isFirstStep(this)) {
        MR::startBckWithInterpole(this, "StartTalk", 0xF);
        MR::startBtk(this, "StartTalk");
    }

    updateBlink();
    MR::tryTalkNearPlayer(mTalkMessageCtrl);

    if (!MR::isNearPlayer(mTalkMessageCtrl, cNum_exeTalkNormal_isNearPlayer_1)) {
        setNerve(GET_NERVE(Syati, SyatiWaitTalkNormal));
    }
}

void Syati::exeStart() {
}

void Syati::initRings(const TableIter& rIter) {
    MR::getPlacementArg0NoInit(rIter, &mNumRings);
    MR::getPlacementArg3NoInit(rIter, &mPrizeRingLife);

    for (int i = 0; i < 5; i++) {
        PrizeRing* pNewRing = new PrizeRing();
        pNewRing->initWithoutIter();
        mPrizeRingGroup->registerActor(pNewRing);
    }
}

PlayerPoseSetterInWater::PlayerPoseSetterInWater(const Vec3f& rVec, Syati* pSyati)
    : mSyatiPos(rVec), mSyatiQuat(pSyati->mPoseQuat), mStep(), mOffset(&pSyati->mMarioMoveLocalOffsetVec) {
}

void Syati::initPose() {
    MR::makeQuatFromRotate(&mPoseQuat, this);
    mPlayerPoseSetterInWater = new PlayerPoseSetterInWater(mPosition, this);
}

void Syati::initTalking(const TableIter& rIter) {
    mTalkMessageCtrl = MR::createTalkCtrl(this, rIter, "SyatiRing", Vec3f(0.0f, 0.0f, 0.0f), mBalloonFollowMtx);
    MR::setDistanceToTalk(mTalkMessageCtrl, cNum_initTalking_f1);
    mActorCameraInfo = MR::createActorCameraInfo(rIter);
    MR::initMultiActorCamera(this, rIter, &mActorCameraInfo, "会話");
    MR::initMultiActorCamera(this, rIter, &mActorCameraInfo, "開始デモ");
    MR::getPlacementArg1NoInit(rIter, &mHideOnShoreMode);
    const char* pObjectName = "";
    MR::getObjectName(&pObjectName, rIter);

    if (!MR::isEqualString(pObjectName, "Syati")) {
        MR::onRootNodeAutomatic(mTalkMessageCtrl);
    }
}

void Syati::updateSwimCommon() {
    s32 speed = -1;
    MR::getRailPointArg1NoInit(this, MR::getCurrentRailPointNo(this), &speed);

    if (cNum_updateSwimCommon_f1 <= MR::calcDistanceToPlayer(mPosition)) {
        speed = 3;
    }

    MR::moveCoordAndFollowTrans(this, static_cast< f32 >(speed));
    updatePoseByRail();

    if (!MR::isDead(mRaceManagerLayout) && !mRaceManagerLayout->isPlayCountAndGoAnim()) {
        mRaceManagerLayout->kill();
    }

    MR::startLevelSound(this, "SE_SM_LV_SYATI_SWIM");
}

void Syati::updatePoseByRail() {
    Vec3f direction;
    MR::calcRailPosAtCoord(&direction, this, cNum_updatePoseByRail_f1 + MR::getRailCoord(this));
    Vec3f railPosition;
    MR::calcRailPosAtCoord(&railPosition, this, MR::getRailCoord(this));
    direction.sub(railPosition);

    Vec3f railDirectionOnPlane(MR::getRailDirection(this));
    MR::vecKillElement(railDirectionOnPlane, mGravity, &railDirectionOnPlane);
    MR::normalizeOrZero(&railDirectionOnPlane);
    Vec3f directionOnPlane(direction);
    MR::vecKillElement(directionOnPlane, mGravity, &directionOnPlane);

    if (!MR::isNearZero(directionOnPlane)) {
        MR::normalize(&directionOnPlane);
    } else {
        directionOnPlane.set(railDirectionOnPlane);
    }

    MR::vecBlend(mRailDirBlend, directionOnPlane, &mRailDirBlend, cNum_updatePoseByRail_f2);
    Vec3f turnAxis = railDirectionOnPlane.cross(mRailDirBlend);
    MR::normalizeOrZero(&turnAxis);
    Quat4f roll;
    roll.set(0.0f, 0.0f, 0.0f, 1.0f);
    Vec3f up = -mGravity;
    f32 dot = railDirectionOnPlane.dot(mRailDirBlend);
    f32 rad = MR::acos(dot);

    if (dot < 1.0f) {
        f32 side = turnAxis.dot(mGravity);
        if (side < 0.0f) {
            roll.setEulerZ(-1.0f * rad);
        } else if (0.0f < side) {
            roll.setEulerZ(rad);
        }
    }

    if (!MR::isSameDirection(direction, up)) {
        Quat4f rotation;
        MR::makeQuatFromVec(&rotation, direction, up);
        rotation.mult(rotation, roll);
        mPoseQuat.slerp(rotation, cNum_updatePoseByRail_f3);
    }
}

void Syati::updateNumRingPassed() {
    bool isRingPassed = false;

    for (int i = 0; i < mPrizeRingGroup->getObjNum(); i++) {
        PrizeRing* pPrizeRing = static_cast< PrizeRing* >(mPrizeRingGroup->getActor(i));

        if (MR::isDead(pPrizeRing)) {
            continue;
        }

        if (pPrizeRing->isReadyToPass()) {
            pPrizeRing->setNervePass();
            isRingPassed = true;
            mPrizeRingCount++;
            MR::incPlayerLife(1);
        }

        if (pPrizeRing->isReadyToKill()) {
            pPrizeRing->kill();
        } else {
            continue;
        }

        if (mPrizeRingCount == mNumRings) {
            killAllRings();
            setNerve(GET_NERVE(Syati, SyatiWaitStarAppeared));
        }
    }

    if (isRingPassed && mPrizeRingCount != mNumRings) {
        syncNumRingLeftToActiveRings();
    }
}

void Syati::updateBlink() {
    mBlinkTimer--;

    if (mBlinkTimer < 0) {
        MR::startBva(this, "Blink");
        s16 frame = MR::getBvaCtrl(this)->mEnd;
        u32 rand = MR::getRandom(0x78, 0xF0);
        mBlinkTimer = frame + rand;
    } else if (MR::isBvaStopped(this)) {
        MR::startBva(this, "Open");
    }
}

bool Syati::isReadyToEmitRing() const {
    if (mPrizeRingCount == mNumRings) {
        return false;
    }

    if (mPrizeRingGroup->getDeadActor() == nullptr) {
        return false;
    }

    const s32 currentPoint = MR::getCurrentRailPointNo(this);
    if (currentPoint == mCurrentRailPointNo) {
        return false;
    }

    f32 dist = 0.0f;
    MR::calcDistanceToNextRailPoint(this, &dist);

    if (cNum_isReadyToEmitRing_f1 < dist) {
        return false;
    }

    s32 arg = -1;
    s32 point = MR::getNextRailPointNo(this);
    MR::getRailPointArg0NoInit(this, point, &arg);
    return arg == 0;
}

void Syati::syncNumRingLeftToActiveRings() {
    PrizeRing* pPrizeRing;

    for (int i = 0; i < 5; i++) {
        pPrizeRing = static_cast< PrizeRing* >(mPrizeRingGroup->getActor(i));
        if (MR::isDead(pPrizeRing)) {
            continue;
        }

        if (pPrizeRing->isPassed()) {
            continue;
        }

        pPrizeRing->setNumber(mNumRings - mPrizeRingCount);
    }
}

void Syati::resetScore() {
    mPrizeRingCount = 0;
    mCurrentRailPointNo = -1;
    MR::resetNode(mTalkMessageCtrl);
    killAllRings();
    MR::moveCoordToRailPoint(this, 0);
}

void Syati::killAllRings() {
    for (int i = 0; i < 5; i++) {
        PrizeRing* pPrizeRing = static_cast< PrizeRing* >(mPrizeRingGroup->getActor(i));

        if (!MR::isDead(pPrizeRing)) {
            pPrizeRing->kill();
        }
    }
}

void Syati::emitRing() {
    PrizeRing* pPrizeRing = static_cast< PrizeRing* >(mPrizeRingGroup->getDeadActor());
    Vec3f position;
    f32 coord = MR::getRailCoord(this);
    coord += cNum_emitRing_f1;
    MR::calcRailPosAtCoord(&position, this, coord);
    pPrizeRing->mPosition.set(position);
    Vec3f direction;
    MR::calcRailDirectionAtCoord(&direction, this, coord);
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, direction);

    Vec3f rotation;
    mtx.getEuler(rotation);
    pPrizeRing->mRotation.set< f32 >(rotation.x * cNum_emitRing_f2, rotation.y * cNum_emitRing_f3, rotation.z * cNum_emitRing_f4);
    pPrizeRing->appear();
    pPrizeRing->setNumber(mNumRings - mPrizeRingCount);

    if (mPrizeRingLife > 0) {
        pPrizeRing->setLife(mPrizeRingLife);
    }

    mCurrentRailPointNo = MR::getCurrentRailPointNo(this);
}

void Syati::setupBalloonFollowMtx(const Vec3f& rVec) {
    mBalloonFollowMtx.setQuat(mPoseQuat);
    mBalloonFollowMtx.setTrans(mPosition);

    Vec3f position = rVec;
    mBalloonFollowMtx.mult(position, position);
    mBalloonFollowMtx.setTrans(position);
}

bool Syati::calcHeadJoint(Pos3f* pPos, const JointControllerInfo& /*rInfo*/) {
    bool nrv = false;
    nrv = isNerve(GET_NERVE(Syati, SyatiWaitOnShore)) || isNerve(GET_NERVE(Syati, SyatiWaitTalkNormal)) || isNerve(GET_NERVE(Syati, SyatiTalkNormal));

    if (!nrv) {
        return false;
    }

    Pos3f mtx;
    Vec3f playerDirection(*MR::getPlayerPos());
    playerDirection.sub(mPosition);
    MR::normalizeOrZero(&playerDirection);

    if (MR::isNearZero(playerDirection)) {
        return false;
    }

    Vec3f playerDirectionOnPlane(playerDirection);
    playerDirectionOnPlane.y = 0.0f;
    MR::normalizeOrZero(&playerDirectionOnPlane);

    if (MR::isNearZero(playerDirectionOnPlane)) {
        return false;
    }

    Vec3f front;
    mPoseQuat.getZDir(front);
    MR::normalize(&front);
    Quat4f rotation;
    rotation.set(0.0f, 0.0f, 0.0f, 1.0f);
    MR::turnQuat(&rotation, rotation, front, playerDirectionOnPlane, PI / 6);
    mtx.setQuat(rotation);
    mtx.zeroTrans();
    pPos->concat(*pPos, mtx);
    return true;
}

void Syati::control() {
    MR::setClippingRangeIncludeShadow(this, &mClippingVec, mClippingRange);
    updateNumRingPassed();
}

void Syati::calcAndSetBaseMtx() {
    MR::setBaseTRMtx(this, mPoseQuat);
    mJointControlDelegator->registerCallBack();
}

void Syati::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isSensorPlayer(pReceiver)) {
        if (MR::sendMsgEnemyAttackFlipMaximum(pReceiver, pSender)) {
            MR::startSound(this, "SE_SM_SYATI_TRAMPLED");
        }
    } else {
        MR::sendMsgPush(pReceiver, pSender);
    }
}

bool Syati::receiveMsgPlayerAttack(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (MR::isMsgJetTurtleAttack(msg) || MR::isMsgStarPieceReflect(msg)) {
        return true;
    }

    return false;
}
