#include "Game/Player/MarioFpView.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_isDisableStayHere_f1, "isDisableStayHere.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f1, "start.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 0.5f);

namespace {
    static bool sIsDisabled;
};  // namespace

bool Mario::isDisableStayHere() const {
    if (getMovementStates().jumping) {
        return true;
    }

    if (!getMovementStates().onGround) {
        return true;
    }

    if (mLandTimer < 5) {
        return true;
    }

    if (getMovementStates().squat) {
        return true;
    }

    if (isDamaging()) {
        return true;
    }

    if (getMovementStates().damaged) {
        return true;
    }

    if (MR::getKarikariClingNum() != 0) {
        return true;
    }

    if (getPlayer()->getMovementStates().slopeSlide) {
        return true;
    }

    if (mWalkSpeed > cNum_isDisableStayHere_f1) {
        return true;
    }

    if (getStickP() != 0.0f) {
        return true;
    }

    if (mFloorCode == 27) {
        return true;
    }

    if (mFloorCode == 28) {
        return true;
    }

    if (getDrawStates().enforcedMove) {
        return true;
    }

    if (getMovementStates().baseRotated) {
        return true;
    }

    if (!MR::isNearZero(mReactionVec)) {
        return true;
    }

    if (!MR::isNearZero(mPushVec)) {
        return true;
    }

    if (!MR::isNearZero(mPushVelocity)) {
        return true;
    }

    if (!MR::isNearZero(mRelativeMove)) {
        return true;
    }

    if (mActor->mBeeWallWalk != 0) {
        return true;
    }

    if (getMovementStates().mode2D) {
        return true;
    }

    if (getMovementStates().mode25D) {
        return true;
    }

    if (mActionStates.circular2D) {
        return true;
    }

    if (mSinkTimer) {
        return true;
    }

    if (mActor->mInDemo) {
        return true;
    }

    return mActor->mRemoteDemo;
}

bool Mario::isDisableFpViewMode() const {
    if (mCurrentState) {
        return true;
    }

    return isDisableStayHere();
}

MarioFpView::MarioFpView(MarioActor* pActor) : MarioState(pActor, MarioStatus_FpView), mDelay(), mIsActive(), mBlurOffset() {
}

bool MarioFpView::start() {
    MR::startSubjectiveCamera(-1);
    changeAnimation("見る");

    ::sIsDisabled = true;

    mDelay = 0;
    mIsActive = true;
    mBlurOffset = cNum_start_f1;

    return true;
}

bool MarioFpView::update() {
    if (::sIsDisabled) {
        ::sIsDisabled = false;
        return true;
    }

    mBlurOffset -= cNum_update_f1;
    if (mBlurOffset < 0.0f) {
        mBlurOffset = 0.0f;
    }

    if (getPlayer()->isDisableStayHere()) {
        return false;
    }

    if (MR::testFpViewOutTrigger() && mDelay == 0) {
        MR::startSystemSE("SE_SY_CAMERA_RESET");

        MR::endSubjectiveCamera(15);

        mDelay = 20;
        mIsActive = false;
    }

    if (mDelay != 0 && --mDelay == 0) {
        return false;
    }

    return true;
}

bool MarioFpView::close() {
    if (mIsActive) {
        MR::endSubjectiveCamera(15);
    }

    stopAnimation("見る");

    mActor->showBeeFur();

    return true;
}

void MarioFpView::forceClose() {
    MR::endSubjectiveCamera(1);

    mIsActive = false;
}

void Mario::tryFpViewMode() {
    if (isStatusActive(MarioStatus_FpView)) {
        return;
    }

    stopWalk();

    changeStatus(mFpView);

    MR::startSystemSE("SE_SY_CAMERA_FP_VIEW");
}

f32 MarioFpView::getBlurOffset() const {
    return mBlurOffset;
}
