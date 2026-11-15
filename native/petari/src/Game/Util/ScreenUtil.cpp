#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/BloomEffect.hpp"
#include "Game/Screen/BloomEffectSimple.hpp"
#include "Game/Screen/CaptureScreenDirector.hpp"
#include "Game/Screen/CenterScreenBlur.hpp"
#include "Game/Screen/CinemaFrame.hpp"
#include "Game/Screen/CounterLayoutController.hpp"
#include "Game/Screen/DepthOfFieldBlur.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Screen/ImageEffectDirector.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Screen/InformationMessage.hpp"
#include "Game/Screen/LensFlare.hpp"
#include "Game/Screen/MoviePlayerSimple.hpp"
#include "Game/Screen/PurpleCoinCounter.hpp"
#include "Game/Screen/SceneWipeHolder.hpp"
#include "Game/Screen/ScreenBlurEffect.hpp"
#include "Game/Screen/StarCounter.hpp"
#include "Game/Screen/StarPieceCounter.hpp"
#include "Game/Screen/SystemWipeHolder.hpp"
#include "Game/Screen/WipeLayoutBase.hpp"
#include "Game/Screen/YesNoController.hpp"
#include "Game/Screen/YesNoLayout.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemObjHolder.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SystemUtil.hpp"

namespace {
    char sWipeCircle[] = "円ワイプ";
    char sWipeFade[] = "フェードワイプ";
    char sWipeWhiteFade[] = "白フェードワイプ";
    char sWipeGameOver[] = "ゲームオーバー";
    char sWipeKoopa[] = "クッパ";
    char sSoundOneUp[] = "SE_SY_1UP";

    CaptureScreenDirector* getCaptureScreenDirector() {
        return SingletonHolder< GameSystem >::get()->mObjHolder->mCaptureScreenDirector;
    }

    void appearInformationMessage(bool hasButtonLayout, bool isCenter) {
        if (isCenter) {
            MR::getGameSceneLayoutHolder()->mInformationMessage->setCenter(true);
        } else {
            MR::getGameSceneLayoutHolder()->mInformationMessage->setCenter(false);
        }

        if (hasButtonLayout) {
            MR::getGameSceneLayoutHolder()->mInformationMessage->appearWithButtonLayout();
        } else {
            MR::getGameSceneLayoutHolder()->mInformationMessage->appear();
        }
    }

    StarCounter* getStarCounter() {
        return MR::getGameSceneLayoutHolder()->mCounterLayoutCtrl->mStarCounter;
    }

    StarPieceCounter* getStarPieceCounter() {
        return MR::getGameSceneLayoutHolder()->mCounterLayoutCtrl->mStarPieceCounter;
    }

    SystemWipeHolder* getSystemWipeHolder() {
        return SingletonHolder< GameSystem >::get()->mSystemWipeHolder;
    }

    BloomEffect* getNormalBloom() {
        return MR::getSceneObj< BloomEffect >(SceneObj_BloomEffect);
    }

    CinemaFrame* getCinemaFrame() {
        return MR::getSceneObj< CinemaFrame >(SceneObj_CinemaFrame);
    }

    MoviePlayerSimple* getMoviePlayer() {
        return MR::getSceneObj< MoviePlayerSimple >(SceneObj_MoviePlayerSimple);
    }
};  // namespace

namespace MR {
    s32 getScreenWidth() {
        if (isScreen16Per9() && getDisplay().wide) {
            return getDisplay().screen_width;
        }
        return isScreen16Per9() ? NebulaFrameClock::cWideScreenWidth : static_cast< s32 >(NebulaFrameClock::cLayoutWidth);
    }

    f32 getScreenAspect() {
        if (isScreen16Per9()) {
            return getDisplay().wide ? getDisplay().content_aspect : 16.0f / 9.0f;
        }
        return static_cast< f32 >(getScreenWidth()) / getScreenHeight();
    }

    s32 getScreenMargin() {
        return getScreenWidth() * 0.045f;
    }

    void convertScreenPosToFrame(Vec2f* pFrame, const Vec2f& rScreenPos) {
        pFrame->set(rScreenPos.x / getScreenWidth(), rScreenPos.y / getScreenHeight());
    }

    void startToCaptureScreen(const char* pParam1) {
        ::getCaptureScreenDirector()->requestCaptureTiming(pParam1);
    }

    void endToCaptureScreen(const char* pParam1) {
        ::getCaptureScreenDirector()->invalidateCaptureTiming(pParam1);
    }

