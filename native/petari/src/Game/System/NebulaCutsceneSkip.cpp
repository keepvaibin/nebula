#include "Game/System/NebulaCutsceneSkip.hpp"

#ifdef TARGET_PC
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/Language.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <cstring>
#include <revolution/os.h>

namespace {
    enum State {
        State_Idle,
        State_Prompt,    // the scene holds still behind the Yes/No window
        State_Skipping,  // the cutscene runs on, unseen and unheard
    };

    const s32 cEndQuietFrames = 2;        // frames with no cutscene before the picture returns
    const s32 cFadeInFrames = 20;
    const u32 cStepBudgetMicros = 9000;   // the extra steps' share of a frame
    const s32 cStepsPerFrameMax = 40;

    State sState = State_Idle;
    SysInfoWindow* sWindow = nullptr;
    bool sNeedRelease = false;  // + was held when the last prompt or skip ended
    bool sSkipDemo = false;     // the skip runs a demo on (else it stops a movie)
    bool sMovieSkip = false;
    bool sEndRequested = false;
    s32 sQuietFrames = 0;
    s32 sSteps = 0;
    s32 sFrameSteps = 0;
    OSTick sFrameStart = 0;

    bool isGameSceneRunning() {
        GameSystem* pSystem = SingletonHolder< GameSystem >::get();
        if (pSystem == nullptr || pSystem->mSceneController == nullptr) {
            return false;
        }
        GameSystemSceneController* pController = pSystem->mSceneController;
        return pController->isExistSceneObjHolder() && pController->isSceneInitializeState(SceneInitializeState_End) &&
               strcmp(pController->mCurrSceneControlInfo.mScene, "Game") == 0;
    }

    // What + offers to skip: the prerendered movies (THP). Demos, dialogue and the galaxies' opening cameras run on
    // through isCutsceneActive once the skip covers them.
    bool isPromptable() {
        return isGameSceneRunning() && MR::isActiveMoviePlayer();
    }

    bool isCutsceneActive() {
        if (!isGameSceneRunning()) {
            return false;
        }
        if (MR::isActiveMoviePlayer()) {
            return true;
        }
        if (MR::isExistSceneObj(SceneObj_DemoDirector) && MR::isDemoActive()) {
            return true;
        }
        return GameSceneFunction::isExecScenarioOpeningCamera() || GameSceneFunction::isExecScenarioStarter() ||
               GameSceneFunction::isExecStageClearDemo();
    }

    // The question in the played language (the game's own Yes and No answer it), its lines broken as the
    // game's messages are (its text boxes do not wrap).
    const wchar_t* promptText() {
        switch (MR::getLanguage()) {
        case LANGUAGE_USFRENCH:
        case LANGUAGE_EUFRENCH:
            return L"Voulez-vous vraiment passer\ncette cin\u00e9matique ?";
        case LANGUAGE_USSPANISH:
        case LANGUAGE_EUSPANISH:
            return L"\u00bfSeguro que quieres saltar\nesta escena?";
        case LANGUAGE_EUGERMAN:
            return L"M\u00f6chtest du diese Zwischensequenz\nwirklich \u00fcberspringen?";
        case LANGUAGE_EUITALIAN:
            return L"Vuoi davvero saltare\nquesta scena?";
        case LANGUAGE_EUDUTCH:
            return L"Wil je dit tussenfilmpje\necht overslaan?";
        default:
            return L"Are you sure you would like to skip\nthis cutscene?";
        }
    }

    void mute(bool on) {
        AudSystem* pSystem = AudWrap::getSystem();
        if (pSystem != nullptr) {
            pSystem->muteForCutsceneSkip(on);
        }
    }

    void finishSkip() {
        sState = State_Idle;
        sMovieSkip = false;
        sEndRequested = false;
        sNeedRelease = true;
        MR::openSystemWipeFade(cFadeInFrames);
        mute(false);
        OSReport("cutscene skip: done after %d steps\n", sSteps);
    }
};  // namespace

namespace NebulaCutsceneSkip {
    void init() {
        sWindow = MR::createSysInfoWindow();
        sWindow->kill();
    }

    void update() {
        if (sWindow == nullptr) {
            return;
        }

        switch (sState) {
        case State_Idle: {
            if (!MR::testCorePadButtonPlus(WPAD_CHAN0)) {
                sNeedRelease = false;
            }
            if (sNeedRelease || !MR::testCorePadTriggerPlus(WPAD_CHAN0) || !isPromptable() ||
                GameSequenceFunction::isActiveSaveDataHandleSequence()) {
                break;
            }
            sNeedRelease = true;
            sState = State_Prompt;
            mute(true);
            sWindow->appearYesNoWithText(promptText());
            OSReport("cutscene skip: asked\n");
            break;
        }
        case State_Prompt:
            MR::requestStarPointerModeSaveLoad(sWindow);
            MR::updateStarPointerModeWhileSceneHeld();
            sWindow->movement();
            sWindow->calcAnim();
            if (!MR::isDead(sWindow)) {
                break;
            }
            if (!sWindow->isSelectedYes() || !isPromptable()) {
                sState = State_Idle;
                mute(false);
                OSReport("cutscene skip: declined\n");
                break;
            }
            sState = State_Skipping;
            sSteps = 0;
            sQuietFrames = 0;
            sFrameSteps = 0;
            sEndRequested = false;
            sMovieSkip = MR::isActiveMoviePlayer();
            sSkipDemo = !sMovieSkip;
            MR::closeSystemWipeFade(1);
            OSReport("cutscene skip: skipping%s\n", sMovieSkip ? " (movie)" : "");
            break;
        case State_Skipping:
            sSteps++;
            if (sEndRequested || !isGameSceneRunning() || GameSequenceFunction::isActiveSaveDataHandleSequence()) {
                finishSkip();
                break;
            }
            if (sSkipDemo ? isCutsceneActive() : (sMovieSkip || MR::isActiveMoviePlayer())) {
                sQuietFrames = 0;
            } else if (++sQuietFrames >= cEndQuietFrames) {
                finishSkip();
            }
            break;
        }
    }

    void draw() {
        if (sWindow != nullptr && !MR::isDead(sWindow)) {
            sWindow->draw();
        }
    }

    bool isFreezingScene() {
        return sState == State_Prompt;
    }

    bool tryExtraStep() {
        if (sState != State_Skipping || !sSkipDemo) {
            sFrameSteps = 0;
            return false;
        }
        if (sFrameSteps == 0) {
            sFrameStart = OSGetTick();
        }
        if (sFrameSteps >= cStepsPerFrameMax ||
            OSTicksToMicroseconds(OSDiffTick(OSGetTick(), sFrameStart)) >= cStepBudgetMicros) {
            sFrameSteps = 0;
            return false;
        }
        sFrameSteps++;
        return true;
    }

    bool isPressingA(s32 step) {
        (void)step;
        return sState == State_Skipping && sSkipDemo && (sSteps & 1) == 0;
    }

    bool takeMovieSkip() {
        if (sState != State_Skipping || !sMovieSkip) {
            return false;
        }
        sMovieSkip = false;
        return true;
    }

    void endForQuestion() {
        if (sState == State_Skipping) {
            sEndRequested = true;
        }
    }
};  // namespace NebulaCutsceneSkip
#endif
