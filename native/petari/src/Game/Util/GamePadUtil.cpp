#include "Game/Util/GamePadUtil.hpp"
#include "Game/System/GamePad.hpp"
#include "Game/System/GamePadAcceleration.hpp"
#include "Game/System/GamePadButton.hpp"
#include "Game/System/GamePadHVSwing.hpp"
#include "Game/System/GamePadHolder.hpp"
#include "Game/System/GamePadPointer.hpp"
#include "Game/System/GamePadStick.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/VectorUtil.hpp"

void GamePadUtil_FORCE_MATCH_SDATA2() {
    (void)-2607.59448f;
    (void)2607.59448f;
}

namespace MR {
    void getCorePadPointingPosBasedOnScreen(TVec2f* pPos, s32 channel) {
        MR::getGamePad(channel)->mPointer->getPointingPosBasedOnScreen(pPos);
    }

    void getCorePadPointingPos(TVec2f* pPos, s32 channel) {
        MR::getGamePad(channel)->mPointer->getPointingPos(pPos);
    }

    void getCorePadPastPointingPos(TVec2f* pPos, s32 idx, s32 channel) {
        MR::getGamePad(channel)->mPointer->getPastPointingPos(pPos, idx);
    }

    s32 getCorePadEnablePastCount(s32 channel) {
        return MR::getGamePad(channel)->mPointer->getEnablePastCount();
    }

    bool isCorePadPointInScreen(s32 channel) {
        return MR::getGamePad(channel)->mPointer->mIsPointInScreen;
    }

    f32 getCorePadDistanceToDisplay(s32 channel) {
        return MR::getGamePad(channel)->mPointer->mDistDisplay;
    }

    void getCorePadAcceleration(TVec3f* pAccel, s32 channel) {
        MR::getGamePad(channel)->mCorePadAccel->getAcceleration(pAccel);
    }

    bool testCorePadButtonUp(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonUp();
    }

    bool testCorePadButtonDown(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonDown();
    }

    bool testCorePadButtonLeft(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonLeft();
    }

    bool testCorePadButtonRight(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonRight();
    }

    bool testCorePadButtonA(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonA();
    }

    bool testCorePadButtonB(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonB();
    }

    bool testCorePadButton1(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButton1();
    }

    bool testCorePadButton2(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButton2();
    }

    bool testCorePadButtonPlus(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonPlus();
    }

    bool testCorePadButtonMinus(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonMinus();
    }

    bool testSubPadButtonC(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonC();
    }

    bool testSubPadButtonZ(s32 channel) {
        return MR::getGamePad(channel)->mButton->testButtonZ();
    }

    bool testPadButtonAnyWithoutHome(s32 channel) {
        return testCorePadButtonUp(channel) || testCorePadButtonDown(channel) || testCorePadButtonLeft(channel) || testCorePadButtonRight(channel) ||
               testCorePadButtonA(channel) || testCorePadButtonB(channel) || testCorePadButton1(channel) || testCorePadButton2(channel) ||
               testCorePadButtonPlus(channel) || testCorePadButtonMinus(channel) || testSubPadButtonC(channel) || testSubPadButtonZ(channel);
    }

    bool testCorePadTriggerUp(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerUp();
    }

    bool testCorePadTriggerDown(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerDown();
    }

    bool testCorePadTriggerLeft(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerLeft();
    }

    bool testCorePadTriggerRight(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerRight();
    }

    bool testCorePadTriggerA(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerA();
    }

    bool testCorePadTriggerB(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerB();
    }

    bool testCorePadTrigger1(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTrigger1();
    }

    bool testCorePadTrigger2(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTrigger2();
    }

    bool testCorePadTriggerPlus(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerPlus();
    }

    bool testCorePadTriggerMinus(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerMinus();
    }

    bool testCorePadTriggerAnyWithoutHome(s32 channel) {
        return testCorePadTriggerUp(channel) || testCorePadTriggerDown(channel) || testCorePadTriggerLeft(channel) ||
               testCorePadTriggerRight(channel) || testCorePadTriggerA(channel) || testCorePadTriggerB(channel) || testCorePadTrigger1(channel) ||
               testCorePadTrigger2(channel) || testCorePadTriggerPlus(channel) || testCorePadTriggerMinus(channel);
    }

    bool testCorePadTriggerHome(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerHome();
    }

    bool testSubPadTriggerC(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerC();
    }

    bool testSubPadTriggerZ(s32 channel) {
        return MR::getGamePad(channel)->mButton->testTriggerZ();
    }

    bool testSubPadReleaseZ(s32 channel) {
        return MR::getGamePad(channel)->mButton->testReleaseZ();
    }

    bool isCorePadSwing(s32 channel) {
        return MR::getGamePad(channel)->mCorePadSwing->mIsSwing;
    }

