#include "Game/System/NebulaCutsceneSkip.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>

#ifdef TARGET_PC
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Demo/DemoDirector.hpp"
#include "Game/Demo/DemoExecutor.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Demo/DemoTimeKeeper.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/JMapIdInfo.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <cstring>
#include <revolution/os.h>

namespace {
    enum State {
        State_Idle,
        State_Prompt,    // the scene holds still behind the Yes/No window
        State_FadeOut,   // Yes: the held picture fades to black
        State_Skipping,  // the cutscene runs on, unseen and unheard
    };

    const s32 cEndQuietFrames = 2;        // frames with no cutscene before the picture returns
    const u32 cStepBudgetMicros = 9000;   // the extra steps' share of a frame
    const s32 cStepsPerFrameMax = 40;
    const s32 cStallSteps = 600;          // a demo's clock held this long waits on the player: the skip ends there
    const u32 cSkipMaxMicros = 15000000;  // a skip that has not ended by then shows the game as it is

    // The demos + skips, by their time sheets; EscapeDriverApr only in the launch star lessons.
    const char* const cSkippableSheets[] = {
        // The Comet Observatory's story
        "RossetaTalk1", "RosettaDomeRecoverDemo", "RosettaToachProgressDemo", "RosettaSuperGStarDemo",
        "RosettaGalaxyCoreDemo", "RosettaFinalBattleDemo", "RosettaKoopaExplainDemo", "RosettaLuigiDemo",
        "RosettaAstroToadsDemo", "LuigiLostDemo", "ButlerReport", "RosettaReading", "EpDemo",
        // Lessons the game starts itself
        "TicoGuideDemo", "SpinGetDemo", "ReadStar", "ButlerDomeLecture1", "ButlerDomeLecture2", "ButlerMapLecture",
        "ButlerStarPiece1", "ButlerStarPiece2", "ButlerGreenDriverExplain", "RosettaComet", "RosettaLightExplainDemo",
        "CaptainEntry", "CageBroken", "EscapeDriverApr",
        // Stars returning to the Observatory
        "GalaxyPowerStarReturn", "DomeStarReturn", "GrandStarReturn1", "GrandStarReturn2", "GrandStarReturn3",
        "GrandStarReturn4", "GrandStarReturn5", "GrandStarReturn6", "GalaxyGrandStarReturn",
        // Bosses
        "KoopaVs1Start", "KoopaVs1Down", "KoopaVs2Start", "KoopaVs2Down", "KoopaVs3Start", "KoopaMeetPeach",
        "KoopaJrAppearTripod", "KoopaJrAppearOtaKing", "KoopaJrShipBattle", "MechanicKoopaExplosion", "BossBegomanEntry",
        "SkeletalFishBossDemo", "MeraKingOpening", "StinkBugParent", "HoneyQueenFirst", "WormAppear",
    };

    // The bosses whose own code starts their scenes (no time sheet), by their placement names.
    const char* const cSkippableStarters[] = {
        "KoopaVs1", "KoopaVs2", "KoopaVs3", "TripodBoss", "TombSpider", "Polta", "DinoPackun", "DinoPackunVs2",
        "BossKameck", "BossKameck2", "BossStinkBug", "Dodoryu", "IceMerameraKing", "SkeletalFishBoss", "BossBegoman",
        "OtaKing", "KoopaJrShip",
    };

    State sState = State_Idle;
    const DemoExecutor* sStallExecutor = nullptr;  // the demo clock last seen, and for how many steps it has held
    s32 sStallStep = -1;
    s32 sStallSteps = 0;
    SysInfoWindow* sWindow = nullptr;
    bool sNeedRelease = false;  // + was held when the last prompt or skip ended
    bool sGuidanceWasActive = false;  // the pointer guidance, off while the question is up (it draws over it)
    bool sSkipDemo = false;     // the skip runs a demo on (else it stops a movie)
    bool sMovieSkip = false;
    bool sEndRequested = false;
    s32 sQuietFrames = 0;
    s32 sSteps = 0;
    s32 sFadeFrames = 0;
    s32 sFrameSteps = 0;
    OSTick sFrameStart = 0;
    OSTime sSkipStart = 0;

    bool isGameSceneRunning() {
        GameSystem* pSystem = SingletonHolder< GameSystem >::get();
        if (pSystem == nullptr || pSystem->mSceneController == nullptr) {
            return false;
        }
        GameSystemSceneController* pController = pSystem->mSceneController;
        return pController->isExistSceneObjHolder() && pController->isSceneInitializeState(SceneInitializeState_End) &&
               strcmp(pController->mCurrSceneControlInfo.mScene, "Game") == 0;
    }

