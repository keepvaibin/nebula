#include "Game/Player/GhostPlayer.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Camera/CameraTargetArg.hpp"
#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/RaceManager.hpp"
#include <nebula_geometry.hpp>

static_assert(sizeof(GhostFrame) == 32, "GhostFrame is cook/ghost.rs's 32-byte frame");
#include "Game/Player/PlayerModel.hpp"
#include "Game/Player/JetTurtleShadow.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/ActorCameraUtil.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/FixedPosition.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <nebula_format.h>
#include "Game/System/LumaCore.hpp"

namespace {
    struct AnimSoundInfo {
        const char* pAnimName;
        const char* pSoundBM;
        const char* pSoundBV;
    };

    const AnimSoundInfo sAnimSoundTable[] = {{"ジャンプ", "SE_BM_GHOST_MARIO_JUMP_S", "SE_BV_GHOST_MARIO_JUMP_S"},
                                             {"ジャンプB", "SE_BM_GHOST_MARIO_JUMP_M", "SE_BV_GHOST_MARIO_JUMP_M"},
                                             {"ジャンプC", "SE_BM_GHOST_MARIO_JUMP_L", "SE_BV_GHOST_MARIO_JUMP_L"},
                                             {"壁ジャンプ", "SE_BM_GHOST_MARIO_JUMP_S", "SE_BV_GHOST_MARIO_JUMP_S"},
                                             {"埋まり脱出ジャンプ", "SE_BM_GHOST_MARIO_JUMP_S", "SE_BV_GHOST_MARIO_JUMP_S"},
                                             {"幅とび", "SE_BM_GHOST_MARIO_JUMP_L", "SE_BV_GHOST_MARIO_JUMP_L"},
                                             {"しゃがみジャンプ", "SE_BM_GHOST_MARIO_JUMP_L", "SE_BV_GHOST_MARIO_JUMP_L"},
                                             {"崖つかまり開始", nullptr, "SE_BV_GHOST_MARIO_HANG"},
                                             {"崖つかまり終了", nullptr, "SE_BV_GHOST_MARIO_CLIMB"},
                                             {"壁押し", nullptr, "SE_BV_GHOST_MARIO_HANG"},
                                             {"ヒップドロップ開始", "SE_BM_GHOST_MARIO_HIP_DROP_TURN", "SE_BV_GHOST_MARIO_HIPDROP_S"},
                                             {"ヒップドロップ着地", "SE_BM_GHOST_MARIO_HIP_DROP_LAND", "SE_BV_GHOST_MARIO_HIPDROP_E"},
                                             {"埋まり脱出ジャンプ", "SE_BM_GHOST_MARIO_JUMP_S", "SE_BV_GHOST_MARIO_JUMP_S"},
                                             {"水泳スピン", "SE_BM_GHOST_MARIO_SPIN", "SE_BV_GHOST_MARIO_SPIN"},
                                             {"水泳ジェット開始", nullptr, "SE_BV_GHOST_MARIO_SWM_ACCEL"},
                                             {"カメ持ちリング準備", nullptr, "SE_BV_GHOST_MARIO_TAKE"},
                                             {"カメ持ちリング", nullptr, "SE_BV_GHOST_MARIO_SWM_ACCEL"},
                                             {"投げ", nullptr, "SE_BV_GHOST_MARIO_THROW"},
                                             {"空中ひねり", "SE_BM_GHOST_MARIO_SPIN", "SE_BV_GHOST_MARIO_SPIN"},
                                             {"地上ひねり", "SE_BM_GHOST_MARIO_SPIN", "SE_BV_GHOST_MARIO_SPIN"},
                                             {"空パンチ", "SE_BM_GHOST_MARIO_SPIN", "SE_BV_GHOST_MARIO_SPIN"},
                                             {"アイスひねり", "SE_BM_GHOST_MARIO_SPIN", "SE_BV_GHOST_MARIO_SPIN"},
                                             {"アイスひねり移動", "SE_BM_GHOST_MARIO_SPIN", "SE_BV_GHOST_MARIO_SPIN"},
                                             {"スケートアクセルジャンプ", "SE_BM_GHOST_MARIO_JUMP_L", "SE_BV_GHOST_MARIO_JUMP_L"},
                                             {"スケートジャンプ2", "SE_BM_GHOST_MARIO_JUMP_L", "SE_BV_GHOST_MARIO_JUMP_L"},
                                             {"氷上力行右", nullptr, "SE_BV_GHOST_MARIO_JUMP_S"},
                                             {"氷上力行左", nullptr, "SE_BV_GHOST_MARIO_JUMP_S"},
                                             {"ショートジャンプ", "SE_BM_GHOST_MARIO_JUMP_S", "SE_BV_GHOST_MARIO_JUMP_S"}};

