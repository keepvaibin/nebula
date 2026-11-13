#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/System/GameRules.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioFpView.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/RushEndInfo.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"

bool MarioActor::respawnExtra() {
    if (!mExtra) {
        return false;
    }
    // What it holds or rides goes, as forceKill lets it go.
    if (mInRush) {
        if (mRushSensor != nullptr && !mRushSensor->receiveMessage(ACTMES_RUSH_CANCEL, getSensor("body"))) {
            mRushSensor->receiveMessage(ACTMES_RUSH_FORCE_CANCEL, getSensor("body"));
        }
        if (mInRush) {
            RushEndInfo endInfo(nullptr, RushEndInfo::Mode_Release, Vec3f(0.0f, 0.0f, 0.0f), false, 0);
            endRush(&endInfo);
        }
        mInRush = false;
    }
    if (!MR::isDead(mSpinTico)) {
        mSpinTico->kill();
    }
    mMario->closeStatus(nullptr);
    mMario->stopJump();
    mVelocity.zero();
    mLastMove.zero();
    mControlVelocity.zero();
    mPosition.set(mExtraHome);
    mRotation.set(mExtraHomeRotate);
    mMario->mPosition = mExtraHome;
    mMario->setHeadAndFrontVecFromRotate(mRotation);
    mPrevControlPos = mPosition;
    mBaseMtxSet = true;
    mCameraTrans = mExtraHome;
    const u32 startingHealth = static_cast< u32 >(GameRules::playerHealth());
    mMaxHealth = startingHealth;
    mHealth = startingHealth;
    resetWaterLife();
    mLifeDamageQueue = 0;
    mPressTimer = 0;
    mWaitNextNerve = nullptr;
    mWaitTimer = 0;
    mGameOverKind = 0;
    mAnimationFrozen = false;
    mForceKilled = false;
    mHidden = false;
    mBindOn = true;
    mBlurState = 0;
    MR::releaseAnimFrame(this);
    mMario->changeAnimation("基本");
    // A new life: its draws are not blended from where it fell.
    MotionSignal::newLife(this);
    MR::updateHitSensorsAll(this);
    setNerve(GET_NERVE(MarioActor, MarioActorNrvWait));
    return true;
}

void MarioActor::endExtra() {
    if (!respawnExtra()) {
        return;
    }
    kill();
}

void MarioActor::forceKill(u32 type) {
    if (mMario->getMovementStates().invincible) {
        return;
    }

    if (!isEnableNerveChange()) {
        return;
    }

    // An extra player's death ends nothing: it stands again at its home.
    if (respawnExtra()) {
        return;
    }

    // -1: a plugin keeps the player alive (and moves them, and gives life back, as its rule says).
    if (NebulaMods::fire(NEBULA_MOD_EVENT_PLAYER_MISS, static_cast< s32 >(type)) == -1) {
        return;
    }

    mBlurState = 0;

    if (!MR::isDead(mSpinTico)) {
        mSpinTico->kill();
    }

    if (mMario->isStatusActive(MarioStatus_FpView)) {
        mMario->mFpView->forceClose();
        mMario->closeStatus(nullptr);
    }

    if (mInRush) {
        if (mRushSensor != nullptr && mRushSensor->isType(ACTMES_GROUP_SHOW)) {
            mForceKilled = true;
            mAnimationFrozen = true;
        }

        if (mInRush) {
            if (!mRushSensor->receiveMessage(ACTMES_RUSH_CANCEL, getSensor("body"))) {
                mRushSensor->receiveMessage(ACTMES_RUSH_FORCE_CANCEL, getSensor("body"));
            }

            if (mInRush) {
                RushEndInfo endInfo(nullptr, RushEndInfo::Mode_Release, Vec3f(0.0f, 0.0f, 0.0f), false, 0);
                endRush(&endInfo);
            }
        }

        mInRush = false;
    }

    if (type == 0) {
        mMario->doAbyssDamage();
        return;
    }

    if (type == 3) {
        mMario->startPadVib(3);
    }

    if (type == 4) {
        mMario->startPadVib(3);
        initDarkMask();
        mMario->doDarkDamage();

        return;
    }

    switch (type) {
    case 1:
    case 2:
    case 5:
        MR::deactivateDefaultGameLayout();

    default:
        mGameOverKind = type;
        mHealth = 0;

        setNerve(GET_NERVE(MarioActor, MarioActorNrvGameOver));
        break;
    }
}

