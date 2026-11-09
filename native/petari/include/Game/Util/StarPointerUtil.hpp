#pragma once

#include <nebula_geometry.hpp>
#include <NebulaGpu.h>
#include <nebula_math.h>

class LayoutActor;
class LiveActor;
class StarPointerOnOffController;

namespace Ui {
    class Pane;
}  // namespace Ui

namespace MR {
    void initStarPointerGameScene();
    void createStarPointerLayout();
    void onStarPointerSceneOut();
    void setStarPointerModeBase();
    void setStarPointerCameraMtxAtGameScene();
    void initStarPointerTarget(LiveActor*, f32, const Vec3f& rOffset = Vec3f(0.0f));
    void initStarPointerTargetAtPos(LiveActor*, f32, const Vec3f*, const Vec3f&);
    void initStarPointerTargetAtMtx(LiveActor*, f32, MtxPtr, const Vec3f&);
    void initStarPointerTargetAtJoint(LiveActor*, const char*, f32, const Vec3f&);
    void addStarPointerTargetCircle(LayoutActor*, const char*, f32, const Vec2f&, const char*);
    bool isStarPointerValid(s32);
    bool isStarPointerPointing(const LiveActor*, s32, bool, const char*);
    bool tryStarPointerCheck(const LiveActor*, bool, const char*);
    bool tryStarPointerCheckWithoutRumble(LiveActor*, bool);
    bool isStarPointerPointingPane(const LayoutActor*, const char*, s32, bool, const char*);
    // The same for a pane already found; pName (the pane's name) keys the pointer's reaction.
    bool isStarPointerPointingPane(const LayoutActor*, const Ui::Pane* pPane, const char* pName, s32, bool, const char*);
    bool isStarPointerPointingPaneForMeterLayout(const LayoutActor*, const char*, s32, bool, const char*);
    bool isStarPointerPointingTarget(const LayoutActor*, const char*, s32, bool, const char*);
    bool isStarPointerPointing1P(const LiveActor*, const char*, bool, bool);
    bool isStarPointerPointing1PWithoutCheckZ(const LiveActor*, const char*, bool, bool);
    bool isStarPointerPointing2P(const LiveActor*, const char*, bool, bool);
    bool isStarPointerPointing2POnPressButton(const LiveActor*, const char*, bool, bool);
    bool isStarPointerPointing2POnTriggerButton(const LiveActor*, const char*, bool, bool);
    bool isStarPointerPointingFileSelect(const LiveActor*);
    bool isStarPointerPointing1Por2P(const LiveActor*, const char*, bool, bool);
    bool requestTicoSeedGuidanceForce();
    bool requestPointerGuidanceNoInformation();
    bool isExistStarPointerGuidance();
    bool isExistStarPointerGuidanceFrame1P();
    void activeStarPointerGuidance();
    void deactiveStarPointerGuidance();
    // Whether the guidance may show. The system draws it over every layout, so Nebula's own windows turn it off while
    // they are up (as the game does before its own) and restore this after.
    bool isActiveStarPointerGuidance();
    void tryShowTimeoutedStarPointerGuidance();
    bool isExistStarPointerTarget(const LiveActor*);
    void setStarPointerTargetRadius3d(LiveActor*, f32);
    bool isStarPointerPointing(const Vec3f&, f32, s32, bool);
    MtxPtr getStarPointerViewMtx();
    Mtx44Ptr getStarPointerProjMtx();
    Vec2f* getStarPointerScreenPosition(s32);
    Vec2f getStarPointerScreenPositionOrEdge(s32);
    f32 getStarPointerScreenSpeed(s32);
    bool isStarPointerInScreen(s32);
    bool isStarPointerInScreenAnyPort(s32*);
    s32* getStarPointerLastPointedPort(const LiveActor*);
    Vec2f* getStarPointerScreenVelocity(s32);
    void getStarPointerWorldVelocityDirection(Vec3f*, s32);
    Vec3f* getStarPointerWorldPosUsingDepth(s32);
    void calcStarPointerWorldPointingPos(Vec3f*, const Vec3f&, s32);
    void calcStarPointerWorldPointingPosInsideEdge(Vec3f*, const Vec3f&, s32);
    bool calcStarPointerPosOnPlane(Vec3f*, const Vec3f&, const Vec3f&, s32, bool);
    bool calcStarPointerWorldVelocityDirectionOnPlane(Vec3f*, const Vec3f&, const Vec3f&, s32);
    f32 calcPointRadius2D(const Vec3f&, f32);
    bool calcStarPointerStrokeRotateMoment(Vec3f*, const Vec3f&, f32, s32);
    bool calcStarPointerScreenDistanceToTarget(const LiveActor*, f32*, s32);
    bool tryStartStarPointerCommandStream(const LiveActor*, const Vec3f*, s32, bool);
    bool tryEndStarPointerCommandStream(const LiveActor*, s32);
    bool isStarPointerCommandStream(const LiveActor*, s32);
    void startStarPointerModeTitle(void*);
    void startStarPointerModeFileSelect(void*);
    void startStarPointerModeGame(void*);
    void startStarPointerModeDemo(void*);
    void startStarPointerModeDemoWithStarPointer(void*);
    void startStarPointerModeDemoWithHandPointerFinger(void*);
    void startStarPointerModeDemoMarioDeath(void*);
    void startStarPointerModeMarioLauncher(void*);
    void startStarPointerModeChooseYesNo(void*);
    void startStarPointerModePauseMenu(void*);
    void startStarPointerModeScenarioSelectScene(void*);
    void startStarPointerModeBlueStar(void*);
    void startStarPointerModePowerStarGetDemo(void*);
    void startStarPointerModeStarPieceTarget(void*);
    void startStarPointerModeSphereSelectorFinger(void*);
    void startStarPointerModeSphereSelectorOnReaction(void*);
    void startStarPointerModeEnding(void*);
    void startStarPointerModeCommandStream(void*);
    void startStarPointerMode1PInvalid2PValid(void*);
    void endStarPointerMode(void*);
    void requestStarPointerModeErrorWindow(void*);
    void requestStarPointerModeSaveLoad(void*);
    // The pointer's mode as requested this frame, while the scene and its sequences hold still (Nebula's cutscene
    // skip question): GameSequenceProgress updates it otherwise.
    void updateStarPointerModeWhileSceneHeld();
    void requestStarPointerModePictureBook(void*);
    void requestStarPointerModePauseMenu(void*);
    void requestStarPointerModeBlueStarReady(void*);
    // Each frame `pActor` can be grabbed: a finger landing on it grabs it (NEBULA_CONTEXT_TOUCH_TARGET).
    void reportStarPointerGrabTarget(const LiveActor* pActor);
    void requestStarPointerModeBigBubble(void*, const Vec3f&);
    bool isStarPointerModeBlueStarReady();
    bool isStarPointerModeStarPieceTarget();
    bool isStarPointerModeMarioLauncher();
    bool isStarPointerModeHomeButton();
    bool isStarPointerModeErrorWindow();
    void enableStarPointerShootStarPiece();
    void disableStarPointerShootStarPiece();
    bool isEnableStarPointerShootStarPiece(s32);
    bool isStarPointer2PTransparencyMode();
    bool isStarPointer1PInvalid2PValidMode();
    void setStarPointerDrawSyncToken();
    bool requestBlueStarGuidance();
    bool requestTicoSeedGuidance(s32);
    bool requestTicoSeedGuidance();
    bool requestBigBubbleGuidance();
    bool requestMarioLauncherGuidance();
    bool requestFileSelectGuidance();
    bool requestFileSelectCopyGuidance();
    bool requestStarPieceLectureGuidance();

    /*
    inline void initStarPointerTarget(LiveActor* pActor, f32 radius) {
        Vec3f zeroOffset;
        zeroOffset.x = 0.0f;
        zeroOffset.y = 0.0f;
        zeroOffset.z = 0.0f;
        initStarPointerTarget(pActor, radius, zeroOffset);
    }
    */
};  // namespace MR