    void captureScreenIfAllow(const char* pParam1) {
        ::getCaptureScreenDirector()->captureIfAllow(pParam1);
    }

    const ResTIMG* getScreenResTIMG() {
        return ::getCaptureScreenDirector()->getResTIMG();
    }

    void closeWipeCircle(s32 frame) {
        SceneWipeHolderFunction::closeWipe(::sWipeCircle, frame);
    }

    void forceOpenWipeCircle() {
        SceneWipeHolderFunction::forceOpenWipe(::sWipeCircle);
    }

    void forceCloseWipeCircle() {
        SceneWipeHolderFunction::forceCloseWipe(::sWipeCircle);
    }

    void closeWipeFade(s32 frame) {
        SceneWipeHolderFunction::closeWipe(::sWipeFade, frame);
    }

    void forceOpenWipeFade() {
        SceneWipeHolderFunction::forceOpenWipe(::sWipeFade);
    }

    void forceCloseWipeFade() {
        SceneWipeHolderFunction::forceCloseWipe(::sWipeFade);
    }

    void closeWipeWhiteFade(s32 frame) {
        SceneWipeHolderFunction::closeWipe(::sWipeWhiteFade, frame);
    }

    void forceOpenWipeWhiteFade() {
        SceneWipeHolderFunction::forceOpenWipe(::sWipeWhiteFade);
    }

    void forceCloseWipeWhiteFade() {
        SceneWipeHolderFunction::forceCloseWipe(::sWipeWhiteFade);
    }

    bool isWipeActive() {
        return SceneWipeHolderFunction::getSceneWipeHolder()->isWipeIn() || SceneWipeHolderFunction::getSceneWipeHolder()->isWipeOut();
    }

    bool isWipeBlank() {
        return SceneWipeHolderFunction::getSceneWipeHolder()->isClose();
    }

    bool isWipeOpen() {
        return SceneWipeHolderFunction::getSceneWipeHolder()->isOpen();
    }

    // A system or scene wipe fully closed: every wipe closes opaque (a fade at full alpha, a ring shut), so the scene
    // under it is not seen. One still closing or opening does not count.
    bool isScreenCoveredByWipe() {
        const SystemWipeHolder* pSystem = ::getSystemWipeHolder();
        if (pSystem != nullptr && pSystem->getCurrent() != nullptr && !MR::isDead(pSystem->getCurrent()) && pSystem->isClose()) {
            return true;
        }
        if (!MR::isExistSceneObj(SceneObj_SceneWipeHolder)) {
            return false;
        }
        const SceneWipeHolder* pScene = SceneWipeHolderFunction::getSceneWipeHolder();
        return pScene->getCurrent() != nullptr && !MR::isDead(pScene->getCurrent()) && pScene->isClose();
    }

    void closeSystemWipeCircle(s32 frame) {
        ::getSystemWipeHolder()->forceOpen(::sWipeCircle);
        ::getSystemWipeHolder()->wipe(nullptr, frame);
    }

    void openSystemWipeFade(s32 frame) {
        ::getSystemWipeHolder()->forceClose(::sWipeFade);
        ::getSystemWipeHolder()->wipe(nullptr, frame);
    }

    void closeSystemWipeFade(s32 frame) {
        ::getSystemWipeHolder()->forceOpen(::sWipeFade);
        ::getSystemWipeHolder()->wipe(nullptr, frame);
    }

    void forceOpenSystemWipeFade() {
        ::getSystemWipeHolder()->forceOpen(::sWipeFade);
    }

    void openSystemWipeWhiteFade(s32 frame) {
        ::getSystemWipeHolder()->forceClose(::sWipeWhiteFade);
        ::getSystemWipeHolder()->wipe(nullptr, frame);
    }

    void closeSystemWipeWhiteFade(s32 frame) {
        ::getSystemWipeHolder()->forceOpen(::sWipeWhiteFade);
        ::getSystemWipeHolder()->wipe(nullptr, frame);
    }

    void forceCloseSystemWipeWhiteFade() {
        ::getSystemWipeHolder()->forceClose(::sWipeWhiteFade);
    }

    void forceCloseSystemWipeFade() {
        ::getSystemWipeHolder()->forceClose(::sWipeFade);
    }

