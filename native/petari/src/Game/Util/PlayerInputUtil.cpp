#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/System/PlayerInput.hpp"
#include "Game/System/InputTilt.hpp"
#include "Game/System/InputButtons.hpp"
#include "Game/System/InputSwing.hpp"
#include "Game/System/PlayerInputs.hpp"
#include "Game/System/InputPointer.hpp"
#include "Game/System/InputStick.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/VectorUtil.hpp"

namespace MR {
    void getPointerPosOnScreen(TVec2f* pPos, s32 channel) {
        MR::getPlayerInput(channel)->mPointer->getPointingPosBasedOnScreen(pPos);
    }

    void getPointerPos(TVec2f* pPos, s32 channel) {
        MR::getPlayerInput(channel)->mPointer->getPointingPos(pPos);
    }

    void getPointerPastPos(TVec2f* pPos, s32 idx, s32 channel) {
        MR::getPlayerInput(channel)->mPointer->getPastPointingPos(pPos, idx);
    }

    s32 getPointerPastCount(s32 channel) {
        return MR::getPlayerInput(channel)->mPointer->getEnablePastCount();
    }

    bool isPointerInScreen(s32 channel) {
        return MR::getPlayerInput(channel)->mPointer->mIsPointInScreen;
    }

    f32 getPointerDistance(s32 channel) {
        return MR::getPlayerInput(channel)->mPointer->mDistDisplay;
    }

    void getMainTilt(TVec3f* pAccel, s32 channel) {
        MR::getPlayerInput(channel)->mMainTilt->getAcceleration(pAccel);
    }

    bool isHeldUp(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonUp();
    }

    bool isHeldDown(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonDown();
    }

    bool isHeldLeft(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonLeft();
    }

    bool isHeldRight(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonRight();
    }

    bool isHeldJump(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonA();
    }

    bool isHeldFire(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonB();
    }

    bool testCorePadButton1(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButton1();
    }

    bool testCorePadButton2(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButton2();
    }

    bool isHeldPause(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonPlus();
    }

    bool isHeldMap(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonMinus();
    }

    bool isHeldCamera(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonC();
    }

    bool isHeldCrouch(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testButtonZ();
    }

    bool isHeldAnyWithoutHome(s32 channel) {
        return isHeldUp(channel) || isHeldDown(channel) || isHeldLeft(channel) || isHeldRight(channel) ||
               isHeldJump(channel) || isHeldFire(channel) || testCorePadButton1(channel) || testCorePadButton2(channel) ||
               isHeldPause(channel) || isHeldMap(channel) || isHeldCamera(channel) || isHeldCrouch(channel);
    }

    bool isPressedUp(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerUp();
    }

    bool isPressedDown(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerDown();
    }

    bool isPressedLeft(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerLeft();
    }

    bool isPressedRight(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerRight();
    }

    bool isPressedJump(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerA();
    }

    bool isPressedFire(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerB();
    }

    bool testCorePadTrigger1(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTrigger1();
    }

    bool testCorePadTrigger2(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTrigger2();
    }

    bool isPressedPause(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerPlus();
    }

    bool isPressedMap(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerMinus();
    }

    bool isPressedAnyWithoutHome(s32 channel) {
        return isPressedUp(channel) || isPressedDown(channel) || isPressedLeft(channel) ||
               isPressedRight(channel) || isPressedJump(channel) || isPressedFire(channel) || testCorePadTrigger1(channel) ||
               testCorePadTrigger2(channel) || isPressedPause(channel) || isPressedMap(channel);
    }

    bool isPressedHome(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerHome();
    }

    bool isPressedCamera(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerC();
    }

    bool isPressedCrouch(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testTriggerZ();
    }

    bool isReleasedCrouch(s32 channel) {
        return MR::getPlayerInput(channel)->mButton->testReleaseZ();
    }

    bool isMainSpin(s32 channel) {
        return MR::getPlayerInput(channel)->mMainSwing->mIsSwing;
    }

    bool isMainSpinPressed(s32 channel) {
        return MR::getPlayerInput(channel)->mMainSwing->mIsTriggerSwing;
    }

    f32 getMoveStickX(s32 channel) {
        return MR::getPlayerInput(channel)->mStick->mStick.x;
    }

    f32 getMoveStickY(s32 channel) {
        return MR::getPlayerInput(channel)->mStick->mStick.y;
    }

    bool isMoveStickPressedUp(s32 channel) {
        return (MR::getPlayerInput(channel)->mStick->mTrigger & 1) != 0;
    }

