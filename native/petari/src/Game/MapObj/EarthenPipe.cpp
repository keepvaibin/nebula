#include "Game/MapObj/EarthenPipe.hpp"
#include "Game/AudioLib/AudBgmMgr.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/GameAudio/AudStageBgmTable.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/LiveActor/PartsModel.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util.hpp"

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

EarthenPipe::EarthenPipe(const char* pName) : LiveActor(pName) {
    mBasePos.x = 0.0f;
    mBasePos.y = 0.0f;
    mBasePos.z = 0.0f;
    mUp.x = 0.0f;
    mUp.y = 1.0f;
    mUp.z = 0.0f;
    mPipeLength = 100.0f;
    mIsIgnoreGravity = false;
    mPipeMode = 0;
    mTargetPipe = 0;
    mTopJointMtx = nullptr;
    mBottomJointMtx = nullptr;
    mHostActor = nullptr;
    mHorizExitForce = 5.0f;
    mVertExitForce = 20.0f;
    mMusicChangeIdx = -1;
    mMusicState = -1;
    mIsRabbitWait = 0;
    mPipeStreamModel = nullptr;
    mIsWaterFlowing = 0;
    mCameraInfo = nullptr;
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
    s32 arg7 = 0;
    MR::getPlacementArg7NoInit(rIter, &arg7);
    bool isWaterPipe = false;

    if (MR::isObjectName(rIter, "EarthenPipeInWater")) {
        isWaterPipe = true;
    }

    mBasePos.set(mPosition);
    MR::calcGravity(this);
    initModelManagerWithAnm("EarthenPipe", nullptr, false);
    mTopJointMtx = MR::getJointMtx(this, "Top");
    mBottomJointMtx = MR::getJointMtx(this, "Bottom");
    MR::startBrk(this, "EarthenPipe");
    MR::setBrkFrameAndStop(this, arg7);
    Pos3f v25;
    v25.identity();
    MR::makeMtxTR(v25, this);
    Vec3f v23;
    v25.getZDir(v23);

    if (mIsIgnoreGravity) {
        v25.getYDir(mUp);
    } else {
        mUp.set(mGravity);
        f32 _x = mUp.x;
        f32 mult = -1.0f;
        f32 x = _x * mult;
        f32 y = mUp.y * mult;
        f32 z = mUp.z * mult;

        mUp.x = x;
        mUp.y = y;
        mUp.z = z;
    }

    Pos3f v24;
    v24.identity();
    MR::makeMtxUpFrontPos(&v24, mUp, v23, mBasePos);
    NebulaMtxCopy(v24, mTopJointMtx);
    mLavaEffectMtx.setInline(v24);
    Vec3f v22(mBasePos);
    Vec3f v21(mUp);
    v21.scale(50.0f);
    v22.sub(v21);
    v24.setTrans(v22);
    NebulaMtxCopy(v24, mBottomJointMtx);
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

    if (mPipeMode == 3) {
        MR::initCollisionParts(this, "EarthenPipeS", getSensor("binder"), mTopJointMtx);
    } else {
        MR::initCollisionParts(this, "EarthenPipe", getSensor("binder"), mTopJointMtx);
    }

    f32 v14 = (100.0f * mScale.y);
    mScale.y = 1.0f;
    mPipeLength = v14;
    calcTrans(1.0f);
    initEffectKeeper(0, nullptr);
    MR::setEffectHostMtx(this, "LavaAppear", mLavaEffectMtx.mMtx);
    MR::setEffectHostMtx(this, "LavaVanish", mLavaEffectMtx.mMtx);
    initSound(4, false);
    MR::initMultiActorCamera(this, rIter, &mCameraInfo, "出現");
    initNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWait));

    if ((mPipeMode - 1) <= 1u) {
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
        mIsWaterFlowing = 1;
    } else {
        mIsWaterFlowing = 0;
    }

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
    if (mPipeMode != 1 && mPipeMode != 2) {
        return false;
    }

    setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToShowUp));
    return true;
}

bool EarthenPipe::tryHideDown() {
    MR::validateClipping(this);
    MR::endMultiActorCamera(this, mCameraInfo, "出現", true);

    if (!mPipeMode || (mPipeMode - 3) <= 1u) {
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvInvalid));
        return false;
    } else {
        if (mPipeMode == 2) {
            mPipeMode = 0;
            setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvInvalid));
            return false;
        } else {
            setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToHideDown));
            return true;
        }
    }
}