    // A scene's wipe is its own: it goes with the scene, and the intermission scene that follows draws black. A wipe left
    // fully closed (a dead one counts as open: a fade that was never used reads closed) is handed to the system wipe.
    void holdClosedSceneWipeOnSystemWipe() {
        if (!isExistSceneObj(SceneObj_SceneWipeHolder) || ::getSystemWipeHolder() == nullptr) {
            return;
        }

        const SceneWipeHolder* const pHolder = SceneWipeHolderFunction::getSceneWipeHolder();
        const WipeLayoutBase* const pWipe = pHolder->getCurrent();

        if (pWipe == nullptr || isDead(pWipe) || !pWipe->isClose()) {
            return;
        }

        if (pHolder->isCurrent(::sWipeWhiteFade)) {
            forceCloseSystemWipeWhiteFade();
        } else {
            forceCloseSystemWipeFade();
        }
    }

    bool coverScreenForSceneChange() {
        if (!isExistSceneObj(SceneObj_SceneWipeHolder) || ::getSystemWipeHolder() == nullptr) {
            return true;
        }

        const WipeLayoutBase* const pSceneWipe = SceneWipeHolderFunction::getSceneWipeHolder()->getCurrent();

        if (pSceneWipe != nullptr && !isDead(pSceneWipe)) {
            if (pSceneWipe->isWipeOut()) {
                return false;
            }

            if (pSceneWipe->isClose()) {
                return true;
            }
        }

        const WipeLayoutBase* const pSystemWipe = ::getSystemWipeHolder()->getCurrent();

        if (pSystemWipe == nullptr || isDead(pSystemWipe) || !(pSystemWipe->isClose() || pSystemWipe->isWipeOut())) {
            forceCloseSystemWipeWhiteFade();
        }

        return true;
    }

    bool isSystemWipeActive() {
        return ::getSystemWipeHolder()->isWipeIn() || ::getSystemWipeHolder()->isWipeOut();
    }

    void closeSystemWipeCircleWithCaptureScreen(s32 frame) {
        closeSystemWipeCircle(frame);
        ::getSystemWipeHolder()->startGameScreenCapture();
    }

    void closeSystemWipeFadeWithCaptureScreen(s32 frame) {
        closeSystemWipeFade(frame);
        ::getSystemWipeHolder()->startGameScreenCapture();
    }

    void setWipeCircleCenterPos(const Vec3f& rCenterPos) {
        ::getSystemWipeHolder()->setWipeRingCenter(rCenterPos);
    }

    void requestMovementOnImageEffect() {
        if (isExistSceneObj(SceneObj_ImageEffectSystemHolder)) {
            getImageEffectSystemHolder()->pauseOff();
        }

        if (isExistSceneObj(SceneObj_BloomEffect)) {
            requestMovementOn(::getNormalBloom());
        }

        if (isExistSceneObj(SceneObj_BloomEffectSimple)) {
            requestMovementOn(getSceneObj< BloomEffectSimple >(SceneObj_BloomEffectSimple));
        }

        if (isExistSceneObj(SceneObj_ScreenBlurEffect)) {
            requestMovementOn(getSceneObj< ScreenBlurEffect >(SceneObj_ScreenBlurEffect));
        }

        if (isExistSceneObj(SceneObj_DepthOfFieldBlur)) {
            requestMovementOn(getSceneObj< DepthOfFieldBlur >(SceneObj_DepthOfFieldBlur));
        }
    }

    void setImageEffectControlAuto() {
        getImageEffectDirector()->setAuto();
    }

    void turnOffDOFInSubjective() {
        if (isExistImageEffectDirector()) {
            getImageEffectDirector()->turnOffDOFInSubjective();
        }
    }

    void turnOnDOFInSubjective() {
        if (isExistImageEffectDirector()) {
            getImageEffectDirector()->turnOnDOFInSubjective();
        }
    }

    void turnOnNormalBloom() {
        getImageEffectDirector()->turnOnNormal();
    }

    void turnOnDepthOfField(bool isPlayerSync) {
        getImageEffectDirector()->turnOnDepthOfField(isPlayerSync);
    }

    void turnOffImageEffect() {
        getImageEffectDirector()->turnOff();
    }

    void forceOffImageEffect() {
        getImageEffectDirector()->forceOff();
    }

    void createNormalBloom() {
        createSceneObj(SceneObj_BloomEffect);
    }

    bool isNormalBloomOn() {
        return isExistSceneObj(SceneObj_BloomEffect) && ::getNormalBloom()->isSomething();
    }

    bool isNormalBloomForEachModel() {
        return isExistSceneObj(SceneObj_BloomEffect) && ::getNormalBloom()->isForEachModel();
    }

    void setNormalBloomIntensity(u8 intensity) {
        getImageEffectDirector()->setNormalBloomIntensity(intensity);
    }

