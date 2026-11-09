#include "Game/System/NebulaMods.hpp"
#include "Game/System/StorySequenceExecutor.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Demo/PrologueDirector.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Scene/GameSceneFunction.hpp"
#include "Game/Screen/MoviePlayingSequence.hpp"
#include "Game/Screen/StaffRoll.hpp"
#include "Game/System/GalaxyMoveArgument.hpp"
#include "Game/System/GameDataConst.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameSequenceFunction.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/StageResultSequenceChecker.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    using Story = StorySequenceExecutorType;

    const Story::DemoSequenceInfo cDemoPowerStarResultAstroDome[] = {
        {Story::Step_Demo, Story::Event_Demo, "天文ドームスター帰還"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoPowerStarResultAstroDomeWithSave[] = {
        {Story::Step_Demo, Story::Event_Demo, "天文ドームスター帰還"},
        {Story::Step_Save, Story::Event_None, "Save"},
        {Story::Step_End},
    };
    const Story::DemoSequenceInfo cDemoPowerStarResultAstroGalaxy[] = {
        {Story::Step_Demo, Story::Event_Demo, "パワースター帰還"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoGrandStarResult[] = {
        {Story::Step_Demo, Story::Event_Demo, "グランドスター帰還[２回目以降]"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoPrologue[] = {
        {Story::Step_Prologue, Story::Event_Demo, "Prologue"},
        {Story::Step_End},
    };
    const Story::DemoSequenceInfo cDemoGrandStar1ResultFirst[] = {
        {Story::Step_Demo, Story::Event_Demo, "グランドスター１帰還"},
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタ状況説明デモ"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoKinopioRescued[] = {
        {Story::Step_Demo, Story::Event_Demo, "パワースター帰還"},
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタキノピオ探検隊デモ"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoTorchLecture[] = {
        {Story::Step_Demo, Story::Event_Demo, "グランドスター２帰還"},
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタトーチの炎説明デモ"},
        {Story::Step_Demo, Story::Event_Demo, "バトラーマップレクチャー"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoTrickCometLecture[] = {
        {Story::Step_Demo, Story::Event_Demo, "パワースター帰還"},
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタコメット説明デモ"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoRosettaTalkAboutKoopa[] = {
        {Story::Step_Demo, Story::Event_Demo, "グランドスター３帰還"},
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタ銀河の中心説明デモ"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoLuigiRescued[] = {
        {Story::Step_Demo, Story::Event_Demo, "パワースター帰還"},
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタルイージデモ"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoRosettaTalkAstroDomeRecover[] = {
        {Story::Step_Demo, Story::Event_Demo, "グランドスター４帰還"},
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタ天文台機能回復デモ"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoGrandStarReturn5[] = {
        {Story::Step_Demo, Story::Event_Demo, "グランドスター５帰還"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoRosettaTalkCountDownStart[] = {
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタカウントダウン開始デモ"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoGrandStarReturn6[] = {
        {Story::Step_Demo, Story::Event_Demo, "グランドスター６帰還"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoRosettaTalkTorchProgress[] = {
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタトーチの炎進捗デモ"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoRosettaTalkKoopaBattleVs3GalaxyAppear[] = {
        {Story::Step_Save, Story::Event_None, "Save"},
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタ最終決戦デモ"},
        {Story::Step_End},
    };
    const Story::DemoSequenceInfo cDemoStarPieceLectureRetry[] = {
        {Story::Step_Demo, Story::Event_StarPieceLecture, "スターピース解説前半"},
        {Story::Step_Fadein, Story::Event_Demo, "Fadein"},
        {Story::Step_End},
    };
    const Story::DemoSequenceInfo cDemoNewGalaxyDiscover[] = {
        {Story::Step_Demo, Story::Event_Demo, "天文ドームスター帰還"},
        {Story::Step_Demo, Story::Event_ButlerReportNewGalaxy, "バトラー報告"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoGreenStarGetFirst[] = {
        {Story::Step_Demo, Story::Event_Demo, "パワースター帰還"},
        {Story::Step_Demo, Story::Event_ButlerGreenDriverFirst, "バトラーグリーンドライバ説明"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoGreenStarRelease[] = {
        {Story::Step_Demo, Story::Event_Demo, "パワースター帰還"},
        {Story::Step_Demo, Story::Event_ButlerGreenDriverRelease, "バトラーグリーンドライバ説明"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoAstroDomeLecture[] = {
        {Story::Step_Demo, Story::Event_DomeLecture, "ドームレクチャー１"},
        {Story::Step_StoryEvent, Story::Event_None, "バトラー情報Ａ"},
        {Story::Step_End},
    };
    const Story::DemoSequenceInfo cDemoLuigiMissing[] = {
        {Story::Step_Demo, Story::Event_Demo, "ルイージ失踪デモ"},
        {Story::Step_EndSave},
    };
    const Story::DemoSequenceInfo cDemoEpilogueNormal[] = {
        {Story::Step_PointerMode, Story::Event_PointerModeEnding},
        {Story::Step_Movie, Story::Event_MovieEpilogueA, "MovieEpilogueA"},
        {Story::Step_Demo, Story::Event_Demo, "エピローグデモ"},
        {Story::Step_InputPermission, Story::Event_InputOff},
        {Story::Step_Movie, Story::Event_MovieEndingA, "MovieEndingA"},
        {Story::Step_StaffRoll, Story::Event_StaffRoll},
        {Story::Step_InputPermission, Story::Event_InputOn},
        {Story::Step_StaffRoll, Story::Event_StaffRollInfo},
        {Story::Step_ChangeSceneTitle, Story::Event_Demo},
        {Story::Step_End},
    };
    const Story::DemoSequenceInfo cDemoEpilogueComplete[] = {
        {Story::Step_PointerMode, Story::Event_PointerModeEnding},
        {Story::Step_Movie, Story::Event_MovieEpilogueA, "MovieEpilogueA"},
        {Story::Step_Demo, Story::Event_Demo, "エピローグデモ"},
        {Story::Step_InputPermission, Story::Event_InputOff},
        {Story::Step_Movie, Story::Event_MovieEndingA, "MovieEndingA"},
        {Story::Step_StaffRoll, Story::Event_StaffRoll},
        {Story::Step_Movie, Story::Event_MovieEndingB, "MovieEndingB"},
        {Story::Step_InputPermission, Story::Event_InputOn},
        {Story::Step_StaffRoll, Story::Event_StaffRollInfo},
        {Story::Step_ChangeSceneTitle, Story::Event_Demo},
        {Story::Step_End},
    };
    const Story::DemoSequenceInfo cDemoRosettaPicureBookTalk[] = {
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタ朗読"},
        {Story::Step_Fadein, Story::Event_Demo, "Fadein"},
        {Story::Step_End},
    };
    const Story::DemoSequenceInfo cDemoRosettaTalkAfterNormalEnding[] = {
        {Story::Step_Demo, Story::Event_Demo, "ロゼッタノーマルエンディング後デモ"},
        {Story::Step_EventFlag, Story::Event_Demo, "RosettaTalkAfterNormalEnding"},
        {Story::Step_Fadein, Story::Event_Demo, "Fadein"},
        {Story::Step_End},
    };
    const DemoFortressDiscoverCheckList cDemoFortressDiscoverCheckListTable[] = {
        {
            "TriLegLv1Galaxy",
            "クッパＪｒロボプラント発見",
            false,
        },
        {
            "KoopaBattleVs1Galaxy",
            "クッパスタープラント発見",
            true,
        },
        {
            "KoopaJrShipLv1Galaxy",
            "クッパＪｒシッププラント発見",
            false,
        },
        {
            "KoopaBattleVs2Galaxy",
            "クッパダークマタープラント発見",
            true,
        },
        {
            "FloaterOtaKingGalaxy",
            "クッパＪｒクリーチャープラント発見",
            false,
        },
    };
};  // namespace

namespace {
    NEW_NERVE(StorySequenceExecutorIdle, StorySequenceExecutor, Idle);
    NEW_NERVE(StorySequenceExecutorWaitToSceneStart, StorySequenceExecutor, WaitToSceneStart);
    NEW_NERVE(StorySequenceExecutorPlayDemoSequence, StorySequenceExecutor, PlayDemoSequence);
    NEW_NERVE(StorySequenceExecutorWaitTimeKeepDemoEnd, StorySequenceExecutor, WaitTimeKeepDemoEnd);
    NEW_NERVE(StorySequenceExecutorWaitSaveEnd, StorySequenceExecutor, WaitSaveEnd);
    NEW_NERVE(StorySequenceExecutorStartSaveAfterSequence, StorySequenceExecutor, StartSaveAfterSequence);
    NEW_NERVE(StorySequenceExecutorWaitSaveEndAfterSequence, StorySequenceExecutor, WaitSaveEndAfterSequence);
    NEW_NERVE(StorySequenceExecutorFadeinAfterSequence, StorySequenceExecutor, FadeinAfterSequence);
    NEW_NERVE(StorySequenceExecutorStaffRollSequence, StorySequenceExecutor, StaffRollSequence);
};  // namespace

StorySequenceExecutor::StorySequenceExecutor() : NerveExecutor("StorySequenceExecutor"), mNextNerve(), mDemoObj(), mSaveObj(), mDemoSequences(), mDynamicDemoInfos(), mIsSaveStarted() {
    mDemoObj = new NameObj("StorySequenceExecutor");
    mSaveObj = new NameObj("Save");
    mStageName = "";
    mScenarioNo = -1;

    initNerve(GET_NERVE_ANON(StorySequenceExecutorIdle));
}

void StorySequenceExecutor::update() {
    updateNerve();
}

namespace {
    inline bool isDemoSequenceEnd(const StorySequenceExecutorType::DemoSequenceInfo* pInfo) {
        return pInfo->mType == Story::Step_End || pInfo->mType == Story::Step_EndSave;
    }
};  // namespace

bool StorySequenceExecutor::isNeedMoviePlayerExecutingEventEnum() const {
    for (s32 i = 0; i < mDemoSequences.size(); i++) {
        const StorySequenceExecutorType::DemoSequenceInfo* pInfo = mDemoSequences[i];
        while (!isDemoSequenceEnd(pInfo)) {
            if (pInfo->mType == Story::Step_Movie) {
                return true;
            }

            pInfo++;
        }
    }

    return false;
}

bool StorySequenceExecutor::hasNextDemo() const {
    for (s32 i = 0; i < mDemoSequences.size(); i++) {
        const StorySequenceExecutorType::DemoSequenceInfo* pInfo = mDemoSequences[i];
        if (i == 0) {
            pInfo++;
        }

        while (!isDemoSequenceEnd(pInfo)) {
            if (pInfo->mType == Story::Step_Demo) {
                return true;
            }

            pInfo++;
        }
    }

    return false;
}

s32 StorySequenceExecutor::getExecutingEventEnum() const {
    if (getCurrentDemoInfo() == nullptr) {
        return 0;
    }

    return getCurrentDemoInfo()->mEvent;
}

bool StorySequenceExecutor::isEqualStageScenarioBefore(const char* pStageName, int scenarioNo) const {
    return MR::isEqualString(pStageName, mStageName) && scenarioNo == mScenarioNo;
}

void StorySequenceExecutor::moveGalaxy(GalaxyMoveArgument* pMoveArgument, bool noScenarioBefore) {
    setBeforeStageScenario(*pMoveArgument, noScenarioBefore);

    if (pMoveArgument->isEqualStage("AstroGalaxy") && pMoveArgument->mScenarioNo != 4) {
        pMoveArgument->mScenarioNo = calcAproposScenarioNoOnAstroGalaxy();
    }

    if (pMoveArgument->isEqualStageScenario("AstroDome", -1)) {
        pMoveArgument->mScenarioNo = calcAproposScenarioNoOnAstroDome();
    }

    for (s32 i = 0; i < mDynamicDemoInfos.size(); i++) {
        delete mDynamicDemoInfos[i];
    }

    mDynamicDemoInfos.clear();
    mDemoSequences.clear();

    mIsSaveStarted = false;

    switch (pMoveArgument->mMoveType) {
    case GalaxyMoveArgument::Type_InGame:
        decideNextEventForMoveGalaxy(pMoveArgument);
        break;
    case GalaxyMoveArgument::Type_GoBackAstroDome:
        decideNextStageForGalaxyOut(pMoveArgument);
        break;
    case GalaxyMoveArgument::Type_AfterStageClear:
        decideNextEventForClearGalaxy(pMoveArgument);
        decideNextEventForMoveGalaxy(pMoveArgument);
        break;
    case GalaxyMoveArgument::Type_AfterLoadingGameData:
        overwriteGalaxyNameAfterLoading(pMoveArgument);
        decideNextEventForAfterLoading(pMoveArgument);
        break;
    case GalaxyMoveArgument::Type_Title:
        pMoveArgument->mStageName = "FileSelect";
        pMoveArgument->mScenarioNo = 1;
        break;
    default:
        break;
    }

    if (isEqualStageStopCometScheduler(pMoveArgument->mStageName)) {
        GameSequenceFunction::deactivateGalaxyCometScheduler();
    }
}

void StorySequenceExecutor::setNerveSceneStart() {
    if (!isEqualStageStopCometScheduler(MR::getCurrentStageName())) {
        GameSequenceFunction::activateGalaxyCometScheduler();
    }

    if (mNextNerve != nullptr) {
        setNerve(mNextNerve);

        mNextNerve = nullptr;
    }
}

void StorySequenceExecutor::forceStop() {
    mDemoSequences.clear();

    mIsSaveStarted = false;
    mNextNerve = nullptr;

    setNerve(GET_NERVE_ANON(StorySequenceExecutorIdle));
}

void StorySequenceExecutor::exeIdle() {
}

void StorySequenceExecutor::exeWaitToSceneStart() {
}

void StorySequenceExecutor::exePlayDemoSequence() {
    const StorySequenceExecutorType::DemoSequenceInfo* pDemoInfo = getCurrentDemoInfo();

    // A scene change emptied the list while this ran: nothing is left to play.
    if (pDemoInfo == nullptr) {
        mIsSaveStarted = false;
        setNerve(GET_NERVE_ANON(StorySequenceExecutorIdle));
        return;
    }

    switch (pDemoInfo->mType) {
    case Story::Step_Demo:
        // A save whose progress this scene does not show has no such demo: the story goes on without it.
        if (DemoFunction::findDemoExecutor(pDemoInfo->mName) == nullptr) {
            mDemoSequences[0]++;

            setNerve(GET_NERVE_ANON(StorySequenceExecutorPlayDemoSequence));
        } else if (tryStartDemo(pDemoInfo->mName)) {
            setNerve(GET_NERVE_ANON(StorySequenceExecutorWaitTimeKeepDemoEnd));
        }

        break;
    case Story::Step_Fadein:
        if (tryStartFadein()) {
            mDemoSequences[0]++;

            setNerve(GET_NERVE_ANON(StorySequenceExecutorPlayDemoSequence));
        }

        break;
    case Story::Step_Movie:
        if (tryStartMovieAndWaitEnd(pDemoInfo->mEvent)) {
            mDemoSequences[0]++;

            setNerve(GET_NERVE_ANON(StorySequenceExecutorPlayDemoSequence));
        }

        break;
    case Story::Step_ChangeSceneTitle:
        MR::requestChangeSceneTitle();
        forceStop();
        break;
    case Story::Step_Save:
        if (tryStartSave()) {
            setNerve(GET_NERVE_ANON(StorySequenceExecutorWaitSaveEnd));
        }

        break;
    case Story::Step_EventFlag:
        GameDataFunction::onGameEventFlag(pDemoInfo->mName);
        mDemoSequences[0]++;
        exePlayDemoSequence();
        break;
    case Story::Step_StoryEvent:
        GameDataFunction::followStoryEventByName(pDemoInfo->mName);
        mDemoSequences[0]++;
        exePlayDemoSequence();
        break;
    case Story::Step_Prologue:
        MR::forceOpenWipeFade();
        MR::startPrologue();
        mDemoSequences[0]++;
        exePlayDemoSequence();
        break;
    case Story::Step_StaffRoll:
        GameSceneFunction::requestStaffRoll();
        setNerve(GET_NERVE_ANON(StorySequenceExecutorStaffRollSequence));
        break;
    case Story::Step_PointerMode:
        if (pDemoInfo->mEvent == Story::Event_PointerModeEnding) {
            MR::startStarPointerModeEnding(this);
        }

        mDemoSequences[0]++;
        exePlayDemoSequence();
        break;
    case Story::Step_InputPermission:
        if (pDemoInfo->mEvent == Story::Event_InputOff) {
            GameSystemFunction::setPermissionToCheckInput(false);
        } else if (pDemoInfo->mEvent == Story::Event_InputOn) {
            GameSystemFunction::setPermissionToCheckInput(true);
        }

        mDemoSequences[0]++;
        exePlayDemoSequence();
        break;
    case Story::Step_End:
        if (tryNextDemoInfo()) {
            exePlayDemoSequence();
        } else {
            mDemoSequences.clear();

            mIsSaveStarted = false;

            setNerve(GET_NERVE_ANON(StorySequenceExecutorIdle));
        }

        break;
    case Story::Step_EndSave:
        if (tryNextDemoInfo()) {
            exePlayDemoSequence();
        } else {
            const Nerve* pNerve;

            if (mIsSaveStarted) {
                pNerve = GET_NERVE_ANON(StorySequenceExecutorFadeinAfterSequence);
            } else {
                pNerve = GET_NERVE_ANON(StorySequenceExecutorStartSaveAfterSequence);
            }

            setNerve(pNerve);
        }

        break;
    default:
        break;
    }
}

void StorySequenceExecutor::exeWaitTimeKeepDemoEnd() {
    // A stage change can take the scene away while this waits (the sequence runs on between scenes): it waits on for the next.
    if (!MR::isExistSceneObj(SceneObj_DemoDirector) || MR::isDemoActive()) {
        return;
    }

    if (MR::isEqualString(getCurrentDemoInfo()->mName, "エピローグデモ")) {
        MR::offPlayerControl();
    }

    mDemoSequences[0]++;

    setNerve(GET_NERVE_ANON(StorySequenceExecutorPlayDemoSequence));
}

void StorySequenceExecutor::exeWaitSaveEnd() {
    if (GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        return;
    }

    MR::endDemo(mSaveObj, "セーブ");

    mDemoSequences[0]++;

    setNerve(GET_NERVE_ANON(StorySequenceExecutorPlayDemoSequence));
}

void StorySequenceExecutor::exeStartSaveAfterSequence() {
    if (tryStartSave()) {
        setNerve(GET_NERVE_ANON(StorySequenceExecutorWaitSaveEndAfterSequence));
    }
}

void StorySequenceExecutor::exeWaitSaveEndAfterSequence() {
    if (tryWaitSaveEnd()) {
        setNerve(GET_NERVE_ANON(StorySequenceExecutorFadeinAfterSequence));
    }
}

void StorySequenceExecutor::exeFadeinAfterSequence() {
    if (!MR::isFirstStep(this) || !MR::isWipeOpen()) {
        if (!tryStartFadein()) {
            return;
        }
    }

    mDemoSequences.clear();
    mIsSaveStarted = false;

    MR::endStartPosCamera();
    setNerve(GET_NERVE_ANON(StorySequenceExecutorIdle));
}

void StorySequenceExecutor::exeStaffRollSequence() {
    StaffRoll* pStaffRoll = MR::getStaffRoll();

    if (MR::isFirstStep(this)) {
        MR::deactivateDefaultGameLayout();
        MR::offPlayerControl();
        MR::openWipeFade();

        const StorySequenceExecutorType::DemoSequenceInfo* pDemoInfo = getCurrentDemoInfo();

        if (pDemoInfo->mEvent == Story::Event_StaffRoll) {
            pStaffRoll->appear();
        } else if (pDemoInfo->mEvent == Story::Event_StaffRollInfo) {
            pStaffRoll->startInfo();
        }
    }

    if (!pStaffRoll->isPauseOrEnd()) {
        return;
    }

    MR::closeWipeFade();

    mDemoSequences[0]++;

    setNerve(GET_NERVE_ANON(StorySequenceExecutorPlayDemoSequence));
}

void StorySequenceExecutor::decideNextEventForClearGalaxy(GalaxyMoveArgument* pMoveArgument) {
    pMoveArgument->mStageName = nullptr;
    pMoveArgument->mScenarioNo = -1;

    if (GameSequenceFunction::hasStageResultSequence()) {
        if (isEqualStageScenarioBefore("KoopaBattleVs3Galaxy", 1)) {
            if (MR::isStarCompleteAllGalaxy()) {
                prepareDemoSequence(::cDemoEpilogueComplete);
            } else {
                prepareDemoSequence(::cDemoEpilogueNormal);
            }

            pMoveArgument->mStageName = "EpilogueDemoStage";
            pMoveArgument->mScenarioNo = 1;
        } else {
            StageResultSequenceChecker stageResultSequenceChecker = StageResultSequenceChecker();
            stageResultSequenceChecker.check();

            if (!GameSequenceFunction::hasPowerStarYetAtResultSequence()) {
                if (isEqualStageScenarioBefore("HeavensDoorGalaxy", 1)) {
                    setNextStageToAstroGalaxy(pMoveArgument);
                    prepareDemoSequence(::cDemoGrandStar1ResultFirst);
                }

                if (isEqualStageScenarioBefore("EggStarGalaxy", 1)) {
                    prepareDemoSequence(::cDemoPowerStarResultAstroDomeWithSave);
                    setNextStageToAstroDome(pMoveArgument);
                }

                if (isEqualStageScenarioBefore("EggStarGalaxy", 2)) {
                    prepareDemoSequence(::cDemoNewGalaxyDiscover);
                    setNextStageToAstroDome(pMoveArgument);
                }

                if (GameDataFunction::isOnJustGameEventFlag("EventKinopioExplorerRescued")) {
                    prepareDemoSequence(::cDemoKinopioRescued);
                    setNextStageToAstroGalaxy(pMoveArgument);
                }

                if (isEqualStageScenarioBefore("TriLegLv1Galaxy", 1)) {
                    prepareDemoSequence(::cDemoTorchLecture);
                    setNextStageToAstroGalaxy(pMoveArgument);
                }

                if (GameDataFunction::isOnJustGameEventFlag("EventCometStarter")) {
                    prepareDemoSequence(::cDemoTrickCometLecture);
                    setNextStageToAstroGalaxy(pMoveArgument);
                }

                if (isEqualStageScenarioBefore("KoopaBattleVs1Galaxy", 1)) {
                    prepareDemoSequence(::cDemoRosettaTalkAboutKoopa);
                    setNextStageToAstroGalaxy(pMoveArgument);
                }

                if (GameDataFunction::isOnJustGameEventFlag("SpecialStarLuigiRescued")) {
                    prepareDemoSequence(::cDemoLuigiRescued);
                    setNextStageToAstroGalaxy(pMoveArgument);
                }

                if (isEqualStageScenarioBefore("KoopaJrShipLv1Galaxy", 1)) {
                    prepareDemoSequence(::cDemoRosettaTalkAstroDomeRecover);
                    setNextStageToAstroGalaxy(pMoveArgument);
                }

                if (isEqualStageScenarioBefore("KoopaBattleVs2Galaxy", 1)) {
                    prepareDemoSequence(::cDemoGrandStarReturn5);
                    setNextStageToAstroGalaxy(pMoveArgument);
                    prepareDemoSequence(::cDemoRosettaTalkCountDownStart);
                }

                if (isEqualStageScenarioBefore("FloaterOtaKingGalaxy", 1)) {
                    prepareDemoSequence(::cDemoGrandStarReturn6);
                    setNextStageToAstroGalaxy(pMoveArgument);

                    if (!GameDataFunction::canOnGameEventFlag("KoopaBattleVs3Galaxy")) {
                        prepareDemoSequence(::cDemoRosettaTalkTorchProgress);
                    }
                }

                if (stageResultSequenceChecker.isJustGetGreenStarFirst()) {
                    prepareDemoSequence(::cDemoGreenStarGetFirst);
                    setNextStageToAstroGalaxy(pMoveArgument);
                }

                if (GameDataFunction::isOnJustGameEventFlag("SpecialStarGreenAll")) {
                    prepareDemoSequence(::cDemoGreenStarRelease);
                    setNextStageToAstroGalaxy(pMoveArgument);
                }

                if (mDemoSequences.size() > 0) {
                    MR::InlineVector< const StorySequenceExecutorType::DemoSequenceInfo*, 8 > arr =
                        MR::InlineVector< const StorySequenceExecutorType::DemoSequenceInfo*, 8 >();

                    getOptionalDemoForClearGalaxy(&arr, pMoveArgument);
                    prepareDemoSequence(arr);
                    return;
                }
            }

            MR::InlineVector< const StorySequenceExecutorType::DemoSequenceInfo*, 8 > arr =
                MR::InlineVector< const StorySequenceExecutorType::DemoSequenceInfo*, 8 >();

            getOptionalDemoForClearGalaxy(&arr, pMoveArgument);

            if (GameDataConst::isGrandStar(mStageName, mScenarioNo)) {
                prepareDemoSequence(::cDemoGrandStarResult);
                setNextStageToAstroGalaxy(pMoveArgument);
            } else {
                if (arr.size() > 0) {
                    setNextStageToAstroGalaxy(pMoveArgument);
                } else {
                    setNextStageToAstroGalaxyOrDome(pMoveArgument);
                }

                if (pMoveArgument->isEqualStage("AstroDome")) {
                    prepareDemoSequence(::cDemoPowerStarResultAstroDome);
                } else {
                    prepareDemoSequence(::cDemoPowerStarResultAstroGalaxy);
                }
            }

            prepareDemoSequence(arr);
        }
    } else {
        mNextNerve = GET_NERVE_ANON(StorySequenceExecutorIdle);

        setNerve(GET_NERVE_ANON(StorySequenceExecutorWaitToSceneStart));
    }
}

void StorySequenceExecutor::decideNextEventForMoveGalaxy(GalaxyMoveArgument* pMoveArgument) {
    if (pMoveArgument->isEqualStage("AstroDome")) {
        if (!GameDataFunction::isPassedStoryEvent("バトラー情報Ａ")) {
            prepareDemoSequence(::cDemoAstroDomeLecture);
            return;
        }

        if (GameDataFunction::hasPowerStar("EggStarGalaxy", 1) && !GameDataFunction::isPassedStoryEvent("スターピースレクチャー")) {
            prepareDemoSequence(::cDemoStarPieceLectureRetry);
            return;
        }

        for (u32 i = 0; i < ARRAY_SIZE(::cDemoFortressDiscoverCheckListTable); i++) {
            const DemoFortressDiscoverCheckList& rCheckList = ::cDemoFortressDiscoverCheckListTable[i];
            s32 grandGalaxyId = GameDataConst::getIncludedGrandGalaxyId(rCheckList.mStageName);

            if (!GameDataFunction::canOnAndIsOffGameEventFlag(rCheckList.mStageName)) {
                continue;
            }

            if (GameDataFunction::isPassedStoryEvent(rCheckList.mStoryEventName)) {
                continue;
            }

            if (pMoveArgument->mScenarioNo != grandGalaxyId) {
                continue;
            }

            prepareDemoSequenceButlerFortressDiscover(pMoveArgument, rCheckList);
        }
    }

    if (pMoveArgument->isEqualStage("LibraryRoom") && GameDataFunction::getPictureBookChapterCanRead() != 0) {
        if (static_cast< s32 >(GameDataFunction::getPictureBookChapterCanRead()) !=
            static_cast< s32 >(GameDataFunction::getPictureBookChapterAlreadyRead())) {
            prepareDemoSequence(::cDemoRosettaPicureBookTalk);
        }
    }
}

void StorySequenceExecutor::decideNextEventForAfterLoading(GalaxyMoveArgument* /*pMoveArgument*/) {
    if (GameDataFunction::isOnGameEventFlag("RosettaTalkAfterNormalEnding")) {
        return;
    }

    if (GameDataFunction::canOnGameEventFlag("RosettaTalkAfterNormalEnding")) {
        prepareDemoSequence(::cDemoRosettaTalkAfterNormalEnding);
    }
}

void StorySequenceExecutor::decideNextStageForGalaxyOut(GalaxyMoveArgument* pMoveArgument) {
    if (MR::isEqualString("HeavensDoorGalaxy", mStageName)) {
        setNextStageToAstroGalaxy(pMoveArgument);

        pMoveArgument->mIDInfo = PlacementId(100, 0);
    } else {
        setNextStageToAstroGalaxyOrDome(pMoveArgument);
    }
}

void StorySequenceExecutor::overwriteGalaxyNameAfterLoading(GalaxyMoveArgument* pMoveArgument) {
    // A mod's journey: a new file starts where it says, a file with a star at its hub.
    if (NebulaMods::firstStage() != nullptr) {
        if (MR::getPowerStarNum() == 0 || NebulaMods::hubStage() == nullptr) {
            pMoveArgument->mStageName = NebulaMods::firstStage();
            pMoveArgument->mScenarioNo = NebulaMods::firstMission();
        } else {
            setNextStageToAstroGalaxy(pMoveArgument);
        }
        return;
    }
    if (GameDataFunction::hasGrandStar(1)) {
        setNextStageToAstroGalaxy(pMoveArgument);
    } else if (GameDataFunction::isPassedStoryEvent("ピーチ城浮上後") || !GameDataFunction::isDataMario()) {
        pMoveArgument->mStageName = "HeavensDoorGalaxy";
        pMoveArgument->mScenarioNo = 1;
    } else if (GameDataFunction::isPassedStoryEvent("クッパ襲来後")) {
        pMoveArgument->mStageName = "PeachCastleGardenGalaxy";
        pMoveArgument->mScenarioNo = 1;
        pMoveArgument->mIDInfo = PlacementId(1, 0);
    } else {
        prepareDemoSequence(::cDemoPrologue);

        pMoveArgument->mStageName = "PeachCastleGardenGalaxy";
        pMoveArgument->mScenarioNo = 1;
    }
}

void StorySequenceExecutor::setNextStageToAstroGalaxyOrDome(GalaxyMoveArgument* pMoveArgument) {
    s32 scenarioNo = calcAproposScenarioNoOnAstroDome();

    if (NebulaMods::hubStage() != nullptr) {
        setNextStageToAstroGalaxy(pMoveArgument);
    } else if (scenarioNo >= 1) {
        setNextStageToAstroDome(pMoveArgument);
    } else if (scenarioNo == 0) {
        setNextStageToAstroGalaxy(pMoveArgument);
    }
}

void StorySequenceExecutor::setNextStageToAstroGalaxy(GalaxyMoveArgument* pMoveArgument) {
    if (NebulaMods::hubStage() != nullptr) {
        pMoveArgument->mStageName = NebulaMods::hubStage();
        pMoveArgument->mScenarioNo = 1;
        return;
    }
    pMoveArgument->mStageName = "AstroGalaxy";
    pMoveArgument->mScenarioNo = calcAproposScenarioNoOnAstroGalaxy();
}

void StorySequenceExecutor::setNextStageToAstroDome(GalaxyMoveArgument* pMoveArgument) {
    pMoveArgument->mStageName = "AstroDome";
    pMoveArgument->mScenarioNo = calcAproposScenarioNoOnAstroDome();
}

const StorySequenceExecutorType::DemoSequenceInfo* StorySequenceExecutor::isExecuteDemoLuigiMissing(const GalaxyMoveArgument*) const {
    if (GameSequenceFunction::isLuigiDisappearFromAstroGalaxy() && !GameDataFunction::isOnGameEventFlag("SpecialStarFindingLuigi1")) {
        return ::cDemoLuigiMissing;
    }

    return nullptr;
}

void StorySequenceExecutor::getOptionalDemoForClearGalaxy(
    MR::InlineVector< const StorySequenceExecutorType::DemoSequenceInfo*, 8 >* pDemos, const GalaxyMoveArgument* pMoveArgument) const {
    const StorySequenceExecutorType::DemoSequenceInfo* pDemoInfo = isExecuteDemoLuigiMissing(pMoveArgument);

    if (pDemoInfo != nullptr) {
        pDemos->push_back(pDemoInfo);
    }

    if (GameDataFunction::canOnJustGameEventFlag("KoopaBattleVs3Galaxy") || GameDataFunction::isOnJustGameEventFlag("PowerStarComplete")) {
        pDemos->push_back(::cDemoRosettaTalkKoopaBattleVs3GalaxyAppear);
    }
}

bool StorySequenceExecutor::isEqualStageStopCometScheduler(const char* pStageName) {
    if (MR::isEqualString(pStageName, "AstroDome")) {
        return true;
    }

    if (MR::isEqualString(pStageName, "AstroGalaxy")) {
        return true;
    }

    if (MR::isEqualString(pStageName, "LibraryRoom")) {
        return true;
    }

    return false;
}

void StorySequenceExecutor::prepareDemoSequence(const StorySequenceExecutorType::DemoSequenceInfo* pDemoInfo) {
    if (NebulaMods::noStoryScenes()) {
        return;
    }

    mDemoSequences.push_back(pDemoInfo);

    mNextNerve = GET_NERVE_ANON(StorySequenceExecutorPlayDemoSequence);

    setNerve(GET_NERVE_ANON(StorySequenceExecutorWaitToSceneStart));
}

void StorySequenceExecutor::prepareDemoSequence(
    const MR::InlineVector< const StorySequenceExecutorType::DemoSequenceInfo*, 8 >& rDemos) {
    for (const StorySequenceExecutorType::DemoSequenceInfo* const* ppDemoInfo = rDemos.begin(); ppDemoInfo < rDemos.end(); ppDemoInfo++) {
        prepareDemoSequence(*ppDemoInfo);
    }
}

void StorySequenceExecutor::prepareDemoSequenceButlerFortressDiscover(const GalaxyMoveArgument* pMoveArgument,
                                                                      const DemoFortressDiscoverCheckList& rCheckList) {
    const StorySequenceExecutorType::DemoSequenceInfo* pDemoInfo =
        addDynamicDemoSequenceInfo(Story::Step_Demo, rCheckList.mIsKoopaFortress ? Story::Event_ButlerReportKoopaFortress : Story::Event_ButlerReportFortress,
                                   "バトラー報告");

    addDynamicDemoSequenceInfo(Story::Step_StoryEvent, Story::Event_None, rCheckList.mStoryEventName);

    if (pMoveArgument->mMoveType == GalaxyMoveArgument::Type_AfterStageClear) {
        addDynamicDemoSequenceInfo(Story::Step_EndSave, Story::Event_None, "");
    } else {
        addDynamicDemoSequenceInfo(Story::Step_Fadein, Story::Event_Demo, "Fadein");
        addDynamicDemoSequenceInfo(Story::Step_End, Story::Event_None, "");
    }

    prepareDemoSequence(pDemoInfo);
}

bool StorySequenceExecutor::tryStartDemo(const char* pDemoName) {
    return MR::tryStartTimeKeepDemoMarioPuppetable(mDemoObj, pDemoName, nullptr);
}

bool StorySequenceExecutor::tryStartSave() {
    if (MR::tryStartDemoWithoutCinemaFrameValidHandPointerFinger(mSaveObj, "セーブ")) {
        GameSequenceFunction::startGameDataSaveSequence(false, false);

        mIsSaveStarted = true;

        return true;
    }

    return false;
}

bool StorySequenceExecutor::tryWaitSaveEnd() {
    if (!GameSequenceFunction::isActiveSaveDataHandleSequence()) {
        MR::endDemo(mSaveObj, "セーブ");

        return true;
    }

    return false;
}

bool StorySequenceExecutor::tryStartFadein() {
    if (MR::isFirstStep(this)) {
        MR::openWipeFade();
    } else if (!MR::isWipeActive()) {
        return true;
    }

    return false;
}

bool StorySequenceExecutor::tryStartMovieAndWaitEnd(u32 movie) {
    if (MR::isFirstStep(this)) {
        switch (movie) {
        case Story::Event_MovieEpilogueA:
            MR::startMovieEpilogueA();
            break;
        case Story::Event_MovieEndingA:
            MR::startMovieEndingA();
            break;
        case Story::Event_MovieEndingB:
            MR::startMovieEndingB();
            break;
        default:
            break;
        }

        return false;
    }

    switch (movie) {
    case Story::Event_MovieEpilogueA:
        return MR::isEndMovieEpilogueA();
    case Story::Event_MovieEndingA:
        return MR::isEndMovieEndingA();
    case Story::Event_MovieEndingB:
        return MR::isEndMovieEndingB();
    default:
        return false;
    }
}

const StorySequenceExecutorType::DemoSequenceInfo* StorySequenceExecutor::getCurrentDemoInfo() const {
    if (mDemoSequences.size() == 0) {
        return nullptr;
    }

    return *mDemoSequences.begin();
}

bool StorySequenceExecutor::tryNextDemoInfo() {
    if (mDemoSequences.size() <= 1) {
        return false;
    }

    mDemoSequences.erase(mDemoSequences.begin());

    return true;
}

const StorySequenceExecutorType::DemoSequenceInfo* StorySequenceExecutor::addDynamicDemoSequenceInfo(u16 type, u16 event, const char* pName) {
    StorySequenceExecutorType::DemoSequenceInfo* pInfo = new StorySequenceExecutorType::DemoSequenceInfo();
    pInfo->mType = type;
    pInfo->mEvent = event;
    pInfo->mName = pName;
    mDynamicDemoInfos.push_back(pInfo);
    return pInfo;
}

void StorySequenceExecutor::setBeforeStageScenario(const GalaxyMoveArgument& rMoveArgument, bool noScenarioBefore) {
    const char* pStageName = nullptr;
    s32 scenarioNo = -1;

    if (MR::isCurrentScene(SceneKind_Game)) {
        if (rMoveArgument.mMoveType == GalaxyMoveArgument::Type_AfterStageClear) {
            pStageName = GameSequenceFunction::getClearedStageName();
            scenarioNo = GameSequenceFunction::getClearedPowerStarId();
        } else {
            pStageName = MR::getCurrentStageName();

            if (!noScenarioBefore) {
                scenarioNo = MR::getCurrentScenarioNo();
            }
        }
    }

    mStageName = MR::internName(pStageName);

    mScenarioNo = scenarioNo;
}

s32 StorySequenceExecutor::calcAproposScenarioNoOnAstroGalaxy() const {
    if (GameDataFunction::canOnGameEventFlag("KoopaBattleVs3Galaxy")) {
        return 5;
    }

    if (GameDataFunction::hasGrandStar(5)) {
        return 3;
    }

    if (GameDataFunction::hasGrandStar(3)) {
        return 2;
    }

    if (GameDataFunction::isOnJustGameEventFlag("SpecialStarGrand1")) {
        return 4;
    }

    return 1;
}

s32 StorySequenceExecutor::calcAproposScenarioNoOnAstroDome() const {
    return GameDataConst::getIncludedGrandGalaxyId(mStageName);
}
