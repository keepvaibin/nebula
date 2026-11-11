#include "Game/Screen/NoteCounter.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeShowToHide_f1, "exeShowToHide.f1", 10.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeHideToShow_f1, "exeHideToShow.f1", 10.0f);

namespace {
    static const s32 sStepShowWait = NebulaFrameClock::stepsOfSeconds(3);
    NEBULA_TUNABLE(s32, sStepMove, 10);
    NEBULA_TUNABLE(f32, sTransMinY, -100.0f);
    NEBULA_TUNABLE(f32, sTransMaxY, 0.0f);
};  // namespace

namespace NrvNoteCounter {
    NEW_NERVE(NoteCounterNrvShow, NoteCounter, Show);
    NEW_NERVE(NoteCounterNrvShowWait, NoteCounter, ShowWait);
    NEW_NERVE(NoteCounterNrvShowToHide, NoteCounter, ShowToHide);
    NEW_NERVE(NoteCounterNrvHideToShow, NoteCounter, HideToShow);
    NEW_NERVE(NoteCounterNrvHide, NoteCounter, Hide);
};  // namespace NrvNoteCounter

NoteCounter::NoteCounter(const char* pName)
    : LayoutActor(pName, true), mNoteNum(), mNoteAddNum(), mNoteMissNum(), mMelodyNo(-1), mRailLength(), mHost() {
    lumacore::setClass(this, lumacore::kClassId< NoteCounter >);
}

void NoteCounter::init(const TableIter& /*rIter*/) {
    initNerve(GET_NERVE(NoteCounter, NoteCounterNrvHide));
    appear();
}

void NoteCounter::declareNoteNumMaxAndMelody(LiveActor* pHost, s32 noteNum, s32 melodyNo, f32 railLength) {
    mHost = pHost;
    mNoteNum = noteNum;
    mNoteAddNum = 0;
    mNoteMissNum = 0;
    mMelodyNo = melodyNo;
    mRailLength = railLength;

    if (isNerve(GET_NERVE(NoteCounter, NoteCounterNrvHide)) || isNerve(GET_NERVE(NoteCounter, NoteCounterNrvShowToHide))) {
        setNerve(GET_NERVE(NoteCounter, NoteCounterNrvHideToShow));
    } else if (isNerve(GET_NERVE(NoteCounter, NoteCounterNrvShowWait))) {
        setNerve(GET_NERVE(NoteCounter, NoteCounterNrvShow));
    }
}

void NoteCounter::add() {
    if (mMelodyNo == -2) {
        s32 note = mNoteAddNum;
        while (note >= 96) {
            note -= 4;
        }

        s32 pitchGroup = note / 4;
        MR::startSystemSE("SE_SY_FLOWER_GET_COMBO", pitchGroup + (note - pitchGroup * 4));
    } else if (mMelodyNo == -1) {
        s32 note = mNoteAddNum + 5;
        while (note > 36) {
            note -= 12;
        }

        MR::startSystemSE("SE_SY_FLOWER_GET_COMBO", note);
    } else if (mMelodyNo >= 0) {
        MR::startRemixSound(mMelodyNo, mNoteAddNum, mRailLength);
    }

    mNoteAddNum++;
    // A note taken before any host declared the count has no one to tell.
    if (mNoteAddNum >= mNoteNum && mHost != nullptr) {
        mHost->receiveMessage(0x66, MR::getMessageSensor(), MR::getMessageSensor());
    }

    tryEndDisp();
}

void NoteCounter::exeShow() {
}

void NoteCounter::exeShowWait() {
    if (MR::isStep(this, ::sStepShowWait)) {
        setNerve(GET_NERVE(NoteCounter, NoteCounterNrvShowToHide));
    }
}

void NoteCounter::exeShowToHide() {
    f32 y = MR::getEaseOutValue(getNerveStep() / cNum_exeShowToHide_f1, ::sTransMaxY, ::sTransMinY, 1.0f);
    setTrans(Vec2f(getTrans().x, y));

    if (MR::isStep(this, ::sStepMove)) {
        setNerve(GET_NERVE(NoteCounter, NoteCounterNrvHide));
    }
}

void NoteCounter::exeHideToShow() {
    if (MR::isFirstStep(this)) {
        MR::showLayout(this);
    }

    f32 y = MR::getEaseOutValue(getNerveStep() / cNum_exeHideToShow_f1, ::sTransMinY, ::sTransMaxY, 1.0f);
    setTrans(Vec2f(getTrans().x, y));

    if (MR::isStep(this, ::sStepMove)) {
        setNerve(GET_NERVE(NoteCounter, NoteCounterNrvShow));
    }
}

void NoteCounter::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::hideLayout(this);
    }
}

void NoteCounter::tryEndDisp() {
    if (mNoteAddNum + mNoteMissNum < mNoteNum) {
        return;
    }

    setNerve(GET_NERVE(NoteCounter, NoteCounterNrvShowWait));
}

namespace MR {
    void declareNoteNumMaxAndMelodyToCounter(LiveActor* pHost, s32 noteNum, s32 melodyNo, f32 railLength) {
        NoteCounter* pNoteCounter = MR::getGameSceneLayoutHolder()->mNoteCounter;

        pNoteCounter->declareNoteNumMaxAndMelody(pHost, noteNum, melodyNo, railLength);
    }

    void addNoteToCounter() {
        NoteCounter* pNoteCounter = MR::getGameSceneLayoutHolder()->mNoteCounter;

        pNoteCounter->add();
    }

    void failureNoteCollection() {
        NoteCounter* pNoteCounter = MR::getGameSceneLayoutHolder()->mNoteCounter;

        pNoteCounter->mNoteMissNum++;
        pNoteCounter->tryEndDisp();
    }
};  // namespace MR
