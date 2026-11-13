#include "Game/Player/MarioActor.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"

void MarioActor::resetPadSwing() {
    mSubSpinTrigger = false;
    mMainSpinTrigger = 0;
    mMainSpinBlock = 10;
    mSubSpinBlock = 10;
}

bool MarioActor::isRequestHipDrop() const {
    if (mInRush || mStunTimer != 0) {
        return false;
    }

    if (mRushJustEnded) {
        return false;
    }

    return checkButtonType(mHipDropButton, false);
}

bool MarioActor::isRequestRush() const {
    if (!mSwingPermission) {
        return false;
    }

    if (mSpinIntervalTimer != 0) {
        return false;
    }

    if (mSwingInhibitTimer != 0) {
        return false;
    }

    if (!mMario->isEnableRush()) {
        return false;
    }

    if (mRushJustEnded) {
        return false;
    }

    if (mPlayerMode == 5) {
        return false;
    }

    bool out = checkButtonType(mRushButton, false);
    if (mMario->mActionStates.holdButtonAfterRush) {
        if (!out) {
            mMario->mActionStates.holdButtonAfterRush = false;
        }

        out = false;
    }

    return out;
}

bool MarioActor::isRequestThrow() const {
    if (!mSwingPermission) {
        return false;
    }

    if (!mMario->isEnableRush()) {
        return false;
    }

    bool out = checkButtonType(mRushButton, false);
    if (mMario->mActionStates.holdButtonAfterRush) {
        if (!out) {
            mMario->mActionStates.holdButtonAfterRush = false;
        }

        out = false;
    }

    return out;
}

bool MarioActor::isRequestSpin() const {
    if (mRushJustEnded) {
        return false;
    }

    if (isJumping() && mMario->mAirTimer > 6 && !mExtra && MR::isCoStarFree() && MR::isStarPointerPointing2P(this, "弱", true, false) && MR::isPressedJump(1)) {
        return true;
    }

    return checkButtonType(mSpinButton, false);
}

bool MarioActor::isRequestJump() const {
    if (mInRush || mStunTimer != 0) {
        return false;
    }

    if (mRushJustEnded) {
        return false;
    }

    return checkButtonType(mJumpButton, false);
}

bool MarioActor::isRequestJump2P() const {
    if (mMario->isInputDisable()) {
        return false;
    }

    if (mRemoteDemo) {
        return false;
    }

    if (mInRush || mStunTimer != 0) {
        return false;
    }

    if (mRushJustEnded) {
        return false;
    }

    if (!mExtra && MR::isCoStarFree() && MR::isStarPointerPointing2P(this, "弱", true, false) && MR::isPressedJump(1)) {
        return true;
    }

    return false;
}

bool MarioActor::isKeepJump() const {
    return checkButtonType(5, false);
}

bool MarioActor::isKeepJump2P() const {
    if (mMario->isInputDisable()) {
        return false;
    }

    if (mRemoteDemo) {
        return false;
    }

    // The co-star's: none for an extra player, nor while one is driven by the second controller.
    return !mExtra && MR::isCoStarFree() && MR::isHeldJump(1);
}

bool MarioActor::checkButtonType(u16 type, bool isObeyDisable) const {
    if (!isObeyDisable && mMario->isInputDisable()) {
        return false;
    }

    if (mRemoteDemo) {
        return false;
    }

    switch (type) {
    case 2:
        if (mSubSpinTrigger || mMainSpinTrigger) {
            return true;
        }

        break;

    case 0:
        if (mSubSpinTrigger) {
            return true;
        }

        break;

    case 1:
        if (mMainSpinTrigger) {
            return true;
        }

        break;

    case 3:
        if (MR::getPlayerPressedJump()) {
            return true;
        }

        break;

    case 4:
        if (MR::getPlayerPressedFire()) {
            return true;
        }

        break;

    case 7:
        if (MR::getPlayerPressedCrouch()) {
            return true;
        }

        break;

    case 8:
        if (MR::getPlayerPressedCamera()) {
            return true;
        }

        break;

    case 5:
        if (MR::getPlayerHeldJump()) {
            return true;
        }

        break;

    case 6:
        if (MR::getPlayerHeldFire()) {
            return true;
        }

        break;

    case 9:
        if (MR::getPlayerHeldCrouch()) {
            return true;
        }

        break;

    case 10:
        if (MR::getPlayerHeldCamera()) {
            return true;
        }

        break;
    }

    return false;
}