void MarioActor::exeGameOver() {
    if (respawnExtra()) {
        return;
    }

    mVelocity.zero();
    calcHeadPos();
    updateRealMtx();

    if (getNerveStep() == 32) {
        mBlurState = 0;
    }

    mTakeCandidate = nullptr;

    NebulaMtxIdentity(mMarioAnim->mHomingMtx);

    damageDropThrowMemoSensor();

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("ゲームオーバー");
        changeGameOverAnimation();
    } else {
        switch (mGameOverKind) {
        case 2:
            MR::startPlayerEvent("ゴーストレース負け");
            break;

        case 1:
            MR::startPlayerEvent("レース負け");
            changeGameOverAnimation();
            break;

        case 5:
            MR::startPlayerEvent("レース負け");
            break;

        default:
            MR::startPlayerEvent("マリオダウン");
        }
    }

    MR::startGlobalEventCameraNoTarget("昇天カメラ");
    MR::startStarPointerModeDemoMarioDeath(this);
}

void MarioActor::exeGameOverAbyss() {
    if (respawnExtra()) {
        return;
    }

    mVelocity = getLastMove() * 0.99f + mCamDirZ * 35.0f * 0.01f;
    mLastMove = mVelocity;

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();
    MR::startGlobalEventCameraNoTarget("奈落カメラ");

    mBindOn = false;

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("ゲームオーバー");
    } else {
        MR::startPlayerEvent("マリオ奈落");
    }

    MR::startStarPointerModeDemoMarioDeath(this);
    mMario->changeAnimationNonStop("奈落ダウン");

    mAnimationFrozen = true;
}

void MarioActor::exeGameOverFire() {
    if (respawnExtra()) {
        return;
    }

    mVelocity.zero();
    calcHeadPos();
    updateRealMtx();

    if (getNerveStep() == 120) {
        mBlurState = 0;
    }

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("ゲームオーバー");
        MR::startGlobalEventCameraNoTarget("昇天カメラ");
    } else {
        MR::startPlayerEvent("マリオ炎ダウン");
        MR::startGlobalEventCameraNoTarget("奈落カメラ");
    }

    MR::startStarPointerModeDemoMarioDeath(this);
}

void MarioActor::exeGameOverSink() {
    if (respawnExtra()) {
        return;
    }

    mVelocity.zero();
    calcHeadPos();
    updateRealMtx();

    if (getNerveStep() == 120) {
        mBlurState = 0;
    }

    if (MR::isFirstStep(this)) {
        MR::setCubeBgmChangeInvalid();
        MR::clearBgmQueue();
        changeGameOverAnimation();

        if (MR::getPlayerLeft() == 0) {
            MR::startPlayerEvent("ゲームオーバー");
            MR::startGlobalEventCameraNoTarget("昇天カメラ");
        } else {
            MR::startPlayerEvent("マリオダウン");
            MR::startGlobalEventCameraNoTarget("奈落カメラ");
        }

        MR::startStarPointerModeDemoMarioDeath(this);

        if (mMario->mFloorCode == 25 || mMario->mFloorCode == 17) {
            playEffect("砂埋まり体");
            playEffect("砂埋まり手");
        }

        if (mMario->mFloorCode == 31) {
            playEffect("泥埋まり体");
            playEffect("泥埋まり手");
        }
    }

    if (mMario->mFloorCode == 31 || mMario->mFloorCode == 18) {
        playSound("沼死亡", -1);

        if (getNerveStep() == 90) {
            playSound("声沼沈み死亡", -1);
        }
    } else {
        playSound("砂死亡", -1);

        if (getNerveStep() == 90) {
            playSound("声砂沈み死亡", -1);
        }
    }
}

void MarioActor::exeGameOverNonStop() {
    if (respawnExtra()) {
        return;
    }

    if (!MR::isFirstStep(this)) {
        return;
    }

    MR::setCubeBgmChangeInvalid();
    MR::clearBgmQueue();

    if (MR::getPlayerLeft() == 0) {
        MR::startPlayerEvent("ゲームオーバー");
        MR::startGlobalEventCameraNoTarget("昇天カメラ");
    } else {
        MR::startPlayerEvent("マリオ炎ダウン");
        MR::startGlobalEventCameraNoTarget("奈落カメラ");
    }

    MR::startStarPointerModeDemoMarioDeath(this);
}

void MarioActor::exeTimeWait() {
    if (mWaitTimer != getNerveStep()) {
        return;
    }

    setNerve(mWaitNextNerve);
    mWaitNextNerve = nullptr;
}
