#include "Game/Player/MarioAnimator.hpp"
#include "Game/Animation/XanimeCore.hpp"
#include "Game/Animation/XanimePlayer.hpp"
#include "Game/Animation/XanimeResource.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/ModelManager.hpp"
#include "Game/Player/PlayerModel.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimatorData.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/System/ResourceInfo.hpp"
#include <nebula_module.h>
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_targetWeight_f1, "targetWeight.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_targetWeight_f2, "targetWeight.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_setWalkWeight_f1, "setWalkWeight.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 79.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 59.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f3, "update.f3", 59.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f1, "setTilt.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f2, "setTilt.f2", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f3, "setTilt.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f4, "setTilt.f4", 0.95f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f5, "setTilt.f5", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f6, "setTilt.f6", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f7, "setTilt.f7", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f8, "setTilt.f8", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f9, "setTilt.f9", 0.97f);
NEBULA_TUNABLE_NAMED(f32, cNum_setTilt_f10, "setTilt.f10", 0.03f);
NEBULA_TUNABLE_NAMED(f32, cNum_setHoming_f1, "setHoming.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setHoming_f2, "setHoming.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_setHoming_f3, "setHoming.f3", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_setHoming_f4, "setHoming.f4", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_setHoming_f5, "setHoming.f5", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_setHipSlidingTilt_f1, "setHipSlidingTilt.f1", 1.414213f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointRumble_f1, "updateJointRumble.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointRumble_f2, "updateJointRumble.f2", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointRumble_f3, "updateJointRumble.f3", 0.7f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointRumble_f4, "updateJointRumble.f4", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointRumble_f5, "updateJointRumble.f5", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointRumble_f6, "updateJointRumble.f6", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointRumble_f7, "updateJointRumble.f7", 0.707f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateJointRumble_f8, "updateJointRumble.f8", 0.9f);
NEBULA_TUNABLE_NAMED(f32, cNum_isMirrorAnimation_f1, "isMirrorAnimation.f1", 0.5f);

namespace {
    // The player's animation tables with the mods' animations (NebulaMods::takePlayerAnimations) in them: the game's
    // own when no mod adds one. Built the first time the player is, from the clips pHolder has (the player's archive is
    // the same in every stage); kept for the run.
    void getAnimationTables(const ResourceHolder* pHolder, XanimeGroupInfo** ppGroups, XanimeBckTable1** ppSingles) {
        static bool sBuilt = false;
        static XanimeGroupInfo* sGroups = marioAnimeTable;
        static XanimeBckTable1* sSingles = singleAnimeTable;
        if (!sBuilt) {
            sBuilt = true;
            const NebulaMods::PlayerAnimation* pAnimations = nullptr;
            const s32 count = NebulaMods::takePlayerAnimations(&pAnimations);
            const NebulaMods::PlayerAnimation** ppKept =
                count > 0 ? new (NebulaArena::root(), 0) const NebulaMods::PlayerAnimation*[count] : nullptr;
            s32 kept = 0;
            for (s32 i = 0; i < count; i++) {
                if (pHolder == nullptr || pHolder->mMotionResTable == nullptr || !pHolder->mMotionResTable->isExistRes(pAnimations[i].mFile)) {
                    nebula_log("mods: left out: player animation %s: the player's animations have no %s\n", pAnimations[i].mName,
                               pAnimations[i].mFile);
                    continue;
                }
                ppKept[kept++] = &pAnimations[i];
            }
            if (kept > 0) {
                s32 groups = 0;
                while (marioAnimeTable[groups].mParent.mAnimationName[0] != '\0') {
                    groups++;
                }
                s32 singles = 0;
                while (singleAnimeTable[singles].mParent.mAnimationName[0] != '\0') {
                    singles++;
                }

                // A mod's clip plays alone: its single entry comes before the game's, so it is found before the game's
                // single or blend of that name.
                XanimeBckTable1* pSingles = new (NebulaArena::root(), 0) XanimeBckTable1[kept + singles + 1];
                for (s32 i = 0; i < kept; i++) {
                    pSingles[i].mParent.mAnimationName = ppKept[i]->mName;
                    pSingles[i].mFileName = ppKept[i]->mFile;
                    pSingles[i].mAnimationHash = 0;
                    pSingles[i].mFileHash = 0;
                }
                for (s32 i = 0; i <= singles; i++) {
                    pSingles[kept + i] = singleAnimeTable[i];
                }

                // A name the game has keeps its group, at the mod's rate and blend; a new name is a group after the game's.
                XanimeGroupInfo* pGroups = new (NebulaArena::root(), 0) XanimeGroupInfo[groups + kept + 1];
                for (s32 i = 0; i < groups; i++) {
                    pGroups[i] = marioAnimeTable[i];
                }
                s32 total = groups;
                for (s32 i = 0; i < kept; i++) {
                    XanimeGroupInfo* pGroup = nullptr;
                    for (s32 j = 0; j < groups && pGroup == nullptr; j++) {
                        if (MR::isEqualString(pGroups[j].mParent.mAnimationName, ppKept[i]->mName)) {
                            pGroup = &pGroups[j];
                        }
                    }
                    if (pGroup == nullptr) {
                        pGroup = &pGroups[total++];
                        pGroup->init();
                        pGroup->mParent.mAnimationName = ppKept[i]->mName;
                        pGroup->mInterpoleFrame = 6;
                    }
                    pGroup->mRate = ppKept[i]->mRate;
                    if (ppKept[i]->mBlendFrames != 0) {
                        pGroup->mInterpoleFrame = ppKept[i]->mBlendFrames;
                    }
                }
                pGroups[total] = marioAnimeTable[groups];
                sGroups = pGroups;
                sSingles = pSingles;
            }
        }
        *ppGroups = sGroups;
        *ppSingles = sSingles;
    }
}  // namespace

MarioAnimator::MarioAnimator(MarioActor* pActor) : MarioModule(pActor) {
    init();
}

void MarioAnimator::init() {
    XanimeSwapTable* luigiAnimations = nullptr;
    if (gIsLuigi) {
        luigiAnimations = luigiAnimeSwapTable;
    }

    ResourceHolder* const pHolder = MR::getResourceHolder(mActor);
    XanimeGroupInfo* pGroups = nullptr;
    XanimeBckTable1* pSingles = nullptr;
    getAnimationTables(pHolder, &pGroups, &pSingles);
    mResourceTable = new XanimeResourceTable(pHolder, pGroups, marioAnimeAuxTable, marioAnimeOfsTable,
                                             reinterpret_cast< XanimeBckTable* >(pSingles), doubleAnimeTable, tripleAnimeTable,
                                             quadAnimeTable, luigiAnimations);

    mWalkState = 0;
    mPrevWalkState = 0;
    mWaitPhase = 1;

    mWalkWeights[0] = 0.0f;
    mWalkWeights[1] = 0.0f;
    mWalkWeights[2] = 0.0f;
    mWalkWeights[3] = 1.0f;

    NebulaMtxIdentity(mTiltMtx.toMtxPtr());
    NebulaMtxIdentity(mHomingMtx.toMtxPtr());
    NebulaMtxIdentity(mHipMtx.toMtxPtr());

    mTiltSide = 0.0f;
    mTiltDown = 0.0f;
    mTiltPrevFront.zero();

    mUpperRunning = false;
    mMirrorChecked = 0;
    mMirror = 0;
    mUpperDefaultSet = false;
    mCurrBck = nullptr;
    mRumbleHipRot = 0.0f;
    mRumblePower = 0.0f;
    mRumbleTimer = 0;
    mWaitHoldTimer = 0;
    mHomingAngle = 0.0f;

    initCallbackTable();

    mXanimePlayer = new XanimePlayer(MR::getModel(mActor), mResourceTable);

    changeDefault("基本");
    change("基本");

    mXanimePlayer->getCore()->enableJointTransform(MR::getModelData(mActor));

    mActor->mModelManager->mXanimePlayer = mXanimePlayer;
    mXanimePlayerUpper = new XanimePlayer(MR::getModel(mActor), mResourceTable, mXanimePlayer);
    changeDefaultUpper("基本");
    mXanimePlayerUpper->changeAnimation("基本");
    mXanimePlayerUpper->mCore->shareJointTransform(mXanimePlayer->mCore);
    NebulaMtxCopy(MR::tmpMtxRotYRad(PI), mMirrorMtx.toMtxPtr());
}

bool MarioAnimator::isAnimationStop() const {
    return mXanimePlayer->mCurrentAnimation == mXanimePlayer->mDefaultAnimation;
}

void MarioAnimator::targetWeight(f32* pWeight, f32 target, f32 rate) {
    if (getPlayer()->isSwimming()) {
        *pWeight = *pWeight * (1.0f - mActor->mConst->getTable()->mWeightBlendRatioSwim) + target * mActor->mConst->getTable()->mWeightBlendRatioSwim;
    } else if (getPlayer()->mStartSpinTimer != 0) {
        *pWeight = *pWeight * cNum_targetWeight_f1 + target * cNum_targetWeight_f2;
    } else {
        *pWeight = *pWeight * (1.0f - rate) + target * rate;
    }
}

void MarioAnimator::setWalkWeight(const f32* pWeights) {
    f32 rate = mActor->mConst->getTable()->mWeightBlendRatio;
    if (getPlayer()->mStartSpinTimer != 0) {
        rate = cNum_setWalkWeight_f1;
    }

    setBlendWeight(pWeights, rate);
}

void MarioAnimator::setBlendWeight(const f32* pWeights, f32 rate) {
    for (u32 i = 0; i < 4; i++) {
        targetWeight(&mWalkWeights[i], pWeights[i], rate);
        mXanimePlayer->changeTrackWeight(i, mWalkWeights[i]);
    }
}

void MarioAnimator::forceSetBlendWeight(const f32* pWeights) {
    for (u32 i = 0; i < 4; i++) {
        mWalkWeights[i] = pWeights[i];
        mXanimePlayer->changeTrackWeight(i, mWalkWeights[i]);
    }
}

void MarioAnimator::initWalkWeight() {
    mWalkWeights[0] = 0.0f;
    mWalkWeights[1] = 0.0f;
    mWalkWeights[2] = 0.0f;
    mWalkWeights[3] = 1.0f;
}

bool MarioAnimator::isLandingAnimationRun() const {
    if (isPlayerModeTeresa()) {
        return false;
    }

    if (isAnimationRun("着地") || isAnimationRun("着地B") || isAnimationRun("着地C") || isAnimationRun("着地ターン") ||
        isAnimationRun("着地幅とび")) {
        return true;
    }

    if (isAnimationRun("すべり着地")) {
        return true;
    }

    if (isAnimationRun(getPlayer()->mHipDropLandAnim)) {
        return true;
    }

    if (isAnimationRun("ハード着地")) {
        return true;
    }

    if (isAnimationRun("逆着地")) {
        return true;
    }

    if (isAnimationRun("ショート着地")) {
        return true;
    }

    if (MR::isBckPlaying(mActor, "pulloutgraspend")) {
        return true;
    }

    return false;
}

bool MarioAnimator::isCancelableAnimationRun() const {
    if (isAnimationRun("着地") || isAnimationRun("着地B") || isAnimationRun("着地C") || isAnimationRun("着地ターン") ||
        isAnimationRun("着地幅とび")) {
        return true;
    }

    if (isAnimationRun("すべり着地")) {
        return true;
    }

    if (MR::isBckPlaying(mActor, "pulloutgraspend")) {
        return true;
    }

    return false;
}

bool MarioAnimator::isWalkOrWaitingMotion() const {
    if (isAnimationStop()) {
        if (isAnimationRun("基本") || isAnimationRun("テニス基本")) {
            return true;
        }
    }

    if (isAnimationRun("坂前後ウエイト")) {
        return true;
    }

    if (isAnimationRun("坂左右ウエイト")) {
        return true;
    }

    if (isAnimationRun("カリカリ限界")) {
        return true;
    }

    if (isAnimationRun("飛び込み準備")) {
        return true;
    }

    return false;
}

void MarioAnimator::update() {
    if (mXanimePlayer->isAnimationRunSimple()) {
        if (!mActor->mInDemo && !mActor->mRemoteDemo) {
            Mario* player = getPlayer();
            if (!player->mMovementStates.inputLocked) {
                if (!mActor->mInRush) {
                    player = getPlayer();
                    if (player->mMovementStates.onGround) {
                        stopAnimation(nullptr);
                    }
                }
            }
        }
    }

    runningCallback();

    bool specialMode = false;
    if (mActor->mHidden || mActor->mMorphHidden) {
        specialMode = true;
    }

    if (specialMode) {
        mActor->updateSpecialModeAnimation();
        return;
    }

    u8 walkStateTable[] = {0, 1, 1, 2, 2, 2, 3, 3};

    Mario* player = getPlayer();
    u8 prevWalkState = player->mTargetWalkSpeedIndex;
    player = getPlayer();
    player->mPrevWalkSpeedIndex = prevWalkState;

    if (isWalkOrWaitingMotion() || isAnimationRun("しゃがみ基本")) {
        player = getPlayer();
        mWalkState = walkStateTable[player->mTargetWalkSpeedIndex];
    } else {
        mPrevWalkState = 0xFF;
    }

    player = getPlayer();
    if (player->mDrawStates.atEdge) {
        bool normalMode = !isPlayerModeHopper();
        if (normalMode) {
            player = getPlayer();
            if (!player->isStatusActive(MarioStatus_Freeze)) {
                if (isLandingAnimationRun()) {
                    stopAnimation(nullptr);
                }

                player = getPlayer();
                if (player->mMovementStates.hipDrop) {
                    getPlayer()->mMovementStates.jumping = false;
                    getPlayer()->mMovementStates.hipDrop = false;
                    stopAnimation(nullptr, "基本");
                }

                if (isAnimationStop()) {
                    changeAnimation("崖ふんばり");
                }
            }
        }

        goto afterBrake;
    }

    if (isAnimationRun("崖ふんばり")) {
        player = getPlayer();
        if (!player->mMovementStates.onGround) {
            stopAnimation("崖ふんばり", "落下");
            player = getPlayer();
            player->mLedgeFallTimer = 15;
        } else {
            stopAnimation("崖ふんばり");
        }
    }

    if (isCancelableAnimationRun()) {
        player = getPlayer();
        player->decideWalkSpeed();

        player = getPlayer();
        if (player->mTargetWalkSpeedIndex != 0) {
            stopAnimation(nullptr);
        }

        player = getPlayer();
        if (player->mMovementStates.squat) {
            stopAnimation(nullptr);
        }

        goto afterBrake;
    }

    player = getPlayer();
    if (player->mMovementStates.squat) {
        goto squatWalk;
    }

    player = getPlayer();
    if (player->mPrevMovementStates.squat) {
        player = getPlayer();
        if (!player->mMovementStates.jumping) {
            player = getPlayer();
            if (player->mMovementStates.onGround == 1) {
                goto squatWalk;
            }
        }
    }

    goto notSquat;

squatWalk:
    player = getPlayer();
    if (player->mMovementStates.slopeSlide) {
        goto afterBrake;
    }

    player = getPlayer();
    player->decideSquatWalkAnimation();

    if (mWalkState != mPrevWalkState) {
        switch (mWalkState) {
        case 0:
            player = getPlayer();
            player->startBas(nullptr, false, 0.0f, 0.0f);
            break;
        default: {
            if (!isAnimationRun("幅とび")) {
                player = getPlayer();
                player->startBas("SquatWalk", false, 0.0f, cNum_update_f1);
            } else {
                player = getPlayer();
                player->startBas(nullptr, false, 0.0f, 0.0f);
            }
        }
        }

        f32 frame = getAnimator()->mXanimePlayer->tellAnimationFrame();
        player = getPlayer();
        player->skipBas(frame);
    }

    mPrevWalkState = mWalkState;
    goto afterBrake;

notSquat:
    player = getPlayer();
    if (player->mMovementStates.onIce) {
        player = getPlayer();
        player->decideWalkSpeed();
        player = getPlayer();
        player->decideOnIceAnimation();
        goto afterBrake;
    }

    if (isWalkOrWaitingMotion()) {
        player = getPlayer();
        player->decideWalkSpeed();
        player = getPlayer();
        player->decideWalkAnimation();

        if (mActor->mBeeWallWalk != 0) {
            updateWalkBas("BeeCreepWalk", cNum_update_f2);
        } else {
            updateWalkBas("Walk", cNum_update_f3);
        }

        goto afterBrake;
    }

    if (isAnimationRun("氷上慣性走行")) {
        stopAnimation(nullptr);
        goto doBrake;
    }

    if (isAnimationRun("壁押し", MarioIndexedAnime_WallPush)) {
        player = getPlayer();
        player->decideWalkSpeed();
        player = getPlayer();
        player->checkWallPush();
        goto doBrake;
    }

    player = getPlayer();
    if (player->mMovementStates.turning || getPlayer()->mMovementStates.turnBrake) {
        player = getPlayer();
        player->decideWalkSpeed();
        goto doBrake;
    }

    if (!isAnimationRun("ハード着地")) {
        if (isAnimationRun("がんばり走り")) {
            player = getPlayer();
            if (player->mSlipPower < 1.0f) {
                stopAnimation(nullptr);
            }
        } else {
            mPrevWalkState = 0;
        }
    }

doBrake:
    player = getPlayer();
    player->updateBrakeAnimation();

afterBrake:
    if (isAnimationRun("基本") || isAnimationRun("がんばり走り")) {
        setTilt();
    } else {
        resetTilt();
    }

    if (isAnimationRun("スライダー尻")) {
        setHipSliderTilt();
    }

    if (isAnimationRun("坂すべり下向きあおむけ", MarioIndexedAnime_SlideDownFaceUp)) {
        setHipSlipTilt();
    }

    setHoming();
    setHand();

    if (mUpperRunning) {
        XanimePlayer* upperPlayer = mXanimePlayerUpper;
        upperPlayer->updateBeforeMovement();
        upperPlayer->updateAfterMovement();

        u8 idx = mXanimePlayerUpper->mFrameCtrlIndex;
        XanimeFrameCtrl& track = mXanimePlayerUpper->mFrameCtrls[idx];
        if (track.mState & 1) {
            XanimeFrameCtrl* ctrl = mXanimePlayerUpper->mFrameCtrl;
            if (ctrl->mAttribute != 1) {
                if (!mUpperDefaultSet) {
                    stopUpper(nullptr);
                }
            }
        }
    }

    updateJointRumble();
    mActor->updateSpecialModeAnimation();

    if (isAnimationStop()) {
        mCurrBck = mXanimePlayer->getCurrentBckName();
    }
}

void MarioAnimator::updateWalkBas(const char* pAnimName, f32 speed) {
    bool shouldStart = false;

    // Check if player has a special animation queued
    if (getPlayer()->mRunningBas) {
        Mario* player = getPlayer();
        const char* currentBck = mXanimePlayer->getCurrentBckName();

        if (!MR::isEqualString(currentBck, player->mRunningBas)) {
            shouldStart = true;
            if (MR::strcasecmp("DamageWait", getPlayer()->mRunningBas) == 0) {
                if (mActor->mHealth == 1) {
                    shouldStart = false;
                }
            }
        }
    }

    // Check animation state transition
    if (mWalkState == mPrevWalkState) {
        if (mWalkState != 0) {
            if (!getPlayer()->isRunningBas(pAnimName)) {
                goto startBas;
            }
        }

        if (mWalkState != 0) {
            goto updatePrevState;
        }

        if (!shouldStart) {
            goto updatePrevState;
        }
    }

    // Start or continue animation
startBas:
    switch (mWalkState) {
    case 0:
        getPlayer()->startBas(nullptr, false, 0.0f, 0.0f);
        break;
    default:
        getPlayer()->startBas(pAnimName, false, 0.0f, speed);
        break;
    }

    {
        f32 frame = getAnimator()->mXanimePlayer->tellAnimationFrame();
        getPlayer()->skipBas(frame);
    }

updatePrevState:
    mPrevWalkState = mWalkState;
}

void MarioAnimator::setHand() {
    Vec3f scale;
    scale.x = getPlayer()->mHandScaleX;
    scale.y = 1.0f;
    scale.z = 1.0f;
    XanimeCore* core = mXanimePlayer->mCore;
    u32 armR = MR::getJointIndex(mActor, "ArmR1");
    XjointTransform* armRJoint = core->getJointTransform(armR);
    armRJoint->mExtraScale = scale;
    core = mXanimePlayer->mCore;
    u32 armL = MR::getJointIndex(mActor, "ArmL1");
    XjointTransform* armLJoint = core->getJointTransform(armL);
    armLJoint->mExtraScale = scale;
}

extern const char* jname_chest;

void MarioAnimator::setTilt() {
    f32 tiltAngle = 0.0f;

    if (!MR::isNearZero(mTiltPrevFront)) {
        f32 frontDot = mTiltPrevFront.dot(getFrontVec());
        tiltAngle = 1.0f - frontDot;

        if (tiltAngle > cNum_setTilt_f1) {
            Vec3f cross = mTiltPrevFront.cross(getFrontVec());

            f32 gravDot = cross.dot(*getPlayer()->getGravityVec());
            if (gravDot < 0.0f) {
                tiltAngle = -tiltAngle;
            }
        } else {
            tiltAngle = 0.0f;
        }
    }

    mTiltPrevFront = getFrontVec();

    Mario* player = getPlayer();
    MarioConstTable* table = mActor->mConst->getTable();

    f32 speed = player->mWalkSpeed;
    f32 ratio = table->mTiltRatio * speed;
    tiltAngle *= ratio;

    const f32 maxTilt = HALF_PI;
    f32 absTilt = MR::abs(tiltAngle);
    if (absTilt >= maxTilt) {
        tiltAngle *= (maxTilt / absTilt);
    }

    f32 absOld = MR::abs(mTiltSide);
    f32 absNew = MR::abs(tiltAngle);

    if (absNew > absOld) {
        mTiltSide = cNum_setTilt_f2 * mTiltSide + cNum_setTilt_f3 * tiltAngle;
    } else {
        mTiltSide = cNum_setTilt_f4 * mTiltSide + cNum_setTilt_f5 * tiltAngle;
    }

    table = mActor->mConst->getTable();
    Mario* player2 = getPlayer();
    f32 lookDown = player2->mWalkSpeed * table->mLookDownRatio;

    Mario* player3 = getPlayer();
    f32 vertAngle = -player3->mSpinInertiaBlend;
    vertAngle *= PI;
    f32 eased = cNum_setTilt_f6 * mTiltDown + cNum_setTilt_f7 * lookDown;
    f32 target = vertAngle / cNum_setTilt_f8;
    mTiltDown = cNum_setTilt_f9 * eased + cNum_setTilt_f10 * target;

    Vec3f rotation(0.0f, mTiltSide, mTiltDown);
    MR::orderRotateMtx(0, rotation, mTiltMtx.toMtxPtr());

    XanimeCore* core = mXanimePlayer->mCore;
    u32 chestIdx = MR::getJointIndex(mActor, jname_chest);
    XjointTransform* jt = core->getJointTransform(chestIdx);
    jt->mBaseMtx = mTiltMtx.toMtxPtr();
}

void MarioAnimator::resetTilt() {
    NebulaMtxIdentity(mTiltMtx.toMtxPtr());
    mTiltSide = 0.0f;
    mTiltDown = 0.0f;
    mTiltPrevFront.zero();
}

void MarioAnimator::setHoming() {
    f32 angleH = 0.0f;
    s32 hasTarget = 0;
    Vec3f targetPos;
    if (mActor->getLookTargetSensor()) {
        HitSensor* lookTarget = mActor->getLookTargetSensor();
        targetPos = lookTarget->mPosition;
        hasTarget = 1;
    } else if (MR::isNormalTalking()) {
        LiveActor* talkActor = MR::getTalkingActor();
        if (talkActor != nullptr) {
            hasTarget = 1;
            f32 height = mActor->getFaceLookHeight(talkActor->mName);
            targetPos = talkActor->mPosition - getGravityVec() * height;
        }
    }

    if (hasTarget) {
        Vec3f toTarget = targetPos - mActor->mHeadPos;

        Mario* player = getPlayer();
        angleH = MR::vecKillElement(toTarget, player->mPostureUpVec, &toTarget);

        f32 dist = toTarget.length();
        f32 vAngle;

        if (dist < cNum_setHoming_f1) {
            vAngle = 0.0f;
        } else {
            f32 dist2 = toTarget.length();
            vAngle = Angle::atan2Radian(angleH, dist2);

            if (isStatusActiveID(MarioStatus_Talk)) {
                if (vAngle > 1.0f) {
                    vAngle = 1.0f;
                }
            } else {
                MarioConstTable* table = mActor->mConst->getTable();
                if (vAngle > table->mLookMaxAngleVP) {
                    vAngle = table->mLookMaxAngleVP;
                }
            }

            MarioConstTable* table = mActor->mConst->getTable();
            f32 lowerLimit = -table->mLookMaxAngleVM;
            if (vAngle < lowerLimit) {
                vAngle = lowerLimit;
            }
        }

        angleH = MR::diffAngleAbs(toTarget, getFrontVec());

        MarioConstTable* table = mActor->mConst->getTable();
        if (angleH > table->mLookMaxAngleH) {
            angleH = table->mLookMaxAngleH;
        }

        Vec3f cross = toTarget.cross(getFrontVec());
        Mario* player2 = getPlayer();
        f32 gravDot = cross.dot(player2->mPostureUpVec);
        if (gravDot < 0.0f) {
            angleH = -angleH;
        }

        Mtx rotMtx;
        MR::orderRotateMtx(5, Vec3f(0.0f, angleH, -vAngle), rotMtx);

        MR::blendMtx(mHomingMtx.toMtxPtr(), rotMtx, cNum_setHoming_f2, mHomingMtx.toMtxPtr());

        XanimeCore* core = mXanimePlayer->getCore();
        u32 headIdx = MR::getJointIndex(mActor, "Head");
        XjointTransform* jt = core->getJointTransform(headIdx);
        jt->mBaseMtx = mHomingMtx.toMtxPtr();
    } else {
        Mtx identMtx;
        NebulaMtxIdentity(identMtx);
        MR::blendMtx(mHomingMtx.toMtxPtr(), identMtx, cNum_setHoming_f3, mHomingMtx.toMtxPtr());
    }

    MarioConstTable* table = mActor->mConst->getTable();

    f32 blend = cNum_setHoming_f4 * mHomingAngle + cNum_setHoming_f5 * angleH;
    mHomingAngle = blend;

    f32 leftShoulderRot = 0.0f;
    f32 rightShoulderRot = 0.0f;

    if (angleH > 0.0f) {
        table = mActor->mConst->getTable();
        f32 ratio = blend / table->mLookMaxAngleH;
        rightShoulderRot = ratio * table->mLookShoulderMoveMax;
    } else if (angleH < 0.0f) {
        table = mActor->mConst->getTable();
        f32 ratio = -blend / table->mLookMaxAngleH;
        leftShoulderRot = ratio * table->mLookShoulderMoveMax;
    }

    XanimeCore* core = mXanimePlayer->getCore();
    u32 leftIdx = MR::getJointIndex(mActor, "ShoulderL");
    core->getJointTransform(leftIdx)->mTranslateOffset.x = leftShoulderRot;

    core = mXanimePlayer->getCore();
    u32 rightIdx = MR::getJointIndex(mActor, "ShoulderR");
    core->getJointTransform(rightIdx)->mTranslateOffset.x = rightShoulderRot;
}

void MarioAnimator::setHipSlidingTilt(f32 stickX, f32 stickY) {
    Vec2f input(stickX, stickY);
    f32 weights[4];

    weights[0] = (Vec2f(0.0f, 1.0f) - input).length();

    weights[1] = (Vec2f(0.0f, -1.0f) - input).length();

    weights[2] = (Vec2f(-1.0f, 0.0f) - input).length();

    weights[3] = (Vec2f(1.0f, 0.0f) - input).length();

    const f32 maxDist = cNum_setHipSlidingTilt_f1;
    for (s32 i = 0; i < 4; i++) {
        if (weights[i] >= maxDist) {
            weights[i] = 0.0f;
        } else {
            weights[i] = (maxDist - weights[i]) / maxDist;
        }
    }

    for (s32 i = 0; i < 4; i++) {
        f32 w = weights[i];
        f32 w2 = w * w;
        if (w < 0.0f) {
            weights[i] = -w2;
        } else {
            weights[i] = w2;
        }
    }

    MarioConstTable* table = mActor->mConst->getTable();
    f32 speed = table->mSliderTiltRatio;

    setBlendWeight(weights, speed);
}

void MarioAnimator::setHipSliderTilt() {
    f32 stickX = getStickX();
    f32 stickY = getStickY();
    setHipSlidingTilt(stickX, stickY);
}

void MarioAnimator::setHipSlipTilt() {
    Vec3f worldPadDir(getWorldPadDir());
    f32 frontDot = MR::vecKillElement(worldPadDir, getFrontVec(), &worldPadDir);
    Mario* player = getPlayer();
    f32 gravDot = MR::vecKillElement(worldPadDir, player->mSideVec, &worldPadDir);
    setHipSlidingTilt(-gravDot, frontDot);
}

void MarioAnimator::setSpeed(f32 speed) {
    mXanimePlayer->changeSpeed(speed);
}

f32 MarioAnimator::getFrame() const {
    return mXanimePlayer->tellAnimationFrame();
}

f32 MarioAnimator::getUpperFrame() const {
    return mXanimePlayerUpper->tellAnimationFrame();
}

void MarioAnimator::setWalkMode() {
    if (!getPlayer()->mMovementStates.tornado && getPlayer()->mMovementStates.turnStickNeutral && !getPlayer()->mMovementStates.squat && 0.0f == getPlayer()->mSlipPower &&
        static_cast< u8 >(getPlayer()->checkStickFrontBack()) == 2) {
        if (!isStatusActiveID(MarioStatus_Magic) && !isStatusActiveID(MarioStatus_Skate)) {
            if (getPlayer()->mMovementStates.onSkatableFloor) {
                change("ブレーキ滑り床");
            } else {
                change("ブレーキ");
            }
        }
    } else if (isAnimationRun("ブレーキ")) {
        getPlayer()->doBrakingAnimation();
        getPlayer()->mTargetWalkSpeedIndex = 0;
    } else {
        mXanimePlayer->stopAnimation("ブレーキ滑り床");
    }

    if (getPlayer()->mMovementStates.turnBrake && !getPlayer()->mMovementStates.slopeSlide) {
        getPlayer();
    }
}

void MarioAnimator::calc() {
    bool specialMode = false;
    if (mActor->mHidden || mActor->mMorphHidden) {
        specialMode = true;
    }

    if (specialMode) {
        return;
    }

    Mario* player = getPlayer();
    Vec3f playerPos(player->mLookOfs);
    XjointTransform* joint0 = mXanimePlayer->mCore->getJointTransform(0);
    joint0->mWorldTranslate = playerPos;
    if (mUpperRunning) {
        // Both players animation calculation
        mXanimePlayer->calcAnm(0);
        u32 spineIdx = MR::getJointIndex(mActor, "Spine1");
        mXanimePlayerUpper->calcAnm((u16)spineIdx);
        u32 partsIdx = MR::getJointIndex(mActor, "PartsControl");
        mXanimePlayerUpper->overWriteMtxCalc((u16)partsIdx);
    } else {
        u32 spineIdx = MR::getJointIndex(mActor, "Spine1");
        ModelData* modelData = mActor->getModelData();
        Joint* joint = modelData->getJointAt(spineIdx);
        joint->setMtxCalc(nullptr);
        mXanimePlayer->calcAnm(0);
    }

    mXanimePlayer->mCore->mCalcMode = XanimeCore::CalcMode_BlendSpecial;
    MR::calcModel(mActor);

    mXanimePlayer->mCore->mCalcMode = XanimeCore::CalcMode_ScaleBlendSpecial;
    MR::calcModel(mActor);
    mXanimePlayer->clearAnm(0);

    if (mUpperRunning) {
        u32 spineIdx = MR::getJointIndex(mActor, "Spine1");
        mXanimePlayer->clearAnm((u16)spineIdx);
        u32 partsIdx = MR::getJointIndex(mActor, "PartsControl");
        mXanimePlayerUpper->clearMtxCalc((u16)partsIdx);
    }
}

void MarioAnimator::change(const char* pName) {
    if (mActor->mAnimationFrozen) {
        return;
    }

    if (isTeresaClear()) {
        mXanimePlayer->changeAnimation(pName);
    }

    const char* bck = mXanimePlayer->getCurrentBckName();
    if (bck != nullptr) {
        const XanimeGroupInfo* info = mXanimePlayer->mCurrentAnimation;
        if (info->mAttribute == 2) {
            f32 loopFrame = info->mLoop, endFrame = info->mEnd;
            getPlayer()->startBas(bck, false, loopFrame, endFrame);
        } else {
            getPlayer()->startBas(bck, false, 0.0f, 0.0f);
        }

        mActor->setBlink(bck);
    } else {
        getPlayer()->startBas(nullptr, false, 0.0f, 0.0f);
        mActor->setBlink(nullptr);
    }

    mActor->changeSpecialModeAnimation(pName);
    mCurrBck = bck;
    entryCallback(pName);
}

void MarioAnimator::changeUpper(const char* pName) {
    mXanimePlayerUpper->changeAnimation(pName);
    mUpperRunning = true;
}

void MarioAnimator::stopUpper(const char* pName) {
    if (!mUpperRunning) {
        return;
    }

    bool shouldStop = false;
    if (pName == nullptr) {
        shouldStop = true;
    } else if (mXanimePlayerUpper->isRun(pName)) {
        shouldStop = true;
    }

    if (shouldStop) {
        u32 jointID = MR::getJointIndex(mActor, "Spine1");
        XanimeCore* upperCore = mXanimePlayerUpper->getCore();
        ModelData* modelData = mActor->getModelData();
        XanimeCore* lowerCore = mXanimePlayer->getCore();
        upperCore->freezeCopy(modelData, lowerCore, jointID, 8);
        mXanimePlayerUpper->stopAnimation();
        mUpperRunning = false;
    }
}

void MarioAnimator::changeDefault(const char* pName) {
    getPlayer()->startBas(nullptr, false, 0.0f, 0.0f);

    mXanimePlayer->setDefaultAnimation(pName);
}

bool MarioAnimator::isDefaultAnimationRun(const char* pName) const {
    return MR::isEqualString(mXanimePlayer->getDefaultAnimationName(), pName);
}

void MarioAnimator::changeDefaultUpper(const char* pName) {
    if (pName != nullptr) {
        mUpperDefaultSet = true;
        mXanimePlayerUpper->setDefaultAnimation(pName);
    } else {
        mUpperDefaultSet = false;
    }
}

u32 MarioAnimator::getUpperJointID() const {
    return MR::getJointIndex(mActor, "Spine1");
}

const char* jname_chest = "Spine1";

void MarioAnimator::updateJointRumble() {
    u32 timer = mRumbleTimer;
    if (timer == 0) {
        mRumbleHipRot = 0.0f;
        return;
    }

    u32 newTimer = timer - 1;
    mRumbleTimer = newTimer;

    f32 t = (f32)newTimer * Angle::cRadian360 / cNum_updateJointRumble_f1;
    f32 sineVal = Angle::sinRadian(t);

    f32 power = -mRumblePower;

    if (mRumbleTimer < 30) {
        f32 fade = (f32)mRumbleTimer / cNum_updateJointRumble_f2;
        sineVal *= fade;
    }

    MarioActor* actor = mActor;
    XanimeCore* core = mXanimePlayer->mCore;
    XjointTransform* larmJt = core->getJointTransform(MR::getJointIndex(actor, "ArmL1"));
    larmJt->mTranslateOffset.y = sineVal * power;

    core = mXanimePlayer->mCore;
    XjointTransform* rarmJt = core->getJointTransform(MR::getJointIndex(mActor, "ArmR2"));
    rarmJt->mTranslateOffset.y = sineVal * power;

    f32 hipRot = cNum_updateJointRumble_f3 * ((f32)mRumbleTimer / cNum_updateJointRumble_f4);

    if (getPlayer()->mMovementStates.onGround && !getPlayer()->mMovementStates.mode2D && getPlayer()->mTargetWalkSpeedIndex == 0 &&
        getPlayer()->calcAngleD(getPlayer()->mGroundNorm) > cNum_updateJointRumble_f5) {
        Vec3f dir;
        const Vec3f* airGrav = &getAirGravityVec();
        Mario* playerVec = getPlayer();
        MR::vecKillElement(playerVec->mGroundNorm, *airGrav, &dir);
        MR::normalizeOrZero(&dir);

        if (getFrontVec().dot(dir) < -cNum_updateJointRumble_f6) {
            hipRot = -hipRot;
        } else {
            hipRot = getFrontVec().dot(dir) > cNum_updateJointRumble_f7 ? hipRot : cNum_updateJointRumble_f8 * mRumbleHipRot;
        }

        NebulaMtxRotRad(mHipMtx.toMtxPtr(), 'Z', hipRot);
    } else {
        NebulaMtxIdentity(mHipMtx.toMtxPtr());
    }

    mRumbleHipRot = hipRot;

    core = mXanimePlayer->mCore;
    XjointTransform* hipJt = core->getJointTransform(MR::getJointIndex(mActor, "Spine2"));
    hipJt->mBaseMtx = mHipMtx.toMtxPtr();
}

void MarioAnimator::addRumblePower(f32 power, u32 time) {
    if (mRumblePower < power) {
        mRumblePower = power;
    }

    mRumbleTimer = time;
}

void MarioAnimator::setUpperRotateY(f32 angle) {
    NebulaMtxRotRad(mHipMtx.toMtxPtr(), 'X', angle);
    XanimeCore* core = mXanimePlayer->mCore;
    XjointTransform* jt = core->getJointTransform(MR::getJointIndex(mActor, "Spine2"));
    jt->mBaseMtx = mHipMtx.toMtxPtr();
}

void MarioAnimator::clearAllJointTransform() {
    {
        XanimeCore* core = mXanimePlayer->mCore;
        XjointTransform* jt = core->getJointTransform(MR::getJointIndex(mActor, "ShoulderL"));
        jt->mBaseMtx = nullptr;
    }

    {
        XanimeCore* core = mXanimePlayer->mCore;
        XjointTransform* jt = core->getJointTransform(MR::getJointIndex(mActor, "ShoulderR"));
        jt->mBaseMtx = nullptr;
    }

    {
        XanimeCore* core = mXanimePlayer->mCore;
        XjointTransform* jt = core->getJointTransform(MR::getJointIndex(mActor, "Head"));
        jt->mBaseMtx = nullptr;
    }

    {
        XanimeCore* core = mXanimePlayer->mCore;
        XjointTransform* jt = core->getJointTransform(MR::getJointIndex(mActor, "Spine2"));
        jt->mBaseMtx = nullptr;
    }

    {
        XanimeCore* core = mXanimePlayer->mCore;
        XjointTransform* jt = core->getJointTransform(MR::getJointIndex(mActor, "ArmL1"));
        jt->mTranslateOffset.y = 0.0f;
    }

    {
        XanimeCore* core = mXanimePlayer->mCore;
        XjointTransform* jt = core->getJointTransform(MR::getJointIndex(mActor, "ArmR2"));
        jt->mTranslateOffset.y = 0.0f;
    }

    {
        XanimeCore* core = mXanimePlayer->mCore;
        u32 chestIdx = MR::getJointIndex(mActor, jname_chest);
        XjointTransform* jt = core->getJointTransform(chestIdx);
        jt->mBaseMtx = nullptr;
    }

    {
        XanimeCore* core = mXanimePlayer->mCore;
        u32 hipIdx = MR::getJointIndex(mActor, "Hip");
        XjointTransform* jt = core->getJointTransform(hipIdx);
        jt->mBaseMtx = nullptr;
    }

    Vec3f zeroVec(0.0f, 0.0f, 0.0f);
    XjointTransform* jt = mXanimePlayer->mCore->getJointTransform(0);
    jt->mWorldTranslate = zeroVec;

    mRumbleTimer = 0;
    mRumblePower = 0.0f;
}

bool MarioAnimator::isMirrorAnimation() {
    if (mActor->mTakeCount) {
        return false;
    }

    Vec3f camDir = getCamDirX();
    Mario* player = getPlayer();
    f32 dot = player->mPostureUpVec.dot(getCamDirY());
    if (dot < 0.0f) {
        camDir = -camDir;
    }

    if (isAnimationRun("壁すべり") || isAnimationRun("壁くっつき")) {
        if (!mMirrorChecked) {
            const Vec3f& wallNorm = getPlayer()->getWallNorm();
            f32 wallDot = camDir.dot(wallNorm);
            if (wallDot < -cNum_isMirrorAnimation_f1) {
                mMirror = true;
            } else {
                mMirror = false;
            }

            mMirrorChecked = true;
        }

        return mMirror;
    }

    if (getPlayer()->mMovementStates.mode2D) {
        if (isAnimationRun("ブレーキ") || isAnimationRun("ターンブレーキ") || isAnimationRun("ターンジャンプ")) {
            if (!mMirrorChecked) {
                f32 frontDot = camDir.dot(getPlayer()->mFrontVec);
                if (frontDot > 0.0f) {
                    mMirror = true;
                } else {
                    mMirror = false;
                }

                mMirrorChecked = true;
            }

            return mMirror;
        }
    }

    // Clear cache flag
    mMirrorChecked = false;
    if (isAnimationRun("左空パンチ")) {
        return true;
    }

    return false;
}

void MarioAnimator::switchMirrorMode() {
    f32 scale = 1.0f;
    PlayerModel* model = static_cast< PlayerModel* >(MR::getModel(mActor));
    if (isMirrorAnimation()) {
        u32* modelFlags = (u32*)&model->mFlags;
        *modelFlags |= 1;
        XjointTransform* jt = mXanimePlayer->mCore->getJointTransform(0);
        Vec3f mirrorScale;
        mirrorScale.x = scale;
        mirrorScale.y = scale;
        mirrorScale.z = -scale;
        jt->mExtraScale = mirrorScale;

        Mtx invBase;
        MtxPtr base = mActor->getBaseMtx();
        NebulaMtxInverse(base, invBase);
        MR::multMtx(mMirrorMtx, invBase, MR::tmpMtxRotYRad(PI));

        base = mActor->getBaseMtx();
        MR::multMtx(mMirrorMtx, mMirrorMtx, base);

        jt->mMirrorMtx = mMirrorMtx;
    } else {
        u32* modelFlags = (u32*)&model->mFlags;
        *modelFlags &= ~1;
        XjointTransform* jt = mXanimePlayer->mCore->getJointTransform(0);
        Vec3f normalScale;
        normalScale.x = scale;
        normalScale.y = scale;
        normalScale.z = scale;
        jt->mExtraScale = normalScale;
        jt->mMirrorMtx = nullptr;
    }
}

void MarioAnimator::changePickupAnimation(const HitSensor* pSensor) {
    u32 type = pSensor->mType;
    switch (type) {
    case ACTMES_STAR_PIECE_GIFT_MAX:
        mActor->mTakingFixedPos = mActor->mHandHoldFixedPos;
        changeAnimation("カブ抜き");
        mActor->changeNullAnimation("PullOut", -2);
        getPlayer()->stopWalk();
        break;
    case ACTMES_STAR_PIECE_GIFT:
    case ACTMES_STAR_PIECE_GIFT_1:
        mActor->mTakingFixedPos = mActor->mHandFixedPos;
        if (!getPlayer()->isSwimming()) {
            if (mActor->mTakenSensor == pSensor) {
                changeAnimationUpper("ひろいクイック");
                playEffect("ひろいクイック");
                mActor->clearNullAnimation(-3);
                startPadVib(2);
            } else if (getPlayer()->mMovementStates.jumping && !getPlayer()->mMovementStates.hipDrop) {
                changeAnimation("ひろい空中");
                mActor->clearNullAnimation(-3);
                getPlayer()->stopWalk();
            } else {
                changeAnimation("ひろい");
                mActor->changeNullAnimation("CarryStart", -2);
                getPlayer()->stopWalk();
            }
        } else {
            mActor->clearNullAnimation(0);
            startPadVib(2);
        }

        break;
    }
}

void MarioAnimator::updateTakingAnimation(const HitSensor* pSensor) {
    if (pSensor == nullptr) {
        return;
    }

    LiveActor* sensorActor = pSensor->mHost;
    if (MR::isDead(sensorActor)) {
        if (mActor->mTakeCount != 0) {
            mActor->rushDropThrowMemoSensor();
        }

        return;
    }

    u32 type = pSensor->mType;
    switch (type) {
    case ACTMES_STAR_PIECE_GIFT_MAX:
        stopAnimation(nullptr);
        changeAnimationUpper("カブウエイト");
        mActor->clearNullAnimation(0);
        mActor->offTakingFlag();
        break;
    case ACTMES_STAR_PIECE_GIFT:
    case ACTMES_STAR_PIECE_GIFT_1:
        if (!getPlayer()->isSwimming()) {
            s32 shouldChange = 1;
            if (isAnimationRun("ひろいクイック")) {
                shouldChange = isAnimationTerminateUpper(nullptr);
            }

            if (shouldChange) {
                if (!isAnimationRun("ひろい空中") && mActor->isStopNullAnimation()) {
                    changeAnimationUpper("ひろいウエイト");
                    startPadVib(2);
                    mActor->clearNullAnimation(0);
                    mActor->offTakingFlag();
                }
            }
        } else {
            MR::emitEffect(pSensor->mHost, "SwimBubble");
        }

        break;
    }
}

void MarioAnimator::changeThrowAnimation(const HitSensor* pSensor) {
    u32 type = pSensor->mType;
    switch (type) {
    case ACTMES_STAR_PIECE_GIFT_MAX:
        stopAnimationUpper(nullptr);
        changeAnimation("両手投げ");
        startPadVib("マリオ[亀投げ]");
        break;
    case ACTMES_STAR_PIECE_GIFT:
    case ACTMES_STAR_PIECE_GIFT_1:
        stopAnimationUpper(nullptr);
        if (getPlayer()->isSwimming()) {
            changeAnimation("水泳亀投げ", "水泳基本");
        } else {
            changeAnimation("投げ");
        }

        startPadVib("マリオ[亀投げ]");
        break;
    }
}

void MarioAnimator::waterToGround() {
    HitSensor* swimSensor;
    if (mActor->mTakeCount == 0) {
        swimSensor = nullptr;
    } else {
        swimSensor = mActor->mTakeSensors[0];
    }

    if (swimSensor == nullptr) {
        return;
    }

    u32 state = swimSensor->mType;
    switch (state) {
    case ACTMES_STAR_PIECE_GIFT:
    case ACTMES_STAR_PIECE_GIFT_1:
        changeAnimationUpper("ひろいウエイト");
        mActor->clearNullAnimation(0);
        MR::deleteEffect(swimSensor->mHost, "SwimBubble");
        break;
    case ACTMES_STAR_PIECE_GIFT_MAX:
        changeAnimationUpper("カブウエイト");
        mActor->clearNullAnimation(0);
        break;
    }
}

HitSensor* MarioActor::getLookTargetSensor() const {
    if (mInRush) {
        return nullptr;
    }

    if (isSleeping()) {
        return nullptr;
    }

    return mTakeCandidate;
}
