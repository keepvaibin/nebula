#pragma once

#include "Game/System/NerveExecutor.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class JMapInfoIter;
class LiveActor;
class SurfingGuidance;
class TalkMessageCtrl;

class SurfRayTutorial : public NerveExecutor {
public:
    SurfRayTutorial(LiveActor*, TalkMessageCtrl*, const JMapInfoIter&);

    bool update();
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
    void startTimerSound(s32, s32);

    LiveActor* mHost;
    bool mIsTutorialPass;
    TVec3f mPadAccel;
    TalkMessageCtrl* mTalkCtrl;
    SurfingGuidance* mSurfingGuidance;
    s32 mChangeStep;
    s32 _28;
};