    // Animations whose frame control stays as the file gives it; a frame naming its animation tests the first four.
    const char* const cGhostSpecialAnims[] = {"基本", "壁押し", "しゃがみ歩き", "がんばり走り", "スケートアクセルジャンプ", "スケートジャンプ2", "スケートジャンプ3"};
    const u32 cGhostNamedSpecialAnims = 4;
    // The one that takes the recorded track weights.
    const char* const cGhostWeightedAnim = cGhostSpecialAnims[0];

    static const AnimSoundInfo* getAnimSoundInfo(const char* pAnimName) {
        for (u32 i = 0; i < ARRAY_SIZE(sAnimSoundTable); i++) {
            if (MR::isEqualString(pAnimName, sAnimSoundTable[i].pAnimName)) {
                return &sAnimSoundTable[i];
            }
        }
        return nullptr;
    }

    void playSound(LiveActor* pActor, const char* pAnimName) {
        const AnimSoundInfo* matchedEntry = getAnimSoundInfo(pAnimName);
        if (matchedEntry == nullptr) {
            return;
        }

        if (matchedEntry->pSoundBM != nullptr) {
            MR::startSound(pActor, matchedEntry->pSoundBM);
        }
        if (matchedEntry->pSoundBV != nullptr) {
            MR::startSound(pActor, matchedEntry->pSoundBV);
        }
    }
};  // namespace

namespace NrvGhostPlayer {
    NEW_NERVE(HostTypeNrvWait, GhostPlayer, Wait);
    NEW_NERVE(HostTypeNrvWinDemo, GhostPlayer, WinDemo);
    NEW_NERVE(HostTypeNrvLostDemo, GhostPlayer, LostDemo);
    NEW_NERVE(HostTypeNrvPreStartDemo0, GhostPlayer, PreStartDemo0);
    NEW_NERVE(HostTypeNrvPreStartDemo1, GhostPlayer, PreStartDemo1);
    NEW_NERVE(HostTypeNrvPreStartDemo2, GhostPlayer, PreStartDemo2);
    NEW_NERVE(HostTypeNrvStartDemo, GhostPlayer, Wait);
    NEW_NERVE(HostTypeNrvRun, GhostPlayer, Wait);
};  // namespace NrvGhostPlayer

GhostPlayer::GhostPlayer(const char* pName) : LiveActor(pName), mCameraInfo(), mXanimePlayer() {
    lumacore::setClass(this, lumacore::kClassId< GhostPlayer >);
    mTargetRotation.zero();
    mKilledByStar = false;
    mPowerStarTarget = nullptr;
    mHasJetTurtle = false;
}

