#include "Game/Ride/SurfRayTutorial.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/SurfingGuidance.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/TalkUtil.hpp"

namespace {
    static const s32 sStepTalk = NebulaFrameClock::stepsOfMilliseconds(2500);
    static const s32 sStepToSuccess = NebulaFrameClock::stepsOfMilliseconds(1500);
    const s32 sStepToOK = 10;
    const f32 sPadAccelStraightMinX = 0.25f;
    const f32 sPadAccelStraightMinY = 0.45f;
    const f32 sPadAccelTurnMinX = 0.65f;
    const f32 sPadAccelTurnLimitY = 0.5f;
};  // namespace

namespace NrvSurfRayTutorial {
    NEW_NERVE(SurfRayTutorialNrvTutorialAllStart, SurfRayTutorial, TutorialAllStart);
    NEW_NERVE(SurfRayTutorialNrvTutorialStraightStart, SurfRayTutorial, TutorialStraightStart);
    NEW_NERVE(SurfRayTutorialNrvTutorialStraightFailure, SurfRayTutorial, TutorialStraightFailure);
    NEW_NERVE(SurfRayTutorialNrvTutorialStraightUpkeep, SurfRayTutorial, TutorialStraightUpkeep);
    NEW_NERVE(SurfRayTutorialNrvTutorialStraightSuccess, SurfRayTutorial, TutorialStraightSuccess);
    NEW_NERVE(SurfRayTutorialNrvTutorialTurnLeftStart, SurfRayTutorial, TutorialTurnLeftStart);
    NEW_NERVE(SurfRayTutorialNrvTutorialTurnLeftFailure, SurfRayTutorial, TutorialTurnLeftFailure);
    NEW_NERVE(SurfRayTutorialNrvTutorialTurnLeftUpkeep, SurfRayTutorial, TutorialTurnLeftUpkeep);
    NEW_NERVE(SurfRayTutorialNrvTutorialTurnLeftSuccess, SurfRayTutorial, TutorialTurnLeftSuccess);
    NEW_NERVE(SurfRayTutorialNrvTutorialTurnRightStart, SurfRayTutorial, TutorialTurnRightStart);
    NEW_NERVE(SurfRayTutorialNrvTutorialTurnRightFailure, SurfRayTutorial, TutorialTurnRightFailure);
    NEW_NERVE(SurfRayTutorialNrvTutorialTurnRightUpkeep, SurfRayTutorial, TutorialTurnRightUpkeep);
    NEW_NERVE(SurfRayTutorialNrvTutorialTurnRightSuccess, SurfRayTutorial, TutorialTurnRightSuccess);
    NEW_NERVE(SurfRayTutorialNrvTutorialAllEnd, SurfRayTutorial, TutorialAllEnd);
};  // namespace NrvSurfRayTutorial

namespace {
    // The lesson order; nextTutorial steps one along it and stops at the end.
    const Nerve* const sTutorialOrder[] = {
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialAllStart),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialStraightStart),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialStraightFailure),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialStraightUpkeep),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialStraightSuccess),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnLeftStart),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnLeftFailure),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnLeftUpkeep),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnLeftSuccess),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnRightStart),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnRightFailure),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnRightUpkeep),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnRightSuccess),
        GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialAllEnd),
    };
};  // namespace

SurfRayTutorial::SurfRayTutorial(LiveActor* pHost, TalkMessageCtrl* pTalkCtrl, const TableIter& rIter)
    : NerveExecutor("チュートリアル演出"), mHost(pHost), mIsTutorialPass(), mPadAccel(0.0f, 0.0f, 0.0f), mTalkCtrl(pTalkCtrl), mChangeStep(), mGoNextCount() {
    initNerve(GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialAllStart));

    mSurfingGuidance = new SurfingGuidance();
    mSurfingGuidance->init(rIter);
}

bool SurfRayTutorial::update() {
    MR::getMainTilt(&mPadAccel, PLAYER_1);
    updateNerve();

    return mIsTutorialPass;
}

void SurfRayTutorial::exeTutorialAllStart() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::sendMsgToGroupMember(ACTMES_TUTORIAL_START, mHost, MR::getSensor(mHost, 0), "body");
        MR::resetAndForwardNode(mTalkCtrl, 0);
    }

    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mTalkCtrl)) {
        nextTutorial();
        mSurfingGuidance->activate();
    }
}