    bool isCorePadSwingTrigger(s32 channel) {
        return MR::getGamePad(channel)->mCorePadSwing->mIsTriggerSwing;
    }

    f32 getSubPadStickX(s32 channel) {
        return MR::getGamePad(channel)->mStick->mStick.x;
    }

    f32 getSubPadStickY(s32 channel) {
        return MR::getGamePad(channel)->mStick->mStick.y;
    }

    bool testSubPadStickTriggerUp(s32 channel) {
        return (MR::getGamePad(channel)->mStick->mTrigger & 1) != 0;
    }

    bool testSubPadStickTriggerDown(s32 channel) {
        return (MR::getGamePad(channel)->mStick->mTrigger & 2) != 0;
    }

    bool testSubPadStickTriggerLeft(s32 channel) {
        return (MR::getGamePad(channel)->mStick->mTrigger & 8) != 0;
    }

    bool testSubPadStickTriggerRight(s32 channel) {
        return (MR::getGamePad(channel)->mStick->mTrigger & 4) != 0;
    }

    void getSubPadAcceleration(TVec3f* pAccel, s32 channel) {
        MR::getGamePad(channel)->mSubPadAccel->getAcceleration(pAccel);
    }

    bool isSubPadSwing(s32 channel) {
        return MR::getGamePad(channel)->mSubPadSwing->mIsSwing;
    }

    bool isPadSwing(s32 channel) {
        return isCorePadSwing(channel) || isSubPadSwing(channel);
    }

    bool testSystemPadTriggerDecide() {
        return testCorePadTriggerA(GAMEPAD_CHAN0) != false;
    }

    bool testSystemTriggerA() {
        return testCorePadTriggerA(GAMEPAD_CHAN0) != false;
    }

    bool testSystemTriggerB() {
        return testCorePadTriggerB(GAMEPAD_CHAN0) != false;
    }

    bool testDPDMenuPadDecideTrigger() {
        return testCorePadTriggerA(GAMEPAD_CHAN0);
    }

    bool testFpViewStartTrigger() {
        return testCorePadTriggerUp(GAMEPAD_CHAN0);
    }

    bool testFpViewOutTrigger() {
        return testCorePadTriggerDown(GAMEPAD_CHAN0) || testCorePadTriggerA(GAMEPAD_CHAN0);
    }

    f32 getPlayerStickX() {
        f32 stick = getSubPadStickX(GAMEPAD_CHAN0);
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
        f32 stick = getSubPadStickY(GAMEPAD_CHAN0);
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

    bool getPlayerTriggerA() {
        return testCorePadTriggerA(GAMEPAD_CHAN0);
    }

    bool getPlayerTriggerB() {
        return testCorePadTriggerB(GAMEPAD_CHAN0);
    }

    bool getPlayerTriggerZ() {
        return testSubPadTriggerZ(GAMEPAD_CHAN0);
    }

    bool getPlayerTriggerC() {
        return testSubPadTriggerC(GAMEPAD_CHAN0);
    }

    bool getPlayerLevelA() {
        return testCorePadButtonA(GAMEPAD_CHAN0);
    }

    bool getPlayerLevelB() {
        return testCorePadButtonB(GAMEPAD_CHAN0);
    }

    bool getPlayerLevelZ() {
        return testSubPadButtonZ(GAMEPAD_CHAN0);
    }

    bool getPlayerLevelC() {
        return testSubPadButtonC(GAMEPAD_CHAN0);
    }

    bool isGamePadStickOperated(s32 channel) {
        f32 x = getSubPadStickX(channel);
        f32 y = getSubPadStickY(channel);

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

        f32 x = getSubPadStickX(channel);
        f32 y = getSubPadStickY(channel);
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

    u32 getGamePadMaxCount() {
        return 2;
    }

    bool isConnectedGamePad(s32 channel) {
        return MR::getGamePad(channel)->mIsConnected;
    }

    bool isOperatingGamePad(s32 channel) {
        GamePad* pGamePad = MR::getGamePad(channel);

        if (!pGamePad->mCorePadAccel->isBalanced()) {
            return true;
        }

        if (pGamePad->mButton->testButtonA()) {
            return true;
        }

        return pGamePad->mButton->testButtonB();
    }
};  // namespace MR

namespace GamePadFunction {
    GamePadRumble* getGamePadRumble(s32 channel) {
        return MR::getGamePad(channel)->getRumbleInstance();
    }
};  // namespace GamePadFunction

void GamePadUtil_FORCE_MATCH(const TVec3f& rFirst, const TVec3f& rSecond, TVec3f* pOut) {
    TVec3f value(rFirst);
    value.add(rSecond);
    *pOut = value;
}
