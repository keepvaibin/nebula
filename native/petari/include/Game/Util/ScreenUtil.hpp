#pragma once

#include <nebula_geometry.hpp>
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/SystemUtil.hpp"

struct ResTIMG;

namespace MR {
    s32 getScreenWidth();
    // The picture's shape as shown, width / height.
    f32 getScreenAspect();
    // The picture's shape: the host latches the output's shape at each retrace, and the screen width, the camera's
    // aspect and the layout canvas follow it (wide == 0: 16:9).
    inline const NebulaDisplay& getDisplay() {
        return NebulaFrameClock::display();
    }
    inline s32 getScreenHeight() {
        return NebulaFrameClock::cScreenLines;
    }
    // What a plate keeps from the screen's sides.
    s32 getScreenMargin();
    // A point of the screen as fractions of the frame.
    void convertScreenPosToFrame(Vec2f*, const Vec2f&);
    void startToCaptureScreen(const char*);
    void endToCaptureScreen(const char*);
    void captureScreenIfAllow(const char*);
    const ResTIMG* getScreenResTIMG();
    void closeWipeCircle(s32 frame = -1);
    void forceOpenWipeCircle();
    void forceCloseWipeCircle();
    void closeWipeFade(s32 frame = -1);
    void forceOpenWipeFade();
    void forceCloseWipeFade();
    void closeWipeWhiteFade(s32 frame = -1);
    void forceOpenWipeWhiteFade();
    void forceCloseWipeWhiteFade();
    bool isWipeActive();
    bool isWipeBlank();
    bool isWipeOpen();
    void closeSystemWipeCircle(s32 frame = -1);
    void openSystemWipeFade(s32 frame = -1);
    void closeSystemWipeFade(s32 frame = -1);
    void forceOpenSystemWipeFade();
    void openSystemWipeWhiteFade(s32 frame = -1);
    void closeSystemWipeWhiteFade(s32 frame = -1);
    void forceCloseSystemWipeWhiteFade();
    bool isSystemWipeActive();
    void closeSystemWipeCircleWithCaptureScreen(s32 frame = -1);
    void closeSystemWipeFadeWithCaptureScreen(s32 frame = -1);
    void setWipeCircleCenterPos(const Vec3f&);
    void requestMovementOnImageEffect();
    void setImageEffectControlAuto();
    void turnOffDOFInSubjective();
    void turnOnDOFInSubjective();
    void turnOnNormalBloom();
    void turnOnDepthOfField(bool);
    void turnOffImageEffect();
    void forceOffImageEffect();
    void createNormalBloom();
    bool isNormalBloomOn();
    bool isNormalBloomForEachModel();
    void setNormalBloomIntensity(u8);
    void setNormalBloomThreshold(u8);
    void setNormalBloomBlurIntensity1(u8);
    void setNormalBloomBlurIntensity2(u8);
    void createSimpleBloom();
    void createScreenBlur();
    void createDepthOfFieldBlur();
    void setDepthOfFieldBlurIntensity(f32);
    void createCenterScreenBlur();
    void startCenterScreenBlur(s32, f32, u8, s32, s32);
    void requestPowerUpHPMeter();
    void requestForceAppearHPMeter();
    void startMissLayout();
    bool isMissLayoutAnimEnd();
    void activateDefaultGameLayout();
    void deactivateDefaultGameLayout();
    bool isActiveDefaultGameLayout();
    void forceAppearDefaultGameLayout();
    void pauseOffLensFlare();
    void tryScreenToFrameCinemaFrame();
    void tryFrameToBlankCinemaFrame();
    void tryBlankToFrameCinemaFrame();
    void tryFrameToScreenCinemaFrame();
    void forceToScreenCinemaFrame();
    void forceToFrameCinemaFrame();
    void forceToBlankCinemaFrame();
    bool isStopCinemaFrame();
    void appearInformationMessage(const char*, bool);
    void appearInformationMessageCenter(const char*, bool);
    void appearInformationMessage(const char16_t*, bool);
    void setInformationMessageReplaceString(const char16_t*, s32);
    void disappearInformationMessage();
    bool isDeadInformationMessage();
    void appearYesNoSelector(const char*, const char*, const char*);
    bool isYesNoSelected();
    bool isYesNoSelectedYes();
    void setYesNoSelectorSE(const char*, const char*, const char*);
    void resetYesNoSelectorSE();
    void startMoviePlayer(const char*);
    void stopMoviePlayer();
    bool isActiveMoviePlayer();
    bool isMoviePlayerPlaying();
    s32 getMovieCurrentFrame();
    u32 getMovieTotalFrame();
    void setMovieVolume(f32, s32);
    void appearStarCounter();
    void disappearStarCounter();
    void forceSyncStarPieceCounter();
    void forceAppearStarPieceCounterForTicoFat();
    void disappearStarPieceCounterForTicoFat();
    void createPurpleCoinCounter();
    void validatePurpleCoinCounter();
    void requestCounterLayoutAppearanceForTicoEat(bool);
    void openWipeCircle(s32 frame = -1);
    void openWipeFade(s32 frame = -1);
    void openWipeWhiteFade(s32 frame = -1);
    void startGameOverWipe();
    void startDownWipe();
    void requestOneUp();
};  // namespace MR