// The lesson's talk, then on to its failure step after sStepTalk.
void SurfRayTutorial::updateLessonStart(s32 talkNode, void (SurfingGuidance::*pReady)()) {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::resetAndForwardNode(mTalkCtrl, talkNode);
        (mSurfingGuidance->*pReady)();
    }

    MR::tryTalkForceWithoutDemoMarioPuppetable(mTalkCtrl);

    if (MR::isGreaterStep(this, ::sStepTalk)) {
        nextTutorial();
    }
}

// On to the upkeep step once the pose has held sStepToOK steps.
void SurfRayTutorial::updateLessonFailure(bool (SurfRayTutorial::*pIsSuccess)() const) {
    if (MR::isFirstStep(this)) {
        mChangeStep = 0;
    }

    if ((this->*pIsSuccess)()) {
        if (mChangeStep++ == ::sStepToOK) {
            nextTutorial();
        }
    } else {
        mChangeStep = 0;
    }

    MR::tryTalkForceWithoutDemoMarioPuppetable(mTalkCtrl);
}

// The pose held sStepToSuccess steps passes the lesson; lost for sStepToOK steps, back to the failure step.
void SurfRayTutorial::updateLessonUpkeep(bool (SurfRayTutorial::*pIsSuccess)() const, void (SurfingGuidance::*pHold)(),
                                         void (SurfingGuidance::*pReady)(), const char* pGoNextSe) {
    if (MR::isFirstStep(this)) {
        mChangeStep = 0;

        MR::tryRumblePadWeak(this, PLAYER_1);
        MR::startSystemSE("SE_SY_SURF_TUTORIAL_OK");
        (mSurfingGuidance->*pHold)();
    }

    startTimerSound(getNerveStep(), mGoNextCount);

    if ((this->*pIsSuccess)()) {
        mChangeStep = 0;

        if (MR::isStep(this, ::sStepToSuccess)) {
            MR::startSystemSE(pGoNextSe, mGoNextCount);

            mGoNextCount++;

            nextTutorial();
            return;
        }
    } else if (mChangeStep++ == ::sStepToOK) {
        prevTutorial();
        MR::startSystemSE("SE_SY_SURF_TUTORIAL_NG");
        (mSurfingGuidance->*pReady)();
    }

    MR::tryTalkForceWithoutDemoMarioPuppetable(mTalkCtrl);
}

void SurfRayTutorial::exeTutorialStraightStart() {
    updateLessonStart(1, &SurfingGuidance::levelOffReady);
}

void SurfRayTutorial::exeTutorialStraightFailure() {
    updateLessonFailure(&SurfRayTutorial::isSuccessStraight);
}

void SurfRayTutorial::exeTutorialStraightUpkeep() {
    updateLessonUpkeep(&SurfRayTutorial::isSuccessStraight, &SurfingGuidance::levelOffHold, &SurfingGuidance::levelOffReady,
                       "SE_SY_SURF_TUTORIAL_GONEXT");
}

void SurfRayTutorial::exeTutorialStraightSuccess() {
    if (MR::isFirstStep(this)) {
        mSurfingGuidance->levelOffSuccess();
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::resetAndForwardNode(mTalkCtrl, 2);
    }

    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mTalkCtrl)) {
        nextTutorial();
    }
}

void SurfRayTutorial::exeTutorialTurnLeftStart() {
    updateLessonStart(3, &SurfingGuidance::turnLeftReady);
}

void SurfRayTutorial::exeTutorialTurnLeftFailure() {
    updateLessonFailure(&SurfRayTutorial::isSuccessTurnLeft);
}

void SurfRayTutorial::exeTutorialTurnLeftUpkeep() {
    updateLessonUpkeep(&SurfRayTutorial::isSuccessTurnLeft, &SurfingGuidance::turnLeftHold, &SurfingGuidance::turnLeftReady,
                       "SE_SY_SURF_TUTORIAL_GONEXT");
}

void SurfRayTutorial::exeTutorialTurnLeftSuccess() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::resetAndForwardNode(mTalkCtrl, 4);
        mSurfingGuidance->turnLeftSuccess();
    }

    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mTalkCtrl)) {
        nextTutorial();
    }
}

