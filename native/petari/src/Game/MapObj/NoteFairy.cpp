#include "Game/MapObj/NoteFairy.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/Note.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/NoteCounter.hpp"
#include "Game/Util.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvNoteFairy {
    NEW_NERVE(NoteFairyNrvHide, NoteFairy, Hide);
    NEW_NERVE(NoteFairyNrvStartAppearDemo, NoteFairy, StartAppearDemo);
    NEW_NERVE(NoteFairyNrvRailMoveStart, NoteFairy, RailMoveStart);
    NEW_NERVE(NoteFairyNrvDemoAppear, NoteFairy, DemoAppear);
    NEW_NERVE(NoteFairyNrvAppearNoteBloom, NoteFairy, AppearNoteBloom);
    NEW_NERVE(NoteFairyNrvAppearNote, NoteFairy, AppearNote);
    NEW_NERVE(NoteFairyNrvEndWait, NoteFairy, EndWait);
    NEW_NERVE(NoteFairyNrvSuccessDemo, NoteFairy, SuccessDemo);
    NEW_NERVE(NoteFairyNrvSuccess, NoteFairy, Success);
};  // namespace NrvNoteFairy

NoteFairy::NoteFairy(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< NoteFairy >);
    mInitPos.x = 0.0f;
    mInitPos.y = 0.0f;
    mInitPos.z = 0.0f;
    mDemoJumpPos.x = 0.0f;
    mDemoJumpPos.y = 0.0f;
    mDemoJumpPos.z = 0.0f;
    mAppearanceSpeed = 13.0f;
    mTimeLimit = 20;
    mMelodyNoteNum = 0;
    mAppearedNoteNum = 0;
    mSong = -1;
    mCompletionJingle = -1;
    mNoteCoord = 150.0f;
    mNoteArray = nullptr;
    mLastGotNote = nullptr;
    mAppearanceType = -1;
    mIsDemoUntilAllNotes = false;
    mIsNoDemo = false;
    mHasDemo = false;
    mIsLowerBgm = false;
    mIsNoRiddleSE = false;
    mIsNoteAtRailPoints = false;
    mPowerStarSpawnType = -1;
    mCameraInfo = nullptr;
}

