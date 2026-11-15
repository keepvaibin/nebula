#include "Game/MapObj/EarthenPipe.hpp"
#include "Game/AudioLib/AudBgmMgr.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/GameAudio/AudStageBgmTable.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvEarthenPipe {
    NEW_NERVE(EarthenPipeNrvWait, EarthenPipe, Wait);
    NEW_NERVE(EarthenPipeNrvReady, EarthenPipe, Ready);
    NEW_NERVE(EarthenPipeNrvPlayerIn, EarthenPipe, PlayerIn);
    NEW_NERVE(EarthenPipeNrvTargetPipeShowUp, EarthenPipe, TargetPipeShowUp);
    NEW_NERVE(EarthenPipeNrvPlayerOut, EarthenPipe, PlayerOut);
    NEW_NERVE(EarthenPipeNrvInvalid, EarthenPipe, Invalid);
    NEW_NERVE(EarthenPipeNrvHide, EarthenPipe, Hide);
    NEW_NERVE(EarthenPipeNrvShow, EarthenPipe, Show);
    NEW_NERVE(EarthenPipeNrvWaitToShowUp, EarthenPipe, WaitToShowUp);
    NEW_NERVE(EarthenPipeNrvShowUp, EarthenPipe, ShowUp);
    NEW_NERVE(EarthenPipeNrvWaitToHideDown, EarthenPipe, WaitToHideDown);
    NEW_NERVE(EarthenPipeNrvHideDown, EarthenPipe, HideDown);
};  // namespace NrvEarthenPipe

namespace {
    // Placement arg 2.
    enum PipeMode {
        PIPE_NORMAL = 0,
        // Hidden; rises to take the player in and sinks again after.
        PIPE_HIDDEN_RETURN = 1,
        // Hidden; rises the first time and then stays as a normal pipe.
        PIPE_HIDDEN_ONCE = 2,
        // Short collision.
        PIPE_SHORT = 3,
        // Usable again once the player is sRearmDistance away, not only after landing.
        PIPE_REARM_WHEN_AWAY = 4,
    };

    const f32 sRearmDistance = 100.0f;
    // The rise out of the lava takes half a second.
    const s32 sRiseSteps = NebulaFrameClock::stepsOfSeconds(1) / 2;

    bool isPlayerAway(const LiveActor* pActor) {
        return !MR::isNearPlayer(pActor, sRearmDistance);
    }
};  // namespace

EarthenPipe::EarthenPipe(const char* pName)
    : LiveActor(pName), mBasePos(0.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mPipeLength(100.0f), mIsIgnoreGravity(false), mPipeMode(PIPE_NORMAL),
      mTargetPipe(nullptr), mTopJointMtx(nullptr), mBottomJointMtx(nullptr), mHostActor(nullptr), mHorizExitForce(5.0f), mVertExitForce(20.0f),
      mMusicChangeIdx(-1), mMusicState(-1), mIsRabbitWait(false), mPipeStreamModel(nullptr), mIsWaterFlowing(false), mCameraInfo(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< EarthenPipe >);
    mPlayerMtx.identity();
    mPlayerInStartMtx.identity();
    mPlayerInEndMtx.identity();
    mLavaEffectMtx.identity();
}