void GhostPlayer::init(const TableIter& rIter) {
    MR::FormattedName gstPath;
    MR::FormattedName arcPath;

    mRaceManagerLayout = new RaceManagerLayout("レース管理用レイアウト");
    mRaceManagerLayout->init(rIter);

    mFrame = nullptr;
    mFrameEnd = nullptr;
    mGhostNames = nullptr;
    if (MR::isPlayerLuigi()) {
        const char* currentStageName = MR::getCurrentStageName();
        gstPath.format("%sLuigi.gst", currentStageName);
    } else {
        const char* currentStageName = MR::getCurrentStageName();
        gstPath.format("%s.gst", currentStageName);
    }
    arcPath.format("GhostData%s", MR::getCurrentStageName());

    const u8* pFile = static_cast< const u8* >(MR::loadResourceFromArc(arcPath.c_str(), gstPath.c_str()));

    if (MR::isFileExist(gstPath.c_str(), false)) {
        pFile = static_cast< const u8* >(GameAssets::load(gstPath.c_str(), nullptr, 0, nullptr, nullptr));
    }

    if (pFile == nullptr) {
        makeActorDead();
        return;
    }

    const GhostFileHeader* pHeader = reinterpret_cast< const GhostFileHeader* >(pFile);
    if (pHeader->mMagic[0] != 'N' || pHeader->mMagic[1] != 'G' || pHeader->mMagic[2] != 'S' || pHeader->mMagic[3] != 'T') {
        NEBULA_PANIC("GhostPlayer: %s: a recording the importer did not cook", gstPath.c_str());
    }
    mFrame = reinterpret_cast< const GhostFrame* >(pHeader + 1);
    mFrameEnd = mFrame + pHeader->mFrameNum;
    mGhostNames = reinterpret_cast< const char* >(pFile + pHeader->mNameOffset);

    if (MR::isPlayerLuigi()) {
        initModelManagerWithAnm("GhostLuigi", nullptr, false);
    } else {
        initModelManagerWithAnm("GhostMario", nullptr, false);
    }

    initEffectKeeper(5, "GhostMario");
    MR::connectToScene(this, MR::MovementType_Player, MR::CalcAnimType_Player, MR::DrawBufferType_None, MR::DrawType_Player);
    MR::initDefaultPos(this, rIter);
    mStartPos = mPosition;
    mVelocity.zero();
    initAnimation();
    initHitSensor(1);
    MR::addHitSensorRide(this, "body", 4, 40.0f, Vec3f(0.0f, 80.0f, 0.0f));
    MR::initShadowVolumeSphere(this, 50.0f);
    MR::validateShadow(this, nullptr);
    MR::calcGravity(this);
    MR::onCalcShadow(this, nullptr);
    MR::onCalcShadowDropGravity(this, nullptr);
    initSound(6, false);
    MR::invalidateClipping(this);
    mCameraInfo = nullptr;
    if (MR::isValidInfo(rIter)) {
        MR::initMultiActorCamera(this, rIter, &mCameraInfo, "レース開始1");
        MR::initMultiActorCamera(this, rIter, &mCameraInfo, "レース開始2");
        MR::initMultiActorCamera(this, rIter, &mCameraInfo, "レース開始3");
        MR::initMultiActorCamera(this, rIter, &mCameraInfo, "レース終了");
    }
    mCameraTargetMtx = new CameraTargetMtx("カメラターゲットダミー");
    initNerve(GET_NERVE(GhostPlayer, HostTypeNrvWait));
    MR::needStageSwitchReadAppear(this, rIter);
    MR::syncStageSwitchAppear(this);
    makeActorDead();
    mWaitingToStart = true;
    mSkippingLeadIn = true;
    MR::getRotatedAxisZ(&mInitialDirection, mRotation);
    NebulaMtxCopy(getBaseMtx(), mTargetRotationMtx);
    mJetTurtleShadow = new JetTurtleShadow("カメシャドウモデル");
    mJetTurtleShadow->initWithoutIter();
    mHandRPos = new FixedPosition(this, "HandR", Vec3f(15.59f, 42.5f, 42.93f), Vec3f(-17.05f, -0.7f, 113.55f));
    MR::declareStarPiece(this, 50);
    MR::declareCoin(this, 50);
    mAppearStarPieceCooldown = 0;
    mPlayerTrampleCooldown = 0;
}

void GhostPlayer::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(GhostPlayer, HostTypeNrvPreStartDemo0));
    mIsHidden = true;
    MR::invalidateShadow(this, nullptr);
    if (MR::isPlayerLuigi()) {
        MR::startBtk(this, "GhostLuigi");
        MR::startBrk(this, "GhostLuigi");
    } else {
        MR::startBtk(this, "GhostMario");
        MR::startBrk(this, "GhostMario");
    }
    MR::emitEffect(this, "Shadow");
}

void GhostPlayer::control() {
    if (mWaitingToStart) {
        return;
    }
    if (MR::isDead(this)) {
        return;
    }

    MR::startLevelSound(this, "SE_BM_LV_GHOST_MARIO_AMBIENT");

    if (isNerve(GET_NERVE(GhostPlayer, HostTypeNrvLostDemo))) {
        return;
    }
    if (isNerve(GET_NERVE(GhostPlayer, HostTypeNrvWinDemo))) {
        return;
    }

    if (MR::isEqualString("powerstarget", MR::getPlayerCurrentBckName()) && isNerve(GET_NERVE(GhostPlayer, HostTypeNrvRun))) {
        setNerve(GET_NERVE(GhostPlayer, HostTypeNrvLostDemo));
        mKilledByStar = true;
    } else if (mFrame != nullptr) {
        u32 packetDelayTimer = 0;
        while (true) {
            mFrame += receiveGhostFrame();
            if (mSkippingLeadIn) {
                if (mXanimePlayer->mWeights[3] > 0.9f) {
                    continue;
                }
                mSkippingLeadIn = false;
                packetDelayTimer = 10;
                continue;
            }

            if (packetDelayTimer == 0) {
                break;
            }
            packetDelayTimer--;
        }

        if (mAppearStarPieceCooldown != 0) {
            mAppearStarPieceCooldown--;
        }
        if (mPlayerTrampleCooldown != 0) {
            mPlayerTrampleCooldown--;
        }
    }
}

