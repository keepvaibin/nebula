#pragma once

#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/Array.hpp"

class GalaxyMoveArgument;
class NameObj;

class DemoFortressDiscoverCheckList {
public:
    const char* mStageName;
    const char* mStoryEventName;
    bool _8;
};

class StorySequenceExecutorType {
public:
    class DemoSequenceInfo {
    public:
        u16 _0;
        u16 _2;
        const char* _4;
    };
};

class StorySequenceExecutor : public NerveExecutor {
public:
    /// @brief Creates a new `StorySequenceExecutor`.
    StorySequenceExecutor();

    void update();
    bool isNeedMoviePlayerExecutingEventEnum() const;
    bool hasNextDemo() const;
    s32 getExecutingEventEnum() const;
    bool isEqualStageScenarioBefore(const char*, int) const;
    void moveGalaxy(GalaxyMoveArgument*, bool);
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
    void getOptionalDemoForClearGalaxy(MR::Vector< MR::FixedArray< const StorySequenceExecutorType::DemoSequenceInfo*, 8 > >*,
                                       const GalaxyMoveArgument*) const;
    static bool isEqualStageStopCometScheduler(const char*);
    void prepareDemoSequence(const StorySequenceExecutorType::DemoSequenceInfo*);
    void prepareDemoSequence(const MR::Vector< MR::FixedArray< const StorySequenceExecutorType::DemoSequenceInfo*, 8 > >&);
    void prepareDemoSequenceButlerFortressDiscover(const GalaxyMoveArgument*, const DemoFortressDiscoverCheckList&);
    bool tryStartDemo(const char*);
    bool tryStartSave();
    bool tryWaitSaveEnd();
    bool tryStartFadein();
    bool tryStartMovieAndWaitEnd(u32);
    const StorySequenceExecutorType::DemoSequenceInfo* getCurrentDemoInfo() const;
    bool tryNextDemoInfo();
    const StorySequenceExecutorType::DemoSequenceInfo* addDynamicDemoSequenceInfo(u16, u16, const char*);
    void setBeforeStageScenario(const GalaxyMoveArgument&, bool);
    s32 calcAproposScenarioNoOnAstroGalaxy() const;
    s32 calcAproposScenarioNoOnAstroDome() const;

    const Nerve* mNextNerve;
    NameObj* mDemoObj;
    NameObj* mSaveObj;
    char mStageName[48];
    s32 mScenarioNo;
    MR::Vector< MR::FixedArray< const StorySequenceExecutorType::DemoSequenceInfo*, 8 > > _48;
    MR::Vector< MR::FixedArray< StorySequenceExecutorType::DemoSequenceInfo, 8 > > _6C;
    bool _B0;
};