void NoteFairy::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    s32 noteNum = -1;
    MR::getPlacementArg0WithInit(rIter, &noteNum);
    MR::getPlacementArg1NoInit(rIter, &mAppearanceSpeed);
    MR::getPlacementArg2NoInit(rIter, &mTimeLimit);
    mAppearanceType = -1;
    MR::getPlacementArg3WithInit(rIter, &mAppearanceType);

    switch (mAppearanceType) {
    case -1:
        break;
    case 1:
        break;
    case 3:
        mIsDemoUntilAllNotes = 1;
        break;
    case 5:
        mIsNoDemo = 1;
        break;
    case 6:
        mIsNoDemo = 1;
        mIsNoRiddleSE = 1;
        break;
    }

    MR::getPlacementArg4WithInit(rIter, &mSong);
    MR::getPlacementArg5WithInit(rIter, &mCompletionJingle);
    s32 keepBgm;
    MR::getPlacementArg6WithInit(rIter, &keepBgm);

    if (keepBgm > 0) {
        mIsLowerBgm = false;
    } else {
        mIsLowerBgm = true;
    }

    s32 powerStarType = 0;
    MR::getPlacementArg7NoInit(rIter, &powerStarType);

    if (powerStarType != 0) {
        MR::declarePowerStar(this);

        if (powerStarType == 2) {
            mPowerStarSpawnType = 1;
        } else if (powerStarType == 3) {
            mPowerStarSpawnType = 2;
        } else {
            mPowerStarSpawnType = 0;
        }
    }

    mInitPos.set(mPosition);
    mTimeLimit *= NebulaFrameClock::stepsOfSeconds(1);
    mHasDemo = MR::tryRegisterDemoCast(this, rIter);
    initRailRider(rIter);
    initModelManagerWithAnm("Note", nullptr, false);
    MR::startBrk(this, "Note");
    MR::connectToSceneNoSilhouettedMapObjStrongLight(this);
    initEffectKeeper(0, "NoteFairy");
    initSound(6, false);
    MR::initShadowVolumeSphere(this, 50.0f);
    s32 railArg = -1;
    MR::getRailArg0WithInit(this, &railArg);

    if (noteNum > 0) {
        if (mSong < 0) {
            mMelodyNoteNum = noteNum;
        } else {
            mMelodyNoteNum = MR::getRemixMelodyNoteNum(mSong);
        }

        mNoteCoord = MR::getRailTotalLength(this) / mMelodyNoteNum;
    } else {
        if (noteNum == -2) {
            s32 pointNum = MR::getRailPointNum(this);
            if (mSong < 0) {
                mMelodyNoteNum = pointNum;
            } else {
                mMelodyNoteNum = MR::getRemixMelodyNoteNum(mSong);
            }

            if (railArg < 0) {
                mNoteCoord = 200.0f;
            } else {
                mNoteCoord = railArg;
            }

            mIsNoteAtRailPoints = true;
        } else {
            if (mSong < 0) {
                mMelodyNoteNum = MR::getRailTotalLength(this) / mNoteCoord;
            } else {
                mMelodyNoteNum = MR::getRemixMelodyNoteNum(mSong);
                mNoteCoord = MR::getRailTotalLength(this) / mMelodyNoteNum;
            }
        }
    }

    // A negative count (a note spacing under zero) is no notes.
    if (mMelodyNoteNum < 0) {
        mMelodyNoteNum = 0;
    }

    // A note spacing near zero (a mod's) would make millions of notes: the game's longest runs are far under this.
    if (mMelodyNoteNum > 1024) {
        mMelodyNoteNum = 1024;
    }

    mNoteArray = new Note*[mMelodyNoteNum];

    for (s32 i = 0; i < mMelodyNoteNum; i++) {
        Vec3f notePos;

        if (mIsNoteAtRailPoints) {
            MR::calcRailPointPos(&notePos, this, i);
        } else {
            notePos.set(MR::getRailPos(this));
            MR::moveCoord(this, mNoteCoord);
        }

        mNoteArray[i] = new Note("音符", MR::getRailDirection(this), this);
        mNoteArray[i]->mPosition.set(notePos);
        mNoteArray[i]->initWithoutIter();
        mNoteArray[i]->mCounter = mTimeLimit;
    }

    MR::moveCoordToStartPos(this);
    MR::setRailCoord(this, 0.0f);
    Vec3f jumpOffset(MR::getRailDirection(this));
    jumpOffset.y = 0.0f;
    MR::normalizeOrZero(&jumpOffset);
    jumpOffset.x *= -800.0f;
    jumpOffset.z *= -800.0f;
    mDemoJumpPos.x = jumpOffset.x + mPosition.x;
    mDemoJumpPos.y = (0.5f * mPosition.y) + (0.5f * MR::getRailPos(this).y);
    mDemoJumpPos.z = jumpOffset.z + mPosition.z;
    MR::needStageSwitchReadAppear(this, rIter);
    MR::useStageSwitchWriteA(this, rIter);
    MR::useStageSwitchReadB(this, rIter);
    MR::initActorCamera(this, rIter, &mCameraInfo);
    initNerve(GET_NERVE(NoteFairy, NoteFairyNrvHide));
    makeActorAppeared();
}

void NoteFairy::control() {
    if (!isNerve(GET_NERVE(NoteFairy, NoteFairyNrvHide)) && MR::isValidSwitchB(this) && !MR::isOnSwitchB(this)) {
        kill();
    } else if (MR::isPowerStarGetDemoActive()) {
        kill();
    }
}

void NoteFairy::kill() {
    if (mIsLowerBgm) {
        MR::moveVolumeStageBGMForNoteFairy(1.0f, 120);
    }

    killAllNotes();
    LiveActor::kill();
}

void NoteFairy::killAllNotes() {
    for (s32 i = 0; i < mMelodyNoteNum; i++) {
        mNoteArray[i]->kill();
    }
}

void NoteFairy::exeHide() {
    if (MR::isFirstStep(this)) {
        MR::hideModel(this);
        MR::invalidateClipping(this);
    }

    if (MR::isOnSwitchAppear(this)) {
        if (!mIsNoRiddleSE) {
            MR::startSystemSE("SE_SY_READ_RIDDLE_S");
        }

        MR::invalidateClipping(this);

        if (mIsLowerBgm) {
            MR::moveVolumeStageBGMForNoteFairy(0.0f, 30);
        }

        switch (mAppearanceType) {
        case 1:
        case 2:
        case 3:
            setNerve(GET_NERVE(NoteFairy, NoteFairyNrvStartAppearDemo));
            return;
        case 4:
        case 5:
        case 6:
            setNerve(GET_NERVE(NoteFairy, NoteFairyNrvRailMoveStart));
            return;
        }

        setNerve(GET_NERVE(NoteFairy, NoteFairyNrvAppearNote));
    }
}