void EarthenPipe::init(const TableIter& rIter) {
    MR::createSceneObj(SceneObj_EarthenPipeMediator);
    MR::getSceneObj< EarthenPipeMediator >(SceneObj_EarthenPipeMediator)->entry(this, rIter);
    MR::initDefaultPos(this, rIter);
    MR::getPlacementArg0NoInit(rIter, &mHorizExitForce);
    MR::getPlacementArg1NoInit(rIter, &mVertExitForce);
    MR::getPlacementArg2NoInit(rIter, &mPipeMode);
    MR::getPlacementArg4NoInit(rIter, &mMusicChangeIdx);
    MR::getPlacementArg5NoInit(rIter, &mMusicState);
    MR::getPlacementArg6NoInit(rIter, &mIsIgnoreGravity);
    s32 colorFrame = 0;
    MR::getPlacementArg7NoInit(rIter, &colorFrame);
    bool isWaterPipe = MR::isObjectName(rIter, "EarthenPipeInWater");

    mBasePos.set(mPosition);
    MR::calcGravity(this);
    initModelManagerWithAnm("EarthenPipe", nullptr, false);
    mTopJointMtx = MR::getJointMtx(this, "Top");
    mBottomJointMtx = MR::getJointMtx(this, "Bottom");
    MR::startBrk(this, "EarthenPipe");
    MR::setBrkFrameAndStop(this, colorFrame);
    Pos3f placeMtx;
    placeMtx.identity();
    MR::makeMtxTR(placeMtx, this);
    Vec3f front;
    placeMtx.getZDir(front);

    // The pipe points up its placement (or against gravity); the bottom joint sits 50 below the top.
    if (mIsIgnoreGravity) {
        placeMtx.getYDir(mUp);
    } else {
        mUp = mGravity * -1.0f;
    }

    Pos3f jointMtx;
    jointMtx.identity();
    MR::makeMtxUpFrontPos(&jointMtx, mUp, front, mBasePos);
    NebulaMtxCopy(jointMtx, mTopJointMtx);
    mLavaEffectMtx.setInline(jointMtx);
    Vec3f bottomPos(mBasePos);
    Vec3f bottomOffset(mUp);
    bottomOffset.scale(50.0f);
    bottomPos.sub(bottomOffset);
    jointMtx.setTrans(bottomPos);
    NebulaMtxCopy(jointMtx, mBottomJointMtx);
    MR::setBaseTRMtx(this, mTopJointMtx);
    MR::connectToSceneMapObjNoCalcAnimStrongLight(this);

    if (isWaterPipe) {
        initHitSensor(3);
    } else {
        initHitSensor(1);
    }

    MR::addHitSensorBinder(this, "binder", 8, 70.0f, Vec3f(0.0f, -50.0f, 0.0f));

    if (isWaterPipe) {
        MR::addHitSensorBinder(this, "BinderWaterUpper", 8, 200.0f, Vec3f(0.0f, 180.0f, 0.0f));
        MR::addHitSensorBinder(this, "BinderWaterLower", 8, 80.0f, Vec3f(0.0f, 50.0f, 0.0f));
    }

    if (mPipeMode == PIPE_SHORT) {
        MR::initCollisionParts(this, "EarthenPipeS", getSensor("binder"), mTopJointMtx);
    } else {
        MR::initCollisionParts(this, "EarthenPipe", getSensor("binder"), mTopJointMtx);
    }

    // The placement Y scale is the pipe length (in 100s); the model is moved, not stretched.
    mPipeLength = 100.0f * mScale.y;
    mScale.y = 1.0f;
    calcTrans(1.0f);
    initEffectKeeper(0, nullptr);
    MR::setEffectHostMtx(this, "LavaAppear", mLavaEffectMtx.mMtx);
    MR::setEffectHostMtx(this, "LavaVanish", mLavaEffectMtx.mMtx);
    initSound(4, false);
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "出現");
    initNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWait));

    if (mPipeMode == PIPE_HIDDEN_RETURN || mPipeMode == PIPE_HIDDEN_ONCE) {
        MR::invalidateCollisionParts(this);
        calcTrans(0.0f);
        MR::invalidateHitSensors(this);
        MR::hideModel(this);
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvHide));
    }

    if (isWaterPipe) {
        mPipeStreamModel = MR::createPartsModelMapObj(this, "土管水流", "EarthenPipeStream", nullptr);
        MR::setClippingTypeSphere(mPipeStreamModel, 500.0f);
        MR::tryStartAllAnim(mPipeStreamModel, "EarthenPipeStream");
        mPipeStreamModel->appear();
    }

    mIsWaterFlowing = isWaterPipe;

    MR::useStageSwitchReadA(this, rIter);
    MR::useStageSwitchWriteB(this, rIter);
    MR::useStageSwitchSleep(this, rIter);
    if (MR::tryRegisterDemoCast(this, rIter)) {
        if (mPipeStreamModel != nullptr) {
            MR::registerDemoSimpleCastAll(mPipeStreamModel);
        }
    } else if (MR::isEqualStageName("HeavensDoorGalaxy")) {
        MR::registerDemoSimpleCastAll(this);
    }

    makeActorAppeared();
}

void EarthenPipe::makeActorAppeared() {
    LiveActor::makeActorAppeared();
    if (isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvHide))) {
        MR::invalidateCollisionParts(this);
    }
}