    const DemoDirector* getActiveDemo() {
        if (!MR::isExistSceneObj(SceneObj_DemoDirector)) {
            return nullptr;
        }
        const DemoDirector* pDirector = DemoFunction::getDemoDirector();
        return pDirector->mIsActive && pDirector->_30 != nullptr ? pDirector : nullptr;
    }

    // A time-keep demo's executor: the director's own, or the one its name finds (a demo the game requested).
    const DemoExecutor* getExecutor(const DemoDirector* pDirector) {
        return pDirector->mExecutor != nullptr ? pDirector->mExecutor : DemoFunction::findDemoExecutor(pDirector->_30);
    }

    bool isSkippableExecutor(const DemoExecutor* pExecutor) {
        if (pExecutor == sStallExecutor && sStallSteps >= cStallSteps && pExecutor->mTimeKeeper != nullptr &&
            pExecutor->mTimeKeeper->mCurrentStep + pExecutor->mTimeKeeper->_10 * 0x10000 == sStallStep) {
            return false;  // the last skip left it waiting on the player
        }
        const char* pSheet = pExecutor->mSheetName;
        if (pSheet == nullptr) {
            return false;
        }
        for (u32 i = 0; i < sizeof(cSkippableSheets) / sizeof(cSkippableSheets[0]); i++) {
            if (strcmp(pSheet, cSkippableSheets[i]) != 0) {
                continue;
            }
            if (strcmp(pSheet, "EscapeDriverApr") != 0) {
                return true;
            }
            // Its lesson (MarioLancherLecture3Zone), not a level's launch star appearing.
            const char* pZone = pExecutor->mInfo != nullptr ? MR::getZoneNameFromZoneId(pExecutor->mInfo->mZoneID) : nullptr;
            return pZone != nullptr && strstr(pZone, "Lecture") != nullptr;
        }
        return false;
    }

    // Placed objects are named by the stage's table of Japanese names (PlacementInfoOrdered).
    bool isSkippableStarter(const NameObj* pStarter) {
        if (pStarter == nullptr || pStarter->mName == nullptr) {
            return false;
        }
        for (u32 i = 0; i < sizeof(cSkippableStarters) / sizeof(cSkippableStarters[0]); i++) {
            if (strcmp(pStarter->mName, MR::getJapaneseObjectName(cSkippableStarters[i])) == 0) {
                return true;
            }
        }
        return false;
    }

    bool isSkippableDemo(const DemoDirector* pDirector) {
        const DemoExecutor* pExecutor = getExecutor(pDirector);
        return pExecutor != nullptr ? isSkippableExecutor(pExecutor) : isSkippableStarter(pDirector->_2C);
    }

    // What + skips now. Asked only when + is pressed and while a skip runs.
    bool isSkippableNow() {
        if (!isGameSceneRunning() || GameSequenceFunction::isActiveSaveDataHandleSequence()) {
            return false;
        }
        if (MR::isActiveMoviePlayer() || GameSceneFunction::isExecScenarioOpeningCamera()) {
            return true;
        }
        const DemoDirector* pDirector = getActiveDemo();
        return pDirector != nullptr && isSkippableDemo(pDirector);
    }

    // The log's name for what runs: a time sheet, or the starter's (Shift-JIS) name.
    void report(const char* pWhat) {
        const bool isScene = isGameSceneRunning();
        const DemoDirector* pDirector = isScene ? getActiveDemo() : nullptr;
        const DemoExecutor* pExecutor = pDirector != nullptr ? getExecutor(pDirector) : nullptr;
        JUTNativeLog("cutscene skip: %s (%s%s%s%s)\n", pWhat, MR::isActiveMoviePlayer() ? "movie" : "",
                 isScene && GameSceneFunction::isExecScenarioOpeningCamera() ? "opening camera" : "",
                 pExecutor != nullptr && pExecutor->mSheetName != nullptr ? pExecutor->mSheetName : "",
                 pExecutor == nullptr && pDirector != nullptr && pDirector->_2C != nullptr ? pDirector->_2C->mName : "");
    }

    // A time-keep demo whose clock has not moved for cStallSteps waits on the player (an action, not a page).
    bool isStalled() {
        const DemoDirector* pDirector = getActiveDemo();
        const DemoExecutor* pExecutor = pDirector != nullptr ? getExecutor(pDirector) : nullptr;
        if (pExecutor == nullptr || pExecutor->mTimeKeeper == nullptr) {
            sStallExecutor = nullptr;
            sStallSteps = 0;
            return false;
        }
        const s32 step = pExecutor->mTimeKeeper->mCurrentStep + pExecutor->mTimeKeeper->_10 * 0x10000;
        if (pExecutor != sStallExecutor || step != sStallStep) {
            sStallExecutor = pExecutor;
            sStallStep = step;
            sStallSteps = 0;
            return false;
        }
        return ++sStallSteps >= cStallSteps;
    }