void SurfRayTutorial::exeTutorialTurnRightStart() {
    updateLessonStart(5, &SurfingGuidance::turnRightReady);
}

void SurfRayTutorial::exeTutorialTurnRightFailure() {
    updateLessonFailure(&SurfRayTutorial::isSuccessTurnRight);
}

void SurfRayTutorial::exeTutorialTurnRightUpkeep() {
    updateLessonUpkeep(&SurfRayTutorial::isSuccessTurnRight, &SurfingGuidance::turnRightHold, &SurfingGuidance::turnRightReady,
                       "SE_SY_SURF_TUTORIAL_GONEXT2");
}

void SurfRayTutorial::exeTutorialTurnRightSuccess() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::resetAndForwardNode(mTalkCtrl, 6);
        mSurfingGuidance->turnRightSuccess();
    }

    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mTalkCtrl)) {
        nextTutorial();
    }
}

void SurfRayTutorial::exeTutorialAllEnd() {
    if (MR::isFirstStep(this)) {
        MR::tryRumblePadMiddle(this, PLAYER_1);
        MR::resetAndForwardNode(mTalkCtrl, 7);
        mSurfingGuidance->deactivate();
    }

    if (MR::tryTalkForceWithoutDemoMarioPuppetableAtEnd(mTalkCtrl)) {
        MR::sendMsgToGroupMember(ACTMES_TUTORIAL_PASS, mHost, MR::getSensor(mHost, 0), "body");

        mIsTutorialPass = true;
    }
}

bool SurfRayTutorial::isSuccessStraight() const {
    return MR::abs(mPadAccel.x) < ::sPadAccelStraightMinX && MR::abs(mPadAccel.y) < ::sPadAccelStraightMinY;
}

bool SurfRayTutorial::isSuccessTurnLeft() const {
    return !isFailureTwistLeftMore() && !isFailureStand();
}

bool SurfRayTutorial::isSuccessTurnRight() const {
    return !isFailureTwistRightMore() && !isFailureStand();
}

bool SurfRayTutorial::isFailureTwistLeftMore() const {
    return mPadAccel.x > -::sPadAccelTurnMinX;
}

bool SurfRayTutorial::isFailureTwistRightMore() const {
    return mPadAccel.x < ::sPadAccelTurnMinX;
}

bool SurfRayTutorial::isFailureStand() const {
    return mPadAccel.y < -::sPadAccelTurnLimitY;
}

void SurfRayTutorial::omitTutorial() const {
    MR::sendMsgToGroupMember(ACTMES_TUTORIAL_OMIT, mHost, MR::getSensor(mHost, 0), "body");
}

void SurfRayTutorial::nextTutorial() {
    const s32 last = sizeof(::sTutorialOrder) / sizeof(::sTutorialOrder[0]) - 1;
    for (s32 i = 0; i < last; i++) {
        if (isNerve(::sTutorialOrder[i])) {
            setNerve(::sTutorialOrder[i + 1]);
            break;
        }
    }

    MR::sendMsgToGroupMember(ACTMES_TUTORIAL_NEXT, mHost, MR::getSensor(mHost, 0), "body");
}

void SurfRayTutorial::prevTutorial() {
    if (isNerve(GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialStraightUpkeep))) {
        setNerve(GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialStraightFailure));
    } else if (isNerve(GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnLeftUpkeep))) {
        setNerve(GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnLeftFailure));
    } else if (isNerve(GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnRightUpkeep))) {
        setNerve(GET_NERVE(SurfRayTutorial, SurfRayTutorialNrvTutorialTurnRightFailure));
    }

    MR::sendMsgToGroupMember(ACTMES_TUTORIAL_PREV, mHost, MR::getSensor(mHost, 0), "body");
}

void SurfRayTutorial::startTimerSound(s32 step, s32 level) {
    if (step == 20) {
        MR::startSystemSE("SE_SY_SURF_TUTORIAL_TIMER", level);
    }

    if (step == 43) {
        MR::startSystemSE("SE_SY_SURF_TUTORIAL_TIMER2", level);
    }

    if (step == 66) {
        MR::startSystemSE("SE_SY_SURF_TUTORIAL_TIMER3", level);
    }
}
