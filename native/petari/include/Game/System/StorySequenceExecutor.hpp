#pragma once

#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/Array.hpp"

class GalaxyMoveArgument;
class NameObj;

class DemoFortressDiscoverCheckList {
public:
    const char* mStageName;
    const char* mStoryEventName;
    bool mIsKoopaFortress;
};

class StorySequenceExecutorType {
public:
    // What a step does; mEvent and mName are its operands.
    enum Step : u16 {
        Step_Demo,             // time-keep demo mName
        Step_Fadein,
        Step_Movie,            // movie mEvent
        Step_ChangeSceneTitle,
        Step_Save,
        Step_EventFlag,        // game event flag mName goes on
        Step_StoryEvent,       // story event mName is passed
        Step_Prologue,
        Step_StaffRoll,        // Event_StaffRoll or Event_StaffRollInfo
        Step_PointerMode,      // Event_PointerModeEnding
        Step_InputPermission,  // Event_InputOff or Event_InputOn
        Step_End,              // last step: idle
        Step_EndSave,          // last step: save unless saved, then fade in
    };

    // What the running step means to the scene (GameSequenceFunction::getExecutingStorySequenceEventNum).
    enum Event : u16 {
        Event_None,
        Event_Demo,
        Event_DomeLecture,
        Event_StarPieceLecture,
        Event_ButlerReportNewGalaxy,
        Event_ButlerReportFortress,
        Event_ButlerReportKoopaFortress,
        Event_ButlerGreenDriverFirst,
        Event_ButlerGreenDriverRelease,
        Event_MovieEpilogueA,
        Event_MovieEndingA,
        Event_MovieEndingB,
        Event_StaffRoll,
        Event_StaffRollInfo,
        Event_PointerModeEnding,
        Event_InputOff,
        Event_InputOn,
    };

    class DemoSequenceInfo {
    public:
        u16 mType;
        u16 mEvent = Event_None;  // the tables end a sequence with the step alone
        const char* mName = nullptr;
    };
};

class StorySequenceExecutor : public NerveExecutor {
public:
    StorySequenceExecutor();

    void update();
    bool isNeedMoviePlayerExecutingEventEnum() const;
    bool hasNextDemo() const;
    s32 getExecutingEventEnum() const;
    bool isEqualStageScenarioBefore(const char*, int) const;
    // noScenarioBefore: the stage left had no scenario (reset, or scenario select cancelled).
    void moveGalaxy(GalaxyMoveArgument*, bool noScenarioBefore);
    void setNerveSceneStart();
    void forceStop();
    void exeIdle();
    void exeWaitToSceneStart();
    void exePlayDemoSequence();
    void exeWaitTimeKeepDemoEnd();
    void exeWaitSaveEnd();
    void exeStartSaveAfterSequence();
    void exeWaitSaveEndAfterSequence();
    void exeFadeinAfterSequence();
    void exeStaffRollSequence();
    void decideNextEventForClearGalaxy(GalaxyMoveArgument*);
    void decideNextEventForMoveGalaxy(GalaxyMoveArgument*);
    void decideNextEventForAfterLoading(GalaxyMoveArgument*);
    void decideNextStageForGalaxyOut(GalaxyMoveArgument*);
    void overwriteGalaxyNameAfterLoading(GalaxyMoveArgument*);
    void setNextStageToAstroGalaxyOrDome(GalaxyMoveArgument*);
    void setNextStageToAstroGalaxy(GalaxyMoveArgument*);
    void setNextStageToAstroDome(GalaxyMoveArgument*);
    const StorySequenceExecutorType::DemoSequenceInfo* isExecuteDemoLuigiMissing(const GalaxyMoveArgument*) const;
    void getOptionalDemoForClearGalaxy(MR::InlineVector< const StorySequenceExecutorType::DemoSequenceInfo*, 8 >*,
                                       const GalaxyMoveArgument*) const;
    static bool isEqualStageStopCometScheduler(const char*);
    void prepareDemoSequence(const StorySequenceExecutorType::DemoSequenceInfo*);
    void prepareDemoSequence(const MR::InlineVector< const StorySequenceExecutorType::DemoSequenceInfo*, 8 >&);
    void prepareDemoSequenceButlerFortressDiscover(const GalaxyMoveArgument*, const DemoFortressDiscoverCheckList&);
    bool tryStartDemo(const char*);
    bool tryStartSave();
    bool tryWaitSaveEnd();
    bool tryStartFadein();
    bool tryStartMovieAndWaitEnd(u32 movie);
    const StorySequenceExecutorType::DemoSequenceInfo* getCurrentDemoInfo() const;
    bool tryNextDemoInfo();
    const StorySequenceExecutorType::DemoSequenceInfo* addDynamicDemoSequenceInfo(u16, u16, const char*);
    void setBeforeStageScenario(const GalaxyMoveArgument&, bool);
    s32 calcAproposScenarioNoOnAstroGalaxy() const;
    s32 calcAproposScenarioNoOnAstroDome() const;

    const Nerve* mNextNerve;
    NameObj* mDemoObj;
    NameObj* mSaveObj;
    const char* mStageName;  // MR::internName
    s32 mScenarioNo;
    MR::InlineVector< const StorySequenceExecutorType::DemoSequenceInfo*, 8 > mDemoSequences;
    // The steps made while moving (a butler's report): each made by new, so its address holds while mDemoSequences points at it.
    MR::InlineVector< StorySequenceExecutorType::DemoSequenceInfo*, 8 > mDynamicDemoInfos;
    bool mIsSaveStarted;
};