    void setNormalBloomThreshold(u8 threshold) {
        getImageEffectDirector()->setNormalBloomThreshold(threshold);
    }

    void setNormalBloomBlurIntensity1(u8 intensity1) {
        getImageEffectDirector()->setNormalBloomBlurIntensity1(intensity1);
    }

    void setNormalBloomBlurIntensity2(u8 intensity2) {
        getImageEffectDirector()->setNormalBloomBlurIntensity2(intensity2);
    }

    void createSimpleBloom() {
        createSceneObj(SceneObj_BloomEffectSimple);
    }

    void createScreenBlur() {
        createSceneObj(SceneObj_ScreenBlurEffect);
    }

    void createDepthOfFieldBlur() {
        createSceneObj(SceneObj_DepthOfFieldBlur);
    }

    void setDepthOfFieldBlurIntensity(f32 intensity) {
        getImageEffectDirector()->setDepthOfFieldIntensity(intensity);
    }

    void createCenterScreenBlur() {
        createSceneObj(SceneObj_CenterScreenBlur);
    }

    void startCenterScreenBlur(s32 time, f32 offset, u8 alpha, s32 fadeIn, s32 fadeOut) {
        getSceneObj< CenterScreenBlur >(SceneObj_CenterScreenBlur)->start(time, offset, alpha, fadeIn, fadeOut);
    }

    void requestPowerUpHPMeter() {
        getGameSceneLayoutHolder()->requestPowerUpHPMeter();
    }

    void requestForceAppearHPMeter() {
        getGameSceneLayoutHolder()->requestForceAppearHPMeter();
    }

    void startMissLayout() {
        getGameSceneLayoutHolder()->startMiss();
    }

    bool isMissLayoutAnimEnd() {
        return getGameSceneLayoutHolder()->isMissAnimEnd();
    }

    void activateDefaultGameLayout() {
        getGameSceneLayoutHolder()->activateDefaultGameLayout();
    }

    void deactivateDefaultGameLayout() {
        getGameSceneLayoutHolder()->deactivateDefaultGameLayout();
    }

    bool isActiveDefaultGameLayout() {
        return getGameSceneLayoutHolder()->mIsActiveDefaultGameLayout;
    }

    void forceAppearDefaultGameLayout() {
        getGameSceneLayoutHolder()->activateDefaultGameLayout();
        getGameSceneLayoutHolder()->mCounterLayoutCtrl->forceAppear();
    }

    void pauseOffLensFlare() {
        if (!isExistSceneObj(SceneObj_LensFlareDirector)) {
            return;
        }

        getSceneObj< LensFlareDirector >(SceneObj_LensFlareDirector)->pauseOff();
    }

    void tryScreenToFrameCinemaFrame() {
        ::getCinemaFrame()->tryScreenToFrame();
    }

    void tryFrameToBlankCinemaFrame() {
        ::getCinemaFrame()->tryFrameToBlank();
    }

    void tryBlankToFrameCinemaFrame() {
        ::getCinemaFrame()->tryBlankToFrame();
    }

    void tryFrameToScreenCinemaFrame() {
        ::getCinemaFrame()->tryFrameToScreen();
    }

    void forceToScreenCinemaFrame() {
        ::getCinemaFrame()->forceToScreen();
    }

    void forceToFrameCinemaFrame() {
        ::getCinemaFrame()->forceToFrame();
    }

    void forceToBlankCinemaFrame() {
        ::getCinemaFrame()->forceToBlank();
    }

    bool isStopCinemaFrame() {
        return ::getCinemaFrame()->isStop();
    }

    void appearInformationMessage(const char* pMessageId, bool hasButtonLayout) {
        getGameSceneLayoutHolder()->mInformationMessage->setMessage(pMessageId);
        ::appearInformationMessage(hasButtonLayout, false);
    }

    void appearInformationMessageCenter(const char* pMessageId, bool hasButtonLayout) {
        getGameSceneLayoutHolder()->mInformationMessage->setMessage(pMessageId);
        ::appearInformationMessage(hasButtonLayout, true);
    }

    void appearInformationMessage(const char16_t* pMessage, bool hasButtonLayout) {
        getGameSceneLayoutHolder()->mInformationMessage->setMessage(pMessage);
        ::appearInformationMessage(hasButtonLayout, false);
    }

    void appearInformationMessageCenter(const char16_t* pMessage, bool hasButtonLayout) {
        getGameSceneLayoutHolder()->mInformationMessage->setMessage(pMessage);
        ::appearInformationMessage(hasButtonLayout, true);
    }