bool EarthenPipe::tryShowUp() {
    MR::invalidateClipping(this);
    MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "出現");
    if (mPipeMode != PIPE_HIDDEN_RETURN && mPipeMode != PIPE_HIDDEN_ONCE) {
        return false;
    }

    setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToShowUp));
    return true;
}

bool EarthenPipe::tryHideDown() {
    MR::validateClipping(this);
    MR::endMultiActorCamera(this, mCameraInfo, "出現", true);

    // Every mode that stays up waits for the player to leave; any other (PIPE_HIDDEN_RETURN, or a value outside
    // the enum) sinks.
    if (mPipeMode == PIPE_HIDDEN_ONCE) {
        mPipeMode = PIPE_NORMAL;
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvInvalid));
        return false;
    }

    if (mPipeMode == PIPE_NORMAL || mPipeMode == PIPE_SHORT || mPipeMode == PIPE_REARM_WHEN_AWAY) {
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvInvalid));
        return false;
    }

    setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToHideDown));
    return true;
}

bool EarthenPipe::isNerveShowUp() const {
    return isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToShowUp)) || isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvShowUp));
}

void EarthenPipe::exeWait() {
    if (MR::isValidSwitchA(this)) {
        if (MR::isOnSwitchA(this)) {
            if (!MR::isValidHitSensor(this, "binder")) {
                MR::validateHitSensors(this);
                if (mPipeStreamModel != nullptr) {
                    mPipeStreamModel->appear();
                    MR::startAction(mPipeStreamModel, "Appear");
                    mIsWaterFlowing = true;
                }
            }
        } else if (MR::isValidHitSensor(this, "binder")) {
            MR::invalidateHitSensors(this);
            if (mPipeStreamModel != nullptr) {
                mPipeStreamModel->kill();
            }

            mIsWaterFlowing = false;
        }
    }
}

void EarthenPipe::exeReady() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("EarthenPipeReady");
    }

    MR::blendMtx(mPlayerInStartMtx.mMtx, mPlayerInEndMtx.mMtx, MR::calcNerveRate(this, MR::getBckFrameMaxPlayer()), mPlayerMtx.mMtx);
    if (MR::isBckStoppedPlayer() && !MR::isPlayerDead()) {
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvPlayerIn));
    }
}

void EarthenPipe::exePlayerIn() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("EarthenPipeIn");
        MR::startSystemSE("SE_DM_DOKAN_WARP");
        processBgmPlayerIn();
    }

    if (MR::isBckStopped(mHostActor) && !MR::isPlayerDead()) {
        mPlayerMtx.setInline(mTargetPipe->getBaseMtx());
        if (mTargetPipe->tryShowUp()) {
            setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvTargetPipeShowUp));
        } else {
            setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvPlayerOut));
        }
    }
}

void EarthenPipe::exeTargetPipeShowUp() {
    if (MR::isFirstStep(this)) {
        MR::hidePlayer();
    }

    if (!mTargetPipe->isNerveShowUp()) {
        mPlayerMtx.setInline(mTargetPipe->getBaseMtx());
        MR::showPlayer();
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvPlayerOut));
    }
}

void EarthenPipe::exePlayerOut() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("EarthenPipeOut");
        MR::startSystemSE("SE_DM_DOKAN_WARP");
        processBgmPlayerOut();
    }

    if (MR::isBckStopped(mHostActor)) {
        MtxPtr baseMtx = mTargetPipe->getBaseMtx();
        // Jumps out up the target pipe, and forward unless the pipe is under water.
        Vec3f exitFront(baseMtx[0][2], baseMtx[1][2], baseMtx[2][2]);
        Vec3f exitUp(baseMtx[0][1], baseMtx[1][1], baseMtx[2][1]);
        exitFront *= mTargetPipe->mHorizExitForce;
        exitUp *= mTargetPipe->mVertExitForce;
        Vec3f exitVel(exitUp);

        if (!MR::isInWater(mTargetPipe->mPosition)) {
            exitVel.add(exitFront);
        }

        MR::startSound(mHostActor, "SE_PM_JUMP_M");
        MR::startSound(mHostActor, "SE_PV_JUMP_JOY");
        MR::startBckPlayer("EarthenPipeJump", 0);
        MR::endBindAndPlayerForceWeakGravityJumpInputOff(this, exitVel);
        mHostActor = nullptr;
        mTargetPipe->tryHideDown();

        if (!mIsRabbitWait && MR::isValidSwitchB(this)) {
            MR::onSwitchB(this);
        }

        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvInvalid));
    }
}