    // The question in the played language (NebulaText; the game's own Yes and No answer it), its lines broken as
    // the game's messages are (its text boxes do not wrap).
    const wchar_t* promptText() {
        static wchar_t question[128];
        NebulaText::get(question, 128, "SkipCutsceneQuestion");
        return question;
    }

    void mute(bool on) {
        AudSystem* pSystem = AudWrap::getSystem();
        if (pSystem != nullptr) {
            pSystem->muteForCutsceneSkip(on);
        }
    }

    void silenceSe(bool silent) {
        AudSystem* pSystem = AudWrap::getSystem();
        if (pSystem != nullptr) {
            pSystem->silenceSeForCutsceneSkip(silent);
        }
    }

    // The picture and sound fade back in as the game's own fades do.
    void finishSkip(const char* pWhy) {
        sState = State_Idle;
        sMovieSkip = false;
        sEndRequested = false;
        sNeedRelease = true;
        MR::openSystemWipeFade();
        mute(false);
        silenceSe(false);
        JUTNativeLog("cutscene skip: done after %d steps (%s)\n", sSteps, pWhy);
    }
};  // namespace

namespace NebulaCutsceneSkip {
    void init() {
        sWindow = MR::createSysInfoWindow();
        sWindow->setBackAnswersNo(true);
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
            if (sNeedRelease || !MR::testCorePadTriggerPlus(WPAD_CHAN0)) {
                break;
            }
            sNeedRelease = true;
            if (!isSkippableNow()) {
                if (isGameSceneRunning()) {
                    report("not offered");
                }
                break;
            }
            sState = State_Prompt;
            mute(true);
            sGuidanceWasActive = MR::isActiveStarPointerGuidance();
            MR::deactiveStarPointerGuidance();
            sWindow->appearYesNoWithText(promptText());
            report("asked");
            break;
        }
        case State_Prompt:
            // + again answers No.
            if (!MR::testCorePadButtonPlus(WPAD_CHAN0)) {
                sNeedRelease = false;
            } else if (!sNeedRelease && MR::testCorePadTriggerPlus(WPAD_CHAN0)) {
                sNeedRelease = true;
                sWindow->decideNo();
            }
            MR::requestStarPointerModeSaveLoad(sWindow);
            MR::updateStarPointerModeWhileSceneHeld();
            sWindow->movement();
            sWindow->calcAnim();
            if (!MR::isDead(sWindow)) {
                break;
            }
            if (sGuidanceWasActive) {
                MR::activeStarPointerGuidance();
            }
            if (!sWindow->isSelectedYes() || !isSkippableNow()) {
                sState = State_Idle;
                mute(false);
                JUTNativeLog("cutscene skip: declined\n");
                break;
            }
            sState = State_FadeOut;
            sFadeFrames = 0;
            MR::closeSystemWipeFade();
            break;
        case State_FadeOut:
            // The scene still holds; the fade runs with the system's layouts.
            MR::updateStarPointerModeWhileSceneHeld();
            if (++sFadeFrames < 2 || MR::isSystemWipeActive()) {
                break;
            }
            sState = State_Skipping;
            sSteps = 0;
            sQuietFrames = 0;
            sFrameSteps = 0;
            sEndRequested = false;
            sStallExecutor = nullptr;
            sStallSteps = 0;
            sMovieSkip = MR::isActiveMoviePlayer();
            sSkipDemo = !sMovieSkip;
            sSkipStart = OSGetTime();
            silenceSe(true);
            report("skipping");
            break;
        case State_Skipping: {
            sSteps++;
            const char* pEnd = nullptr;
            if (sEndRequested) {
                pEnd = "a question";
            } else if (!isGameSceneRunning()) {
                pEnd = "the scene ended";
            } else if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
                pEnd = "a save";
            } else if (OSTicksToMicroseconds(OSGetTime() - sSkipStart) >= cSkipMaxMicros) {
                pEnd = "time";
            } else if (sSkipDemo && isStalled()) {
                pEnd = "waits on the player";
            }
            if (pEnd != nullptr) {
                report("ends");
                finishSkip(pEnd);
                break;
            }
            if (sSkipDemo ? isSkippableNow() : (sMovieSkip || MR::isActiveMoviePlayer())) {
                sQuietFrames = 0;
            } else if (++sQuietFrames >= cEndQuietFrames) {
                report("ends");
                finishSkip("the cutscene ended");
            }
            break;
        }
        }
    }

    void draw() {
        if (sWindow != nullptr && !MR::isDead(sWindow)) {
            sWindow->draw();
        }
    }

    bool isFreezingScene() {
        return sState == State_Prompt || sState == State_FadeOut;
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

    bool isSkipping() {
        return sState == State_Skipping;
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