bool EarthenPipe::isNerveShowUp() const {
    bool ret;

    if (isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToShowUp)) || isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvShowUp))) {
        ret = true;
    } else {
        ret = false;
    }

    return ret;
}

void EarthenPipe::exeWait() {
    if (MR::isValidSwitchA(this)) {
        if (MR::isOnSwitchA(this)) {
            if (!MR::isValidHitSensor(this, "binder")) {
                MR::validateHitSensors(this);
                if (mPipeStreamModel != nullptr) {
                    mPipeStreamModel->appear();
                    MR::startAction(mPipeStreamModel, "Appear");
                    mIsWaterFlowing = 1;
                }
            }
        } else if (MR::isValidHitSensor(this, "binder")) {
            MR::invalidateHitSensors(this);
            if (mPipeStreamModel != nullptr) {
                mPipeStreamModel->kill();
            }

            mIsWaterFlowing = 0;
        }
    }
}

void EarthenPipe::exeReady() {
    if (MR::isFirstStep(this)) {
        MR::startBckPlayer("EarthenPipeReady");
    }

    MR::blendMtx(mPlayerInStartMtx.mMtx, mPlayerInEndMtx.mMtx, MR::calcNerveRate(this, MR::getBckFrameMaxPlayer()), mPlayerMtx.mMtx);
    if (MR::isBckStoppedPlayer()) {
        if (!MR::isPlayerDead()) {
            setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvPlayerIn));
        }
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
        Vec3f v6((baseMtx)[0][2], (baseMtx)[1][2], (baseMtx)[2][2]);
        Vec3f v5((baseMtx)[0][1], (baseMtx)[1][1], (baseMtx)[2][1]);
        v6 *= (mTargetPipe->mHorizExitForce);
        v5 *= (mTargetPipe->mVertExitForce);
        Vec3f v4(v5);

        if (!MR::isInWater(mTargetPipe->mPosition)) {
            v4.add(v6);
        }

        MR::startSound(mHostActor, "SE_PM_JUMP_M");
        MR::startSound(mHostActor, "SE_PV_JUMP_JOY");
        MR::startBckPlayer("EarthenPipeJump", 0);
        MR::endBindAndPlayerForceWeakGravityJumpInputOff(this, v4);
        mHostActor = nullptr;
        mTargetPipe->tryHideDown();

        if (!mIsRabbitWait) {
            if (MR::isValidSwitchB(this)) {
                MR::onSwitchB(this);
            }
        }

        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvInvalid));
    }
}

inline bool isNear(const LiveActor* actor) {
    return !(MR::isNearPlayer(actor, 100.0f));
}

void EarthenPipe::exeInvalid() {
    if ((MR::isGreaterStep(this, 30) && MR::isOnGroundPlayer()) || (mPipeMode == 4 && isNear(this)) || (MR::isPlayerSwimming() && isNear(this))) {
        MR::validateClipping(this);
        MR::validateHitSensors(this);
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWait));
    }
}

void EarthenPipe::exeHide() {
}

void EarthenPipe::exeShow() {
}

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

    f32 easeIn = MR::getEaseInValue(getNerveStep() / 30.0f, 0.0f, 1.0f, 1.0f);
    calcTrans(easeIn);

    if (MR::isLessStep(this, 30)) {
        MR::startLevelSound(this, "SE_OJ_LV_PIPE_APPEAR_MAGMA");
    }

    if (MR::isStep(this, 30)) {
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

bool EarthenPipe::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgAutoRushBegin(msg)) {
        if (!(!isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvInvalid)) && !isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvHide)) &&
              !isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToShowUp)) && !isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvShowUp)) &&
              !isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWaitToHideDown)) && !isNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvHideDown)) &&
              !MR::isPlayerDead())) {
            return false;
        }

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
    if (msg == ACTMES_HEAVENSDOOR_RUNAWAY_RABBIT_WAIT) {  // Do we have an inline for this?
        mIsRabbitWait = true;
    }
    if (msg == ACTMES_HEAVENSDOOR_RUNAWAY_RABBIT_START) {
        mIsRabbitWait = false;
        return true;
    }
    if (MR::isMsgRushCancel(msg)) {
        mHostActor = nullptr;
        mTargetPipe->tryHideDown();
        setNerve(GET_NERVE(EarthenPipe, EarthenPipeNrvWait));
        return true;
    }
    return false;
}

void EarthenPipe::calcTrans(f32 a1) {
    mPosition.set(mUp);
    mPosition.scale(a1 * mPipeLength);
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
            if (cur != bgmId) {
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
            if (cur == bgmId && MR::isPlayingStageBgm()) {
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
