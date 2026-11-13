#pragma once

#include <nebula_geometry.hpp>
#include "Game/System/NebulaInput.hpp"

class InputRumble;

namespace MR {
    void getPointerPosOnScreen(Vec2f* pPos, s32 channel);
    void getPointerPos(Vec2f* pPos, s32 channel);
    void getPointerPastPos(Vec2f* pPos, s32 idx, s32 channel);
    s32 getPointerPastCount(s32 channel);

    bool isPointerInScreen(s32 channel);

    f32 getPointerDistance(s32 channel);

    void getMainTilt(Vec3f* pAccel, s32 channel);

    bool isHeldUp(s32 channel);

    bool isHeldDown(s32 channel);

    bool isHeldLeft(s32 channel);

    bool isHeldRight(s32 channel);

    bool isHeldJump(s32 channel);

    bool isHeldFire(s32 channel);

    bool isHeldPause(s32 channel);

    bool isHeldMap(s32 channel);

    bool isHeldCamera(s32 channel);

    bool isHeldCrouch(s32 channel);

    bool isHeldAnyWithoutHome(s32 channel);

    bool isPressedUp(s32 channel);

    bool isPressedDown(s32 channel);

    bool isPressedLeft(s32 channel);

    bool isPressedRight(s32 channel);

    bool isPressedJump(s32 channel);

    bool isPressedFire(s32 channel);

    bool isPressedPause(s32 channel);

    bool isPressedMap(s32 channel);

    bool isPressedHome(s32 channel);

    bool isPressedCamera(s32 channel);

    bool isPressedCrouch(s32 channel);

    bool isReleasedCrouch(s32 channel);

    bool isMainSpin(s32 channel);

    bool isMainSpinPressed(s32 channel);

    f32 getMoveStickX(s32 channel);

    f32 getMoveStickY(s32 channel);

    bool isMoveStickPressedUp(s32 channel);

    bool isMoveStickPressedDown(s32 channel);

    bool isMoveStickPressedLeft(s32 channel);

    bool isMoveStickPressedRight(s32 channel);

    bool isSubSpin(s32 channel);

    bool isSpin(s32 channel);

    bool testSystemPadTriggerDecide();

    bool testSystemTriggerA();

    bool testSystemTriggerB();

    bool testPointerMenuPadDecideTrigger();

    bool testFpViewStartTrigger();

    bool testFpViewOutTrigger();

    // The controller the getPlayer* reads below take: PLAYER_1, or an extra player's while it moves (-1: none, the
    // reads give nothing pressed).
    s32 getPlayerChannel();
    // getPlayerChannel is `channel` for the scope (an extra player's control), then what it was.
    class PlayerChannelScope {
    public:
        explicit PlayerChannelScope(s32 channel);
        ~PlayerChannelScope();

    private:
        s32 mBefore;
    };
    // The co-star's reads (the second controller's pointer jump and spin) are the scene's player's: no extra player
    // is driven by that controller.
    bool isCoStarFree();
    // An extra player takes (true) or gives back (false) the second controller; counted.
    void takeCoStar(bool take);

    f32 getPlayerStickX();

    f32 getPlayerStickY();

    bool getPlayerPressedJump();

    bool getPlayerPressedFire();

    bool getPlayerPressedCrouch();

    bool getPlayerPressedCamera();

    bool getPlayerHeldJump();

    bool getPlayerHeldFire();

    bool getPlayerHeldCrouch();

    bool getPlayerHeldCamera();

    bool isMoveStickOperated(s32 channel);

    void calcWorldStickDirectionXZ(f32* pDirX, f32* pDirZ, s32 channel);
    void calcWorldStickDirectionXZ(Vec3f* pDir, s32 channel);

    u32 getPlayerMaxCount();

    bool isConnectedInput(s32 channel);

};  // namespace MR

namespace PlayerInputFunction {
    InputRumble* getInputRumble(s32 channel);
};  // namespace PlayerInputFunction