    bool isMoveStickPressedDown(s32 channel) {
        return (MR::getPlayerInput(channel)->mStick->mTrigger & 2) != 0;
    }

    bool isMoveStickPressedLeft(s32 channel) {
        return (MR::getPlayerInput(channel)->mStick->mTrigger & 8) != 0;
    }

    bool isMoveStickPressedRight(s32 channel) {
        return (MR::getPlayerInput(channel)->mStick->mTrigger & 4) != 0;
    }

    void getSubTilt(TVec3f* pAccel, s32 channel) {
        MR::getPlayerInput(channel)->mSubTilt->getAcceleration(pAccel);
    }

    bool isSubSpin(s32 channel) {
        return MR::getPlayerInput(channel)->mSubSwing->mIsSwing;
    }

    bool isSpin(s32 channel) {
        return isMainSpin(channel) || isSubSpin(channel);
    }

    bool testSystemPadTriggerDecide() {
        return isPressedJump(PLAYER_1) != false;
    }

    bool testSystemTriggerA() {
        return isPressedJump(PLAYER_1) != false;
    }

    bool testSystemTriggerB() {
        return isPressedFire(PLAYER_1) != false;
    }

    bool testDPDMenuPadDecideTrigger() {
        return isPressedJump(PLAYER_1);
    }

    bool testFpViewStartTrigger() {
        return isPressedUp(PLAYER_1);
    }

    bool testFpViewOutTrigger() {
        return isPressedDown(PLAYER_1) || isPressedJump(PLAYER_1);
    }

    f32 getPlayerStickX() {
        f32 stick = getMoveStickX(PLAYER_1);
        if (stick != 0.0f) {
            return stick;
        }

        f32 x = 0.0f;
        f32 y = 0.0f;
        if (x != 0.0f) {
            x *= 1.0f + MR::abs(MR::sin(MR::atan2(x, y)));
        }

        return x;
    }

    f32 getPlayerStickY() {
        f32 stick = getMoveStickY(PLAYER_1);
        if (stick != 0.0f) {
            return stick;
        }

        f32 x = 0.0f;
        f32 y = 0.0f;
        if (x != 0.0f) {
            y *= 1.0f + MR::abs(MR::cos(MR::atan2(x, y)));
        }

        return y;
    }

    bool getPlayerPressedJump() {
        return isPressedJump(PLAYER_1);
    }

    bool getPlayerPressedFire() {
        return isPressedFire(PLAYER_1);
    }

    bool getPlayerPressedCrouch() {
        return isPressedCrouch(PLAYER_1);
    }

    bool getPlayerPressedCamera() {
        return isPressedCamera(PLAYER_1);
    }

    bool getPlayerHeldJump() {
        return isHeldJump(PLAYER_1);
    }

    bool getPlayerHeldFire() {
        return isHeldFire(PLAYER_1);
    }

    bool getPlayerHeldCrouch() {
        return isHeldCrouch(PLAYER_1);
    }

    bool getPlayerHeldCamera() {
        return isHeldCamera(PLAYER_1);
    }

    bool isMoveStickOperated(s32 channel) {
        f32 x = getMoveStickX(channel);
        f32 y = getMoveStickY(channel);

        return MR::abs(x) + MR::abs(y) > 0.0f;
    }

    void calcWorldStickDirectionXZ(f32* pDirX, f32* pDirZ, s32 channel) {
        TPos3f mtx;
        mtx.set(getCameraInvViewMtx());
        TVec3f side;
        mtx.getXDir(side);
        side.y = 0.0f;
        normalizeOrZero(&side);
        TVec3f front;
        mtx.getZDir(front);
        front.y = 0.0f;
        normalizeOrZero(&front);
        front.scale(-1.0f);

        f32 x = getMoveStickX(channel);
        f32 y = getMoveStickY(channel);
        side.scale(x);
        front.scale(y);
        TVec3f direction = side + front;
        normalizeOrZero(&direction);
        *pDirX = direction.x;
        *pDirZ = direction.z;
    }

    void calcWorldStickDirectionXZ(TVec3f* pDir, s32 channel) {
        pDir->y = 0.0f;

        calcWorldStickDirectionXZ(&pDir->x, &pDir->z, channel);
    }

    u32 getPlayerMaxCount() {
        return 2;
    }

    bool isConnectedInput(s32 channel) {
        return MR::getPlayerInput(channel)->mIsConnected;
    }

};  // namespace MR

namespace PlayerInputFunction {
    InputRumble* getInputRumble(s32 channel) {
        return MR::getPlayerInput(channel)->getRumbleInstance();
    }
};  // namespace PlayerInputFunction