void GhostPlayer::exeWait() {
    if (mRaceManagerLayout->isAllAnimStopped()) {
        mRaceManagerLayout->kill();
    }
}

void GhostPlayer::warpPosition(const char* pName) {
    MR::findNamePos(pName, getBaseMtx());
    NebulaMtxCopy(getBaseMtx(), mTargetRotationMtx);
    MR::extractMtxTrans(getBaseMtx(), &mPosition);
}

void GhostPlayer::exePreStartDemo0() {
    if (MR::isFirstStep(this)) {
        MR::startSound(this, "SE_BM_GHOST_MARIO_APPEAR");
        MR::startSound(this, "SE_BV_GHOST_MARIO_APPEAR");
        MR::offPlayerControl();
        Pos3f* cameraTargetMatrix = &mCameraTargetMtx->mMatrix;
        cameraTargetMatrix->set(getBaseMtx());

        MR::startMultiActorCameraTargetOther(this, mCameraInfo, "レース開始1", CameraTargetArg(mCameraTargetMtx));
        warpPosition("ゴーストデモゴースト位置");
    }

    if (getNerveStep() == 1) {
        Mtx playerPosMtx;
        Vec3f playerPosVec;

        MR::findNamePos("ゴーストデモマリオ位置", playerPosMtx);
        MR::extractMtxTrans(playerPosMtx, &playerPosVec);
        MR::setPlayerPosAndWait(playerPosVec);
        MR::setPlayerBaseMtx(playerPosMtx);
        MR::startBckPlayerJ("レース見る");
        MR::resetPlayerEffect();
        MR::tryStartDemo(this, "レース準備");
        MR::requestMovementOn(this);
        MR::requestMovementOnPlayer();
    }
    if (getNerveStep() == 60) {
        mIsHidden = false;
        setAnimation("ゴースト出現");
        MR::validateShadow(this, nullptr);
    }
    if (getNerveStep() == 107) {
        MR::startSound(this, "SE_BV_GHOST_MARIO_LAND");
    }
    if (getNerveStep() == 111) {
        MR::startSound(this, "SE_BM_GHOST_MARIO_LAND");
    }
    if (getNerveStep() == 146) {
        MR::startSound(this, "SE_BV_GHOST_MARIO_PROVOKE");
    }
    if (getNerveStep() == 240) {
        MR::endMultiActorCamera(this, mCameraInfo, "レース開始1", false);
        Pos3f* cameraTargetMatrix = &mCameraTargetMtx->mMatrix;
        cameraTargetMatrix->set(getBaseMtx());
        MR::startMultiActorCameraTargetOther(this, mCameraInfo, "レース開始2", CameraTargetArg(mCameraTargetMtx));
        setNerve(GET_NERVE(GhostPlayer, HostTypeNrvPreStartDemo1));
        setAnimation("レース見る");
    } else if (isRequestSkipDemo()) {
        MR::endMultiActorCamera(this, mCameraInfo, "レース開始1", false);
        MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "レース開始3");
        setNerve(GET_NERVE(GhostPlayer, HostTypeNrvPreStartDemo2));
        if (getNerveStep() < 60) {
            mIsHidden = false;
            setAnimation("ゴースト出現");
            MR::validateShadow(this, nullptr);
        }
    }
}

bool GhostPlayer::isRequestSkipDemo() const {
    if (MR::hasRetryGalaxySequence() && MR::isPressedJump(PLAYER_1)) {
        return true;
    }
    return false;
}

void GhostPlayer::exePreStartDemo1() {
    if (getNerveStep() == 240) {
        MR::endMultiActorCamera(this, mCameraInfo, "レース開始2", false);
        MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "レース開始3");
        setNerve(GET_NERVE(GhostPlayer, HostTypeNrvPreStartDemo2));
    } else if (isRequestSkipDemo()) {
        MR::endMultiActorCamera(this, mCameraInfo, "レース開始2", false);
        MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "レース開始3");
        setNerve(GET_NERVE(GhostPlayer, HostTypeNrvPreStartDemo2));
    }
}