void NoteFairy::exeStartAppearDemo() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::emitEffect(this, "Blur");
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        Vec3f* pos = MR::getPlayerPos();
        Vec3f toFairy(mInitPos);
        toFairy.sub(*pos);
        MR::setPlayerFrontVec(toFairy, 1);
        enterDemoAppear(GET_NERVE(NoteFairy, NoteFairyNrvDemoAppear), true);
    }
}

void NoteFairy::exeRailMoveStart() {
    if (MR::isFirstStep(this)) {
        MR::showModel(this);
        MR::emitEffect(this, "Blur");
        mPosition = MR::getRailPos(this);
    }

    if (mIsNoDemo) {
        setNerve(GET_NERVE(NoteFairy, NoteFairyNrvAppearNoteBloom));
    } else if (MR::isStep(this, 0)) {
        enterDemoAppear(GET_NERVE(NoteFairy, NoteFairyNrvAppearNoteBloom), false);
    }
}

void NoteFairy::exeDemoAppear() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "Blur");
    }

    if (MR::isLessEqualStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        f32 ease = MR::getEaseOutValue(NebulaFrameClock::secondsOfSteps(getNerveStep()), 0.0f, 1.0f, 1.0f);
        mPosition.x = (mInitPos.x * (1.0f - ease)) + (mDemoJumpPos.x * ease);
        mPosition.y = (((mInitPos.y * (1.0f - ease)) + (mDemoJumpPos.y * ease)) - (300.0f * MR::sinDegree(180.0f * ease)));
        mPosition.z = (mInitPos.z * (1.0f - ease)) + (mDemoJumpPos.z * ease);
    }

    if (MR::isGreaterStep(this, 70)) {
        f32 ease = MR::getEaseInValue(NebulaFrameClock::secondsOfSteps(getNerveStep() - 70), 0.0f, 1.0f, 1.0f);
        mPosition.x = (mDemoJumpPos.x * (1.0f - ease)) + (ease * MR::getRailPos(this).x);
        mPosition.y = mDemoJumpPos.y - (MR::sinDegree(90.0f * ease) * (mDemoJumpPos.y - MR::getRailPos(this).y));
        mPosition.z = (mDemoJumpPos.z * (1.0f - ease)) + (ease * MR::getRailPos(this).z);
    }

    MR::startLevelSound(this, "SE_OJ_LV_FLOWER_FAIRY_MOVE");
    mRotation.y = MR::getSceneObj< NoteGroup >(SceneObj_NoteGroup)->mRotation;

    if (MR::isStep(this, 130)) {
        setNerve(GET_NERVE(NoteFairy, NoteFairyNrvAppearNoteBloom));
    }
}

void NoteFairy::exeAppearNoteBloom() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        MR::declareNoteNumMaxAndMelodyToCounter(this, mMelodyNoteNum, mSong, mNoteCoord);
    }

    if (!mIsDemoUntilAllNotes && !mIsNoDemo && MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1)) && mAppearanceType != -1) {
        MR::endDemo(this, "出現");
        MR::endActorCamera(this, mCameraInfo, false);
    }

    MR::startLevelSound(this, "SE_OJ_LV_FLOWER_FAIRY_MOVE");
    MR::moveCoordAndFollowTrans(this, mAppearanceSpeed);
    mRotation.y = MR::getSceneObj< NoteGroup >(SceneObj_NoteGroup)->mRotation;

    while (mAppearedNoteNum < mMelodyNoteNum && MR::getRailCoord(this) >= getNoteCoord(mAppearedNoteNum)) {
        mNoteArray[mAppearedNoteNum]->appear();
        mNoteArray[mAppearedNoteNum]->mBrkFrame = 5.0f * mAppearedNoteNum;

        if (MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(1)) && mAppearanceType != -1) {
            MR::requestMovementOn(mNoteArray[mAppearedNoteNum]);
        }

        mAppearedNoteNum++;
    }

    if (mAppearedNoteNum >= mMelodyNoteNum) {
        if (mIsDemoUntilAllNotes) {
            MR::endDemo(this, "出現");
            MR::endActorCamera(this, mCameraInfo, false);
        }

        MR::hideModel(this);
        MR::deleteEffect(this, "Blur");
        setNerve(GET_NERVE(NoteFairy, NoteFairyNrvEndWait));
    }
}