// Usable again once the player has landed (or swum or, for PIPE_REARM_WHEN_AWAY, moved) away.
void EarthenPipe::exeInvalid() {
    if ((MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(500)) && MR::isOnGroundPlayer()) || (mPipeMode == PIPE_REARM_WHEN_AWAY && ::isPlayerAway(this)) ||
        (MR::isPlayerSwimming() && ::isPlayerAway(this))) {
        MR::validateClipping(this);
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWait));
    }
}

void EarthenPipe::exeHide() {}

void EarthenPipe::exeShow() {}

void EarthenPipe::exeWaitToHideDown() {
    if (MR::isStep(this, 40)) {
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvHideDown));
    }
}

void EarthenPipe::exeWaitToShowUp() {
    if (MR::isStep(this, 20)) {
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvShowUp));
    }
}

void EarthenPipe::exeShowUp() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::validateCollisionParts(this);
        MR::emitEffect(this, "LavaAppear");
        MR::startSystemSE("SE_SY_ITEM_APPEAR");
    }

    f32 easeIn = MR::getEaseInValue(static_cast< f32 >(getNerveStep()) / static_cast< f32 >(sRiseSteps), 0.0f, 1.0f, 1.0f);
    calcTrans(easeIn);

    if (MR::isLessStep(this, sRiseSteps)) {
        MR::startLevelSound(this, "SE_OJ_LV_PIPE_APPEAR_MAGMA");
    }

    if (MR::isStep(this, sRiseSteps)) {
        MR::deleteEffect(this, "LavaAppear");
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvShow));
    }
}

void EarthenPipe::exeHideDown() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "LavaVanish");
    }

    f32 easeIn = MR::getEaseInValue((15 - getNerveStep()) / 15.0f, 0.0f, 1.0f, 1.0f);
    calcTrans(easeIn);
    if (MR::isLessStep(this, 15)) {
        MR::startLevelSound(this, "SE_OJ_LV_PIPE_VANISH_MAGMA");
    }

    if (MR::isStep(this, 15)) {
        MR::hideModel(this);
        MR::invalidateCollisionParts(this);
        MR::validateClipping(this);
        MR::deleteEffect(this, "LavaVanish");
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvHide));
    }
}

void EarthenPipe::control() {
    MR::updateMaterial(this);
    if (mIsWaterFlowing) {
        MR::startLevelSound(this, "SE_OJ_LV_PIPE_IN_WATER");
    }
}

bool EarthenPipe::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* /*pReceiver*/) {
    if (MR::isMsgAutoRushBegin(msg)) {
        // A pipe with no partner (a mod's placement) takes nobody in.
        if (mTargetPipe == nullptr || isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvInvalid)) || isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvHide)) ||
            isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToShowUp)) || isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvShowUp)) ||
            isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToHideDown)) || isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvHideDown)) ||
            MR::isPlayerDead()) {
            return false;
        }

        // Out of water the player must be over the mouth: within 50 across, not more than 5 below the top.
        Vec3f diff = *MR::getPlayerPos() - Vec3f(mTopJointMtx[0][3], mTopJointMtx[1][3], mTopJointMtx[2][3]);
        Vec3f delta = diff;
        MR::vecKillElement(delta, mGravity, &delta);
        if (!MR::isPlayerSwimming() && delta.length() > 50.0f && diff.dot(mUp) < -5.0f) {
            return false;
        }
        mHostActor = MR::getSensorHost(pSender);
        MR::invalidateClipping(this);
        mPlayerInStartMtx.set(MR::getPlayerBaseMtx());
        mPlayerMtx.set(mPlayerInStartMtx);
        Vec3f camPos = MR::getCamPos();
        camPos.sub(mPosition);
        MR::normalize(&camPos);
        MR::makeMtxUpFrontPos(&mPlayerInEndMtx, mUp, camPos, mPosition);
        MR::invalidateHitSensors(this);
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvReady));
        return true;
    }
    if (MR::isMsgUpdateBaseMtx(msg)) {
        MR::setBaseTRMtx(mHostActor, mPlayerMtx);
        return true;
    }
    // While the runaway rabbit waits, using the pipe does not set switch B.
    if (msg == ACTMES_HEAVENSDOOR_RUNAWAY_RABBIT_WAIT) {
        mIsRabbitWait = true;
    }
    if (msg == ACTMES_HEAVENSDOOR_RUNAWAY_RABBIT_START) {
        mIsRabbitWait = false;
        return true;
    }
    if (MR::isMsgRushCancel(msg)) {
        mHostActor = nullptr;

        if (mTargetPipe != nullptr) {
            mTargetPipe->tryHideDown();
        }

        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWait));
        return true;
    }
    return false;
}