void GhostPlayer::exePreStartDemo2() {
    if (MR::isFirstStep(this)) {
        mRaceManagerLayout->setTime(0);
        mRaceManagerLayout->appear();
        mRaceManagerLayout->playCountAndGo();
        mRaceManagerLayout->hideRecordPane();
        mRaceManagerLayout->hideBestRecordPane();
        setAnimation("ゴーストレース開始");
        MR::startBckPlayerJ("レース開始");

        Mtx raceStartMarioPos;
        MR::findNamePos("レース開始時マリオ位置", raceStartMarioPos);
        MR::setPlayerBaseMtx(raceStartMarioPos);

        mPosition = mStartPos;
        MR::endDemo(this, "レース準備");
    }

    if (MR::getPlayerPressedCrouch()) {
        MR::startBckPlayerJ("レースクラウチング開始");
        MR::startSoundPlayer("SE_PV_SQUAT");
    } else if (MR::isReleasedCrouch(PLAYER_1)) {
        MR::startBckPlayerJ("レース開始");
    }

    if (getNerveStep() % 60 == 0) {
        MR::startSystemSE("SE_SY_RACE_COUNT_DOWN");
    }

    Vec3f normalizedFrontVec = mInitialDirection;
    Vec3f upVec;
    Vec3f frontVec;

    bool isNormalized = MR::normalizeOrZero(&normalizedFrontVec);
    MR::extractMtxYDir(getBaseMtx(), &upVec);
    if (!isNormalized) {
        MR::calcFrontVec(&frontVec, this);
        if (MR::vecBlendSphere(frontVec, normalizedFrontVec, &frontVec, 0.1f) == false) {
            frontVec = normalizedFrontVec;
        }
        MR::makeMtxUpFront(getBaseMtx(), upVec, frontVec);
        NebulaMtxCopy(getBaseMtx(), mTargetRotationMtx);
        MR::extractMtxTrans(getBaseMtx(), &mPosition);
    }
    if (!mRaceManagerLayout->isPlayCountAnim()) {
        mWaitingToStart = false;
        MR::startSound(this, "SE_BV_GHOST_MARIO_RUN_START");
        setNerve(GET_NERVE(GhostPlayer, HostTypeNrvRun));
        MR::onPlayerControl(true);
        MR::noticePlayerDashChance();
        MR::startBckPlayerJ("基本");
        MR::startSystemSE("SE_SY_RACE_START");
        MR::endMultiActorCamera(this, mCameraInfo, "レース開始3", false);
    }
}

void GhostPlayer::exeWinDemo() {
    if (MR::isFirstStep(this)) {
        MR::offPlayerControl();
        MR::tryPlayerKillTakingActor();
        MR::readyPlayerDemo();

        Vec3f playerPosVec;
        Mtx playerPosMtx;
        MR::findNamePos("負け時マリオ位置", playerPosMtx);
        MR::extractMtxTrans(playerPosMtx, &playerPosVec);
        MR::setPlayerPosAndWait(playerPosVec);
        MR::setPlayerBaseMtx(playerPosMtx);

        MR::startBckPlayerJ("レース見る");
        MR::startMultiActorCameraTargetSelf(this, mCameraInfo, "レース終了");
        mIsDemoCameraActive = true;

        HitSensor* sensorBody = getSensor("body");
        for (int i = 0; i < sensorBody->mSensorCount; i++) {
            HitSensor* sensor = sensorBody->mSensors[i];
            if (sensor->mType == ATYPE_POWER_STAR_BIND) {
                mPowerStarTarget = sensor->mHost;
                mPosition = sensor->mHost->mPosition;
            }
        }

        MR::startSubBGM("BGM_RACE_LOSE", false);
        mRaceManagerLayout->appear();
        mRaceManagerLayout->playLose();
        setAnimation("ゴースト勝利");
        MR::startSound(this, "SE_BV_GHOST_MARIO_WIN");
        MR::tryStartDemo(this, "レース終了");
        MR::requestMovementOn(this);
        MR::requestMovementOnPlayer();
        if (mPowerStarTarget != nullptr) {
            MR::requestMovementOn(mPowerStarTarget);
        }
    }

    if (mPowerStarTarget != nullptr) {
        NebulaMtxCopy(mPowerStarTarget->getBaseMtx(), getBaseMtx());
        NebulaMtxCopy(getBaseMtx(), mTargetRotationMtx);
        MR::extractMtxTrans(getBaseMtx(), &mPosition);
    }

    if (MR::isStep(this, 180)) {
        if (mIsDemoCameraActive) {
            MR::endMultiActorCamera(this, mCameraInfo, "レース終了", true);
            mIsDemoCameraActive = false;
        }
        MR::endDemo(this, "レース終了");
        MR::forceKillPlayerByGhostRace();
    }
}

