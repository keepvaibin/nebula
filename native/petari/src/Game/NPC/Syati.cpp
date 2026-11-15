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
#include "Game/System/LumaCore.hpp"

namespace {
    const char* const cBckForRipple[] = {"WaitReverse", "Talk", "StartWait", "StartTalk", "Delight", nullptr};

    const Vec sMarioMoveLocalOffsetRing = {6.0f, -33.0f, -776.0f};
    const Vec sMarioMoveLocalOffsetDeepSea = {6.0f, -176.0f, -590.0f};

    const Vec sTalkOffset = {16.0f, 192.0f, -300.0f};
    const Vec sTalkOffsetOnShore = {61.0f, 384.0f, 250.0f};
    const Vec sTalkOffsetDeepSea = {47.0f, 179.0f, -300.0f};
    const Vec sTalkOffsetDelightDeepSea = {152.0f, 240.0f, 250.0f};

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
    MR::addHitSensorAtJointEnemySimple(this, "head", "Jaw", 4, 200.0f, Vec3f(0.0f, -60.0f, 0.0f));
    MR::addHitSensorEnemySimple(this, "body", 4, 230.0f, Vec3f(0.0f, 0.0f, 0.0f));
    MR::addHitSensorAtJointEnemySimple(this, "tail", "Spin2", 4, 150.0f, Vec3f(0.0f, 0.0f, 0.0f));
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
    MR::setShadowDropLength(this, nullptr, 2000.0f);
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
    syatiUp.scale(450.0f);
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

    MR::setBckRate(this, 0.5f);
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

    MR::setBckRate(this, 0.5);
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

    if (MR::isGreaterStep(this, 1) && MR::isStageStatePowerStarAppeared()) {
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
        MR::setDistanceToTalk(mTalkMessageCtrl, 700.0f);

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

    if (MR::isNearPlayer(mTalkMessageCtrl, -1.0f)) {
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

    if (!MR::isNearPlayer(mTalkMessageCtrl, -1.0f)) {
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
    MR::setDistanceToTalk(mTalkMessageCtrl, 2500.0f);
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

    if (5000.0f <= MR::calcDistanceToPlayer(mPosition)) {
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
    MR::calcRailPosAtCoord(&direction, this, 600.0f + MR::getRailCoord(this));
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

    MR::vecBlend(mRailDirBlend, directionOnPlane, &mRailDirBlend, 0.04f);
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
        mPoseQuat.slerp(rotation, 0.04);
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

    if (600.0f < dist) {
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
    coord += 600.0f;
    MR::calcRailPosAtCoord(&position, this, coord);
    pPrizeRing->mPosition.set(position);
    Vec3f direction;
    MR::calcRailDirectionAtCoord(&direction, this, coord);
    Pos3f mtx;
    MR::calcMtxFromGravityAndZAxis(&mtx, this, mGravity, direction);

    Vec3f rotation;
    mtx.getEuler(rotation);
    pPrizeRing->mRotation.set< f32 >(rotation.x * 57.29578f, rotation.y * 57.29578f, rotation.z * 57.29578f);
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
