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

namespace {
    s32 sPlayerChannel = PLAYER_1;  // MR::getPlayerChannel
    u32 sCoStarTaken = 0;  // extra players driven by the second controller
}  // namespace

namespace MR {
    s32 getPlayerChannel() {
        return sPlayerChannel;
    }

    PlayerChannelScope::PlayerChannelScope(s32 channel) : mBefore(sPlayerChannel) {
        sPlayerChannel = channel;
    }

    PlayerChannelScope::~PlayerChannelScope() {
        sPlayerChannel = mBefore;
    }

    bool isCoStarFree() {
        return sCoStarTaken == 0;
    }

    void takeCoStar(bool take) {
        if (take) {
            sCoStarTaken++;
        } else if (sCoStarTaken > 0) {
            sCoStarTaken--;
        }
    }

    void getPointerPosOnScreen(Vec2f* pPos, s32 channel) {
        MR::getPlayerInput(channel)->mPointer->getPointingPosBasedOnScreen(pPos);
    }

    void getPointerPos(Vec2f* pPos, s32 channel) {
        MR::getPlayerInput(channel)->mPointer->getPointingPos(pPos);
    }

    void getPointerPastPos(Vec2f* pPos, s32 idx, s32 channel) {
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

    void getMainTilt(Vec3f* pAccel, s32 channel) {
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
        return (MR::getPlayerInput(channel)->mStick->mTrigger & InputStick::Direction_Up) != 0;
    }

    bool isMoveStickPressedDown(s32 channel) {
        return (MR::getPlayerInput(channel)->mStick->mTrigger & InputStick::Direction_Down) != 0;
    }

    bool isMoveStickPressedLeft(s32 channel) {
        return (MR::getPlayerInput(channel)->mStick->mTrigger & InputStick::Direction_Left) != 0;
    }

    bool isMoveStickPressedRight(s32 channel) {
        return (MR::getPlayerInput(channel)->mStick->mTrigger & InputStick::Direction_Right) != 0;
    }

    bool isSubSpin(s32 channel) {
        return MR::getPlayerInput(channel)->mSubSwing->mIsSwing;
    }

    bool isSpin(s32 channel) {
        return isMainSpin(channel) || isSubSpin(channel);
    }

    bool testSystemPadTriggerDecide() {
        return isPressedJump(PLAYER_1);
    }

    bool testSystemTriggerA() {
        return isPressedJump(PLAYER_1);
    }

    bool testSystemTriggerB() {
        return isPressedFire(PLAYER_1);
    }

    bool testPointerMenuPadDecideTrigger() {
        return isPressedJump(PLAYER_1);
    }

    bool testFpViewStartTrigger() {
        return isPressedUp(PLAYER_1);
    }

    bool testFpViewOutTrigger() {
        return isPressedDown(PLAYER_1) || isPressedJump(PLAYER_1);
    }

    f32 getPlayerStickX() {
        if (sPlayerChannel < 0) {
            return 0.0f;
        }
        f32 stick = getMoveStickX(sPlayerChannel);
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
        if (sPlayerChannel < 0) {
            return 0.0f;
        }
        f32 stick = getMoveStickY(sPlayerChannel);
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
        return sPlayerChannel >= 0 && isPressedJump(sPlayerChannel);
    }

    bool getPlayerPressedFire() {
        return sPlayerChannel >= 0 && isPressedFire(sPlayerChannel);
    }

    bool getPlayerPressedCrouch() {
        return sPlayerChannel >= 0 && isPressedCrouch(sPlayerChannel);
    }

    bool getPlayerPressedCamera() {
        return sPlayerChannel >= 0 && isPressedCamera(sPlayerChannel);
    }

    bool getPlayerHeldJump() {
        return sPlayerChannel >= 0 && isHeldJump(sPlayerChannel);
    }

    bool getPlayerHeldFire() {
        return sPlayerChannel >= 0 && isHeldFire(sPlayerChannel);
    }

    bool getPlayerHeldCrouch() {
        return sPlayerChannel >= 0 && isHeldCrouch(sPlayerChannel);
    }

    bool getPlayerHeldCamera() {
        return sPlayerChannel >= 0 && isHeldCamera(sPlayerChannel);
    }

    bool isMoveStickOperated(s32 channel) {
        f32 x = getMoveStickX(channel);
        f32 y = getMoveStickY(channel);

        return MR::abs(x) + MR::abs(y) > 0.0f;
    }

    void calcWorldStickDirectionXZ(f32* pDirX, f32* pDirZ, s32 channel) {
        Pos3f mtx;
        mtx.set(getCameraInvViewMtx());
        Vec3f side;
        mtx.getXDir(side);
        side.y = 0.0f;
        normalizeOrZero(&side);
        Vec3f front;
        mtx.getZDir(front);
        front.y = 0.0f;
        normalizeOrZero(&front);
        front.scale(-1.0f);

        f32 x = getMoveStickX(channel);
        f32 y = getMoveStickY(channel);
        side.scale(x);
        front.scale(y);
        Vec3f direction = side + front;
        normalizeOrZero(&direction);
        *pDirX = direction.x;
        *pDirZ = direction.z;
    }

    void calcWorldStickDirectionXZ(Vec3f* pDir, s32 channel) {
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