void GhostPlayer::exeLostDemo() {
    if (MR::isFirstStep(this)) {
        warpPosition("負け時マリオ位置");
        mXanimePlayer->changeAnimation("DieSwimEvent");
        mVelocity.zero();
        setAnimation("レース負け");
    }
}

void GhostPlayer::calcAndSetBaseMtx() {
    if (MR::isDead(this)) {
        return;
    }

    MtxPtr baseMtx = MR::getModel(this)->getBaseTRMtx();
    MR::blendMtxRotate(getBaseMtx(), mTargetRotationMtx, 0.2f, baseMtx);
    MR::setMtxTrans(baseMtx, mPosition.x, mPosition.y, mPosition.z);
    MR::getModel(this)->setBaseScale(mScale);

    if (mHasJetTurtle) {
        mHandRPos->calc();
        mJetTurtleShadow->calcType0(mHandRPos->mMtx);
    }
}

void GhostPlayer::initAnimation() {
    XanimeResourceTable* pResource = MR::getPlayerXanimeResource();

    mAnimTrackWeights[0] = 0.0f;
    mAnimTrackWeights[1] = 0.0f;
    mAnimTrackWeights[2] = 0.0f;
    mAnimTrackWeights[3] = 1.0f;

    mXanimePlayer = new XanimePlayer(MR::getModel(this), pResource);
    mXanimePlayer->duplicateSimpleGroup();
    mModelManager->mXanimePlayer = mXanimePlayer;
    mXanimePlayer->setDefaultAnimation("基本");
    mXanimePlayer->getCore()->enableJointTransform(MR::getModelData(this));
    mXanimePlayerUpper = new XanimePlayer(MR::getModel(this), pResource, mXanimePlayer);
    mXanimePlayerUpper->changeAnimation("基本");
}

void GhostPlayer::setAnimation(const char* pName) {
    mXanimePlayer->changeAnimation(pName);
    if (mXanimePlayer->mFrameCtrl->mAttribute == 0) {
        mXanimePlayer->mFrameCtrl->mAttribute = 1;
    }
}

void GhostPlayer::setAnimationWeight(const f32* pWeights) {
    mXanimePlayer->changeTrackWeight(0, pWeights[0]);
    mXanimePlayer->changeTrackWeight(1, pWeights[1]);
    mXanimePlayer->changeTrackWeight(2, pWeights[2]);
    mXanimePlayer->changeTrackWeight(3, pWeights[3]);
}

void GhostPlayer::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (pReceiver->isType(ATYPE_POWER_STAR_BIND)) {
        HitSensor* currentRushSensor = MR::getCurrentRushSensor();
        if ((currentRushSensor == nullptr || !currentRushSensor->isType(ATYPE_POWER_STAR_BIND)) && (!MR::isPlayerConfrontDeath() && !mKilledByStar)) {
            if (!MR::isEqualString("powerstarget", MR::getPlayerCurrentBckName())) {
                setNerve(GET_NERVE(GhostPlayer, HostTypeNrvWinDemo));
                mKilledByStar = true;
                MR::preventPlayerRush();
            }
        }
    } else {
        if (isNerve(GET_NERVE(GhostPlayer, HostTypeNrvRun)) && MR::sendMsgPush(pReceiver, pSender) && mAppearStarPieceCooldown == 0) {
            if (MR::appearStarPiece(this, mPosition, 1, 10.0f, 40.0f, false)) {
                if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
                    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_W");
                } else {
                    MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
                }
            }
            mAppearStarPieceCooldown = 5;
        }
        if (LiveActor::isNerve(GET_NERVE(GhostPlayer, HostTypeNrvWait))) {
            return;
        }
    }
}