    void setInformationMessageReplaceString(const char16_t* pMessage, s32 argIndex) {
        getGameSceneLayoutHolder()->mInformationMessage->setReplaceString(pMessage, argIndex);
    }

    void disappearInformationMessage() {
        getGameSceneLayoutHolder()->mInformationMessage->disappear();
    }

    bool isDeadInformationMessage() {
        return isDead(getGameSceneLayoutHolder()->mInformationMessage);
    }

    void appearYesNoSelector(const char* pRightMessageId, const char* pLeftMessageId, const char* pInformationMessageId) {
        getGameSceneLayoutHolder()->mYesNoLayout->appear();
        getGameSceneLayoutHolder()->mYesNoLayout->setSelectMessage(pRightMessageId, pLeftMessageId);

        if (pInformationMessageId == nullptr) {
            return;
        }

        getGameSceneLayoutHolder()->mYesNoLayout->setInformationMessage(pInformationMessageId);
    }

    bool isYesNoSelected() {
        return getGameSceneLayoutHolder()->mYesNoLayout->mController->isSelected();
    }

    bool isYesNoSelectedYes() {
        return getGameSceneLayoutHolder()->mYesNoLayout->mController->isSelectedYes();
    }

    void setYesNoSelectorSE(const char* pCursorSE, const char* pYesSE, const char* pNoSE) {
        getGameSceneLayoutHolder()->mYesNoLayout->mController->setSE(pCursorSE, pYesSE, pNoSE);
    }

    void resetYesNoSelectorSE() {
        getGameSceneLayoutHolder()->mYesNoLayout->mController->setSE(nullptr, nullptr, nullptr);
    }

    void startMoviePlayer(const char* pName) {
        ::getMoviePlayer()->startMovie(pName, false);
        GameSceneFunction::requestPlayMovieDemo();
    }

    void stopMoviePlayer() {
        ::getMoviePlayer()->stopMovie();
    }

    bool isActiveMoviePlayer() {
        if (!isExistSceneObj(SceneObj_MoviePlayerSimple)) {
            return false;
        }

        return ::getMoviePlayer()->isMovieActive();
    }

    bool isMoviePlayerPlaying() {
        if (!isActiveMoviePlayer()) {
            return false;
        }

        return ::getMoviePlayer()->isMoviePlaying();
    }

    s32 getMovieCurrentFrame() {
        if (!isActiveMoviePlayer()) {
            return -1;
        }

        return ::getMoviePlayer()->getCurrentFrame();
    }

    u32 getMovieTotalFrame() {
        return ::getMoviePlayer()->getTotalFrame();
    }

    void setMovieVolume(f32 volume, s32 step) {
        ::getMoviePlayer()->setVolume(volume, step);
    }

    void appearStarCounter() {
        ::getStarCounter()->appear();
    }

    void disappearStarCounter() {
        ::getStarCounter()->disappear();
    }

    void forceSyncStarPieceCounter() {
        ::getStarPieceCounter()->forceSync();
    }

    void forceAppearStarPieceCounterForTicoFat() {
        ::getStarPieceCounter()->forceAppear(true);
    }

    void disappearStarPieceCounterForTicoFat() {
        ::getStarPieceCounter()->disappear(true);
    }

    void createPurpleCoinCounter() {
        getGameSceneLayoutHolder()->createPurpleCoinCounter();
    }

    void validatePurpleCoinCounter() {
        getGameSceneLayoutHolder()->getPurpleCoinCounter()->validate();
    }

    void requestCounterLayoutAppearanceForTicoEat(bool param1) {
        getGameSceneLayoutHolder()->mCounterLayoutCtrl->requestedTicoEat(param1);
    }

    void openWipeCircle(s32 frame) {
        SceneWipeHolderFunction::openWipe(::sWipeCircle, frame);
    }

    void openWipeFade(s32 frame) {
        SceneWipeHolderFunction::openWipe(::sWipeFade, frame);
    }

    void openWipeWhiteFade(s32 frame) {
        SceneWipeHolderFunction::openWipe(::sWipeWhiteFade, frame);
    }

    void startGameOverWipe() {
        SceneWipeHolderFunction::getSceneWipeHolder()->wipe(::sWipeGameOver, -1);
    }

    void startDownWipe() {
        SceneWipeHolderFunction::getSceneWipeHolder()->wipe(::sWipeKoopa, -1);
    }

    void requestOneUp() {
        startSystemSE(::sSoundOneUp);
        getGameSceneLayoutHolder()->requestOneUp(1);
    }
};  // namespace MR
