#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <nebula_geometry.hpp>

class TableIter;
class LiveActor;
class SurfingGuidance;
class TalkMessageCtrl;

class SurfRayTutorial : public NerveExecutor {
public:
    SurfRayTutorial(LiveActor*, TalkMessageCtrl*, const TableIter&);

    bool update();
    void updateLessonStart(s32, void (SurfingGuidance::*)());
    void updateLessonFailure(bool (SurfRayTutorial::*)() const);
    void updateLessonUpkeep(bool (SurfRayTutorial::*)() const, void (SurfingGuidance::*)(), void (SurfingGuidance::*)(), const char*);
    void exeTutorialAllStart();
    void exeTutorialStraightStart();
    void exeTutorialStraightFailure();
    void exeTutorialStraightUpkeep();
    void exeTutorialStraightSuccess();
    void exeTutorialTurnLeftStart();
    void exeTutorialTurnLeftFailure();
    void exeTutorialTurnLeftUpkeep();
    void exeTutorialTurnLeftSuccess();
    void exeTutorialTurnRightStart();
    void exeTutorialTurnRightFailure();
    void exeTutorialTurnRightUpkeep();
    void exeTutorialTurnRightSuccess();
    void exeTutorialAllEnd();
    bool isSuccessStraight() const;
    bool isSuccessTurnLeft() const;
    bool isSuccessTurnRight() const;
    bool isFailureTwistLeftMore() const;
    bool isFailureTwistRightMore() const;
    bool isFailureStand() const;
    void omitTutorial() const;
    void nextTutorial();
    void prevTutorial();
    void startTimerSound(s32 step, s32 level);

    LiveActor* mHost;
    bool mIsTutorialPass;
    Vec3f mPadAccel;
    TalkMessageCtrl* mTalkCtrl;
    SurfingGuidance* mSurfingGuidance;
    s32 mChangeStep;
    s32 mGoNextCount;
};