void NoteFairy::exeAppearNote() {
    if (MR::isFirstStep(this)) {
        mVelocity.zero();
        MR::declareNoteNumMaxAndMelodyToCounter(this, mMelodyNoteNum, mSong, mNoteCoord);

        for (s32 i = 0; i < mMelodyNoteNum; i++) {
            mNoteArray[i]->appear();
            mNoteArray[i]->mBrkFrame = 5.0f * i;
        }

        setNerve(GET_NERVE(NoteFairy, NoteFairyNrvEndWait));
    }
}

void NoteFairy::exeEndWait() {
    if (MR::isFirstStep(this)) {
        for (s32 i = 0; i < mMelodyNoteNum; i++) {
            mNoteArray[i]->startCountDown();
        }
    }

    MR::startLevelSound(this, "SE_SY_NO_SOUND_FANFARE");
    s32 timeLeft = mTimeLimit - getNerveStep();

    if (timeLeft <= 170) {
        MR::startSystemLevelSE("SE_RS_LV_NOTE_TIMER_FAST", 0);
    } else if (timeLeft <= mTimeLimit / 2) {
        MR::startSystemLevelSE("SE_RS_LV_NOTE_TIMER_MIDDLE", 30);
    } else {
        MR::startSystemLevelSE("SE_RS_LV_NOTE_TIMER_SLOW", 85);
    }

    if (isDeadAllNotes()) {
        kill();
    }
}

void NoteFairy::exeSuccessDemo() {
    if (MR::isFirstStep(this)) {
        MR::tryStartDemoRegistered(this, nullptr);
    }

    if (!MR::isTimeKeepDemoActive()) {
        setNerve(GET_NERVE(NoteFairy, NoteFairyNrvSuccess));
    }
}

void NoteFairy::exeSuccess() {
    if (MR::isFirstStep(this)) {
        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }

        switch (mPowerStarSpawnType) {
        case 1:
            MR::requestAppearPowerStar(this, mLastGotNote->mPosition);
            break;
        case 2:
            MR::requestAppearPowerStar(this, mLastGotNote->mPosition);
            break;
        case 0:
            MR::requestAppearPowerStar(this, mInitPos);
            break;
        }

        setNerve(GET_NERVE(NoteFairy, NoteFairyNrvEndWait));
    }
}

void NoteFairy::enterDemoAppear(const Nerve* pNerve, bool hasNoFrame) {
    MR::startActorCameraTargetSelf(this, mCameraInfo);

    if (hasNoFrame) {
        MR::requestStartDemo(this, "出現", pNerve, nullptr);
    } else {
        MR::requestStartDemoWithoutCinemaFrame(this, "出現", pNerve, nullptr);
    }

    MR::requestMovementOn(MR::getSceneObj< NoteGroup >(SceneObj_NoteGroup));
    MR::requestMovementOn(this);
}

bool NoteFairy::isDeadAllNotes() {
    for (s32 i = 0; i < mMelodyNoteNum; i++) {
        if (!MR::isDead(mNoteArray[i])) {
            return false;
        }
    }

    return true;
}

void NoteFairy::setLastGotNote(Note* pNote) {
    mLastGotNote = pNote;
}

f32 NoteFairy::getNoteCoord(s32 val) {
    if (mIsNoteAtRailPoints) {
        return MR::getRailPointCoord(this, val);
    }

    return val * mNoteCoord;
}

bool NoteFairy::receiveOtherMsg(u32 msg, HitSensor* /*pSender*/, HitSensor* /*pReceiver*/) {
    if (msg == ACTMES_SUCCESS) {
        killAllNotes();

        switch (mCompletionJingle) {
        case -1:
            break;
        case 0:
            MR::startSound(this, "SE_SY_TOTAL_COMPLETE");
            break;
        }

        if (mHasDemo) {
            setNerve(GET_NERVE(NoteFairy, NoteFairyNrvSuccessDemo));
        } else {
            setNerve(GET_NERVE(NoteFairy, NoteFairyNrvSuccess));
        }

        return true;
    }

    return false;
}