// Raises the pipe top to rate of its length above the base.
void EarthenPipe::calcTrans(f32 rate) {
    mPosition.set(mUp);
    mPosition.scale(rate * mPipeLength);
    mPosition.add(mBasePos);

    (mTopJointMtx)[0][3] = mPosition.x;
    (mTopJointMtx)[1][3] = mPosition.y;
    (mTopJointMtx)[2][3] = mPosition.z;
    calcAndSetBaseMtx();
    if (MR::isValidCollisionParts(this)) {
        MR::setCollisionMtx(this);
    }
}

void EarthenPipe::processBgmPlayerIn() {
    s32 idx = mTargetPipe->mMusicChangeIdx;
    if (idx >= 0) {
        u32 bgmId = AudStageBgmTable::getBgmId(MR::getCurrentStageName(), idx);
        if (bgmId != -1) {
            s32 cur = AudWrap::getBgmMgr()->mCurrentBGM[AudBgmMgr::BgmType_Stage];
            if (static_cast< u32 >(cur) != bgmId) {
                MR::stopStageBGM(60);
                return;
            }

            s32 state = mTargetPipe->mMusicState;
            if (state >= 0) {
                s32 bgmState = AudStageBgmTable::getBgmState(MR::getCurrentStageName(), state);
                if (bgmState >= 0) {
                    AudBgm* bgm = AudWrap::getStageBgm();
                    if (bgm != nullptr) {
                        bgm->changeTrackMuteState(bgmState, 240);
                    }
                }
            }
        }
    }
}

void EarthenPipe::processBgmPlayerOut() {
    s32 idx = mTargetPipe->mMusicChangeIdx;
    if (idx >= 0) {
        u32 bgmId = AudStageBgmTable::getBgmId(MR::getCurrentStageName(), idx);
        if (bgmId != -1) {
            s32 cur = AudWrap::getBgmMgr()->mCurrentBGM[AudBgmMgr::BgmType_Stage];
            if (static_cast< u32 >(cur) == bgmId && MR::isPlayingStageBgm()) {
                return;
            }

            AudWrap::startStageBgm(bgmId, false);
            s32 state = mTargetPipe->mMusicState;
            if (state >= 0) {
                s32 bgmState = AudStageBgmTable::getBgmState(MR::getCurrentStageName(), state);
                if (bgmState >= 0) {
                    AudBgm* bgm = AudWrap::getStageBgm();
                    if (bgm != nullptr) {
                        bgm->changeTrackMuteState(bgmState, 0);
                    }
                }
            }
        }
    }
}

EarthenPipeMediator::EarthenPipeMediator() : NameObj("土管仲介者") {
    lumacore::setClass(this, lumacore::kClassId< EarthenPipeMediator >);
    mNumEntries = 0;
    mPipeEntries = nullptr;
    mPipeEntryRoom = 32;
    mPipeEntries = new Entry[mPipeEntryRoom];
}

void EarthenPipeMediator::entry(EarthenPipe* pPipe, const TableIter& rIter) {
    s32 pipeID = -1;
    MR::getPlacementArg3NoInit(rIter, &pipeID);

    for (s32 i = 0; i < mNumEntries; i++) {
        EarthenPipeMediator::Entry* entry = &mPipeEntries[i];

        if (pipeID == entry->mPipeID) {
            entry->mSecond = pPipe;
            entry->mFirst->mTargetPipe = pPipe;
            entry->mSecond->mTargetPipe = entry->mFirst;
            return;
        }
    }

    if (mNumEntries == mPipeEntryRoom) {
        mPipeEntries = MR::growArray(mPipeEntries, mPipeEntryRoom);
        mPipeEntryRoom *= 2;
    }

    EarthenPipeMediator::Entry* entry = &mPipeEntries[mNumEntries];
    entry->mFirst = pPipe;
    entry->mSecond = nullptr;
    entry->mPipeID = pipeID;
    mNumEntries++;
}