bool GhostPlayer::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (MR::isMsgJetTurtleAttack(msg)) {
        return true;
    }
    if (MR::isMsgPlayerTrample(msg) && mPlayerTrampleCooldown == 0) {
        Vec3f frontVec;
        Vec3f upVec;
        MR::calcUpVec(&upVec, this);
        MR::calcFrontVec(&frontVec, this);

        if (MR::appearStarPiece(this, mPosition, 5, 10.0f, 40.0f, false)) {
            if (MR::isInWater(this, Vec3f(0.0f, 0.0f, 0.0f))) {
                MR::startSound(this, "SE_OJ_STAR_PIECE_BURST_W");
            } else {
                MR::startSound(this, "SE_OJ_STAR_PIECE_BURST");
            }
        }

        mPlayerTrampleCooldown = 10;
        return true;
    }

    return false;
}

bool GhostPlayer::receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    return true;
}

void GhostPlayer::draw() const {
    if (MR::isDead(this)) {
        return;
    }
    if (mIsHidden) {
        return;
    }

    PlayerModel* model = static_cast< PlayerModel* >(MR::getModel(this));
    model->viewCalc2();
    model->mDrawPasses = 0;
    model->directDraw();
    if (mHasJetTurtle) {
        mJetTurtleShadow->drawType1();
    }
}

u32 GhostPlayer::receiveGhostFrame() {
    if (mFrame + 1 >= mFrameEnd) {
        if (mIsDemoCameraActive && MR::isBckOneTimeAndStopped(this)) {
            MR::endActorCamera(this, mCameraInfo, false);
            mIsDemoCameraActive = false;
        }
        return 0;
    }

    const GhostFrame& rFrame = *mFrame;
    const u16 updateFlags = rFrame.mFlags;

    if ((updateFlags & 0x0001) != 0) {
        Vec3s rawPos;
        rawPos.x = rFrame.mPosition[0];
        rawPos.y = rFrame.mPosition[1];
        rawPos.z = rFrame.mPosition[2];
        MR::convToFloat(rawPos, -2, &mPosition);
    }

    if ((updateFlags & 0x0800) != 0) {
        Vec3Sc rawVel;
        rawVel.x = rFrame.mVelocity[0];
        rawVel.y = rFrame.mVelocity[1];
        rawVel.z = rFrame.mVelocity[2];
        MR::convToFloat(rawVel, 0, &mVelocity);
    }

    if ((updateFlags & 0x0400) != 0) {
        Vec3Sc rawScale;
        rawScale.x = rFrame.mScale[0];
        rawScale.y = rFrame.mScale[1];
        rawScale.z = rFrame.mScale[2];
        MR::convToFloat(rawScale, 3, &mScale);
    }

    if ((updateFlags & 0x0002) != 0) {
        s8 rawRotX = rFrame.mRotation[0];
        f32 rotX;
        MR::convToFloat(rawRotX, 7, &rotX);
        rotX *= 180.0f;
        rotX = MR::clamp(rotX, -180.0f, 180.0f);
        mTargetRotation.x = rotX;
    }

    if ((updateFlags & 0x0004) != 0) {
        s8 rawRotY = rFrame.mRotation[1];
        f32 rotY;
        MR::convToFloat(rawRotY, 7, &rotY);
        rotY *= 180.0f;
        rotY = MR::clamp(rotY, -180.0f, 180.0f);
        mTargetRotation.y = rotY;
    }

    if ((updateFlags & 0x0008) != 0) {
        s8 rawRotZ = rFrame.mRotation[2];
        f32 rotZ;
        MR::convToFloat(rawRotZ, 7, &rotZ);
        rotZ *= 180.0f;
        rotZ = MR::clamp(rotZ, -180.0f, 180.0f);
        mTargetRotation.z = rotZ;
    }

    if ((updateFlags & 0x000E) != 0) {
        MR::makeMtxRotate(mTargetRotationMtx, mTargetRotation);
    }

    if ((updateFlags & 0x0010) != 0) {
        const char* animName = mGhostNames + rFrame.mName;
        setAnimation(animName);
        ::playSound(this, animName);

        if (MR::isEqualString(animName, cGhostWeightedAnim)) {
            setAnimationWeight(mAnimTrackWeights);
        }
        const bool isSpecialAnim = MR::isExistString(animName, cGhostSpecialAnims, cGhostNamedSpecialAnims);

        if (!isSpecialAnim) {
            mXanimePlayer->mFrameCtrl->mAttribute = 1;
        }
    }

    if ((updateFlags & 0x2000) != 0) {
        mXanimePlayer->changeAnimationByHash(rFrame.mHash);

        const char* currentAnimName = mXanimePlayer->getCurrentAnimationName();
        ::playSound(this, currentAnimName);

        if (MR::isEqualString(currentAnimName, cGhostWeightedAnim)) {
            setAnimationWeight(mAnimTrackWeights);
        }
        const bool isSpecialAnim = MR::isExistString(currentAnimName, cGhostSpecialAnims, ARRAY_SIZE(cGhostSpecialAnims));

        if (MR::isEqualSubString(currentAnimName, "水泳ジェット")) {
            mHasJetTurtle = true;
        } else if (MR::isEqualSubString(currentAnimName, "カメ持ち")) {
            mHasJetTurtle = true;
        } else if (MR::isEqualSubString(currentAnimName, "投げ")) {
            mHasJetTurtle = false;
            mXanimePlayerUpper->stopAnimation();
            MR::getModelData(this)->getJointAt(MR::getJointIndex(this, "Spine1"))->setMtxCalc(nullptr);
        }

        if (mHasJetTurtle && (!MR::isEqualString(currentAnimName, cGhostWeightedAnim) || !MR::isEqualString(currentAnimName, "ジャンプ"))) {
            mXanimePlayerUpper->changeAnimation("ひろいウエイト");
            mXanimePlayerUpper->overWriteMtxCalc(MR::getJointIndex(this, "PartsControl"));
        }

        if (!isSpecialAnim) {
            mXanimePlayer->mFrameCtrl->mAttribute = 1;
        }
    }

    if ((updateFlags & 0x0020) != 0) {
        s16 rawAnimFrame = rFrame.mAnimFrame;
        f32 animFrame;
        MR::convToFloat(rawAnimFrame, 5, &animFrame);
        MR::getBckCtrl(this)->setFrame(animFrame);
    }

    for (u32 i = 0; i < ARRAY_SIZE(mAnimTrackWeights); i++) {
        if ((updateFlags & (0x0040 << i)) == 0) {
            continue;
        }

        s8 rawTrackWeight = rFrame.mTrackWeights[i];
        f32 trackWeight;
        if ((u8)rawTrackWeight == 128) {
            rawTrackWeight = 127;
        }
        MR::convToFloat(rawTrackWeight, 7, &trackWeight);
        mXanimePlayer->changeTrackWeight(i, trackWeight);
        mAnimTrackWeights[i] = trackWeight;
    }

    if ((updateFlags & 0x1000) != 0) {
        s8 rawAnimRate = rFrame.mRate;
        f32 animRate;
        MR::convToFloat(rawAnimRate, 3, &animRate);
        MR::setBckRate(this, animRate);
    }

    return 1;
}