void MarioActor::getStickValue(f32* pStickX, f32* pStickY) {
    *pStickX = 0.0f;
    *pStickY = 0.0f;

    if (mMario->isInputDisable()) {
        return;
    }

    if (mMario->getDrawStates().stickLocked) {
        return;
    }

    if (pStickX != nullptr) {
        *pStickX = MR::getPlayerStickX();
    }

    if (pStickY != nullptr) {
        *pStickY = MR::getPlayerStickY();
    }
}

void MarioActor::updateActionTrigger() {
    mActionTrigger = false;

    if (checkButtonType(3, false) || mMainSpinTrigger != 0) {
        if (mActionHeld) {
            return;
        }

        mActionHeld = true;
        mActionTrigger = true;
    } else {
        mActionHeld = false;
    }

    if (mMario->checkLvlA()) {
    }
}

void MarioActor::updateControllerSwing() {
    if (checkButtonType(3, false) || checkButtonType(4, false)) {
        mMainSpinBlock = 10;
    }

    bool F20 = mMainSpinHeld;
    mMainSpinTrigger = false;

    const s32 channel = MR::getPlayerChannel();
    bool isMainSpin = channel >= 0 && (!mMario->isSwimming() ? MR::isMainSpin(channel) : MR::isMainSpinPressed(channel));
    mMainSpinHeld = isMainSpin;

    if (mMainSpinBlock != 0) {
        mMainSpinBlock--;
    } else if (isMainSpin && F20 == 0) {
        mMainSpinTrigger = true;
    }

    if (isMainSpin && mSpinIntervalTimer > 10 && mMainSpinBlock < 8) {
        mMainSpinBlock = 8;
    }
}

void MarioActor::updateControllerSwingLeft() {
    if (checkButtonType(7, false) || checkButtonType(8, false)) {
        mSubSpinBlock = 10;
    }

    bool F21 = mSubSpinHeld;
    mSubSpinTrigger = false;

    const s32 channel = MR::getPlayerChannel();
    bool isSubSpin = channel >= 0 && MR::isSubSpin(channel);
    mSubSpinHeld = isSubSpin;

    if (mSubSpinBlock != 0) {
        mSubSpinBlock--;
    } else if (isSubSpin && !F21) {
        mSubSpinTrigger = true;
    }

    if (isSubSpin && mSpinIntervalTimer > 10 && mSubSpinBlock < 8) {
        mSubSpinBlock = 8;
    }
}

void MarioActor::lockOnPointer() {
}

bool MarioActor::isRequestSpinJump2P() const {
    if (mMario->isInputDisable()) {
        return false;
    }

    if (mRemoteDemo) {
        return false;
    }

    if (!mSwingPermission) {
        return false;
    }

    if (mSpinIntervalTimer) {
        return false;
    }

    if (mSwingInhibitTimer) {
        return false;
    }

    if (!mMario->isEnableRush()) {
        return false;
    }

    if (mRushJustEnded) {
        return false;
    }

    if (mPlayerMode == 5) {
        return false;
    }

    if (mMario->getMovementStates().jumping && mMario->mJumpKind == 2 && mMario->mActionStates.jump2P) {
        return false;
    }

    if (isJumping() && mMario->mAirTimer > 6 && !mExtra && MR::isCoStarFree() && MR::isStarPointerPointing2P(this, "弱", true, false) && MR::isPressedJump(1)) {
        return true;
    }

    return false;
}