f32 getShiftRatio(s8 shiftValue) {
    if (shiftValue > 0) {
        return static_cast< f32 >(256 << shiftValue) / 256.0f;
    }
    return static_cast< f32 >(256 >> -shiftValue) / 256.0f;
}

namespace MR {
    void convToFloat(Vec3s& rInVec, s8 shiftValue, Vec3f* pOutVec) {
        f32 factor = 1.0f / getShiftRatio(shiftValue);
        pOutVec->x = static_cast< f32 >(rInVec.x) * factor;
        pOutVec->y = static_cast< f32 >(rInVec.y) * factor;
        pOutVec->z = static_cast< f32 >(rInVec.z) * factor;
    }

    void convToFloat(Vec3Sc& rInVec, s8 shiftValue, Vec3f* pOutVec) {
        f32 factor = 1.0f / getShiftRatio(shiftValue);
        pOutVec->x = static_cast< f32 >(rInVec.x) * factor;
        pOutVec->y = static_cast< f32 >(rInVec.y) * factor;
        pOutVec->z = static_cast< f32 >(rInVec.z) * factor;
    }

    void convToFloat(s16 inShort, s8 shiftValue, f32* pOutFloat) {
        f32 factor = 1.0f / getShiftRatio(shiftValue);
        *pOutFloat = static_cast< f32 >(inShort) * factor;
    }

    void convToFloat(s8 inS8, s8 shiftValue, f32* pOutFloat) {
        f32 factor = 1.0f / getShiftRatio(shiftValue);
        *pOutFloat = static_cast< f32 >(inS8) * factor;
    }
};  // namespace MR
