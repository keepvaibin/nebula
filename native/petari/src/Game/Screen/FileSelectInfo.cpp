#include "Game/Screen/FileSelectInfo.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_disappear_setAnimRate_1, "disappear.setAnimRate.1", 0.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeToLuigi_1, "exeToLuigi.1", 10);
NEBULA_TUNABLE_NAMED(s32, cStep_exeToMario_1, "exeToMario.1", 10);

namespace {
    NEBULA_TUNABLE(f32, sDisappearAnimRate, 0.25f);

    // A text's code units with its terminator.
    u32 unitsWithEnd(const char16_t* pText) {
        u32 units = 1;
        while (pText[units - 1] != 0) {
            units++;
        }
        return units;
    }
};  // namespace

namespace {
    NEW_NERVE(FileSelectInfoNrvAppear, FileSelectInfo, Appear);
    NEW_NERVE(FileSelectInfoNrvDisplay, FileSelectInfo, Display);
    NEW_NERVE(FileSelectInfoNrvDisappear, FileSelectInfo, Disappear);
};  // namespace

namespace FileSelectInfoSub {
    NEW_NERVE(SlideStateNrvNormalPos, SlideState, NormalPos);
    NEW_NERVE(SlideStateNrvSliding, SlideState, Sliding);
    NEW_NERVE(SlideStateNrvSlidePos, SlideState, SlidePos);
    NEW_NERVE(SlideStateNrvSlidingBack, SlideState, SlidingBack);
};  // namespace FileSelectInfoSub

namespace FileSelectInfoSub {
    NEW_NERVE(CharaStateNrvMario, CharaState, Mario);
    NEW_NERVE(CharaStateNrvToLuigi, CharaState, ToLuigi);
    NEW_NERVE(CharaStateNrvLuigi, CharaState, Luigi);
    NEW_NERVE(CharaStateNrvToMario, CharaState, ToMario);
};  // namespace FileSelectInfoSub

FileSelectInfo::FileSelectInfo(s32 nameBufferSize, const char* pName)
    : LayoutActor(pName, 1), mNumber(), mStarNum(), mStarPieceNum(), mNameBufferSize(nameBufferSize), mName(new char16_t[nameBufferSize]),
      mMissNum(-1), mIsSelectedMarioPrev(true), mIsSelectedMario(true), mIsViewNormalEnding(), mIsViewCompleteEnding() {
    lumacore::setClass(this, lumacore::kClassId< FileSelectInfo >);
    mSlideState = new FileSelectInfoSub::SlideState(this);
    mCharaState = new FileSelectInfoSub::CharaState(this);

    MR::zeroMemory(mName, nameBufferSize * sizeof(char16_t));
}

void FileSelectInfo::init(const TableIter& /*rIter*/) {
    initLayoutManager("FileInfo", 3);
    MR::connectToSceneLayout(this);
    initNerve(GET_NERVE_ANON(FileSelectInfoNrvAppear));
}

void FileSelectInfo::appear() {
    f32 animFrame;

    if (!MR::isDead(this) && isNerve(GET_NERVE_ANON(FileSelectInfoNrvDisappear))) {
        animFrame = MR::getAnimFrame(this, 0);

        MR::startAnim(this, "Appear", 0);
        MR::setAnimFrame(this, animFrame, 0);
    } else {
        MR::startAnim(this, "Appear", 0);
    }

    setNerve(GET_NERVE_ANON(FileSelectInfoNrvAppear));
    LayoutActor::appear();
}

void FileSelectInfo::disappear() {
    f32 animFrame;

    if (!MR::isDead(this) && !isNerve(GET_NERVE_ANON(FileSelectInfoNrvDisappear))) {
        if (isNerve(GET_NERVE_ANON(FileSelectInfoNrvAppear))) {
            animFrame = MR::getAnimFrame(this, 0);
        } else {
            MR::startAnim(this, "Appear", 0);

            animFrame = MR::getAnimCtrl(this, 0)->getEnd() - 1.0f;
        }

        MR::startAnim(this, "Appear", 0);
        MR::setAnimFrame(this, animFrame, 0);
        MR::setAnimRate(this, cNum_disappear_setAnimRate_1, 0);
        setNerve(GET_NERVE_ANON(FileSelectInfoNrvDisappear));
    }
}

void FileSelectInfo::slide() {
    mSlideState->setNerve(GET_NERVE_DIRECT(FileSelectInfoSub, SlideStateNrvSliding));
}

void FileSelectInfo::slideBack() {
    mSlideState->setNerve(GET_NERVE_DIRECT(FileSelectInfoSub, SlideStateNrvSlidingBack));
}

void FileSelectInfo::setInfo(u16* pName, s32 number, s32 starNum, s32 starPieceNum, bool isSelectedMario, bool isViewNormalEnding,
                             bool isViewCompleteEnding, const char16_t* pDateMessage, const char16_t* pTimeMessage, s32 missNum) {
    mNumber = number;
    mStarNum = starNum;
    mStarPieceNum = starPieceNum;

    MR::copyMemory(mName, pName, mNameBufferSize * sizeof(u16));

    mIsSelectedMario = isSelectedMario;
    mIsViewNormalEnding = isViewNormalEnding;
    mIsViewCompleteEnding = isViewCompleteEnding;

    const u32 dateUnits = unitsWithEnd(pDateMessage);
    MR::copyString(mDateMessage.room(dateUnits), pDateMessage, dateUnits);
    const u32 timeUnits = unitsWithEnd(pTimeMessage);
    MR::copyString(mTimeMessage.room(timeUnits), pTimeMessage, timeUnits);

    mMissNum = missNum;
}

void FileSelectInfo::change() {
    FileSelectInfoSub::CharaState* pCharaState;

    if (mIsSelectedMarioPrev && !mIsSelectedMario) {
        pCharaState = mCharaState;

        if (!pCharaState->isNerve(GET_NERVE_DIRECT(FileSelectInfoSub, CharaStateNrvLuigi)) &&
            !pCharaState->isNerve(GET_NERVE_DIRECT(FileSelectInfoSub, CharaStateNrvToLuigi))) {
            pCharaState->setNerve(GET_NERVE_DIRECT(FileSelectInfoSub, CharaStateNrvToLuigi));
        }
    } else if (!mIsSelectedMarioPrev && mIsSelectedMario) {
        pCharaState = mCharaState;

        if (!pCharaState->isNerve(GET_NERVE_DIRECT(FileSelectInfoSub, CharaStateNrvMario)) &&
            !pCharaState->isNerve(GET_NERVE_DIRECT(FileSelectInfoSub, CharaStateNrvToMario))) {
            pCharaState->setNerve(GET_NERVE_DIRECT(FileSelectInfoSub, CharaStateNrvToMario));
        }
    }

    mIsSelectedMarioPrev = mIsSelectedMario;
}

void FileSelectInfo::forceChange() {
    if (mIsSelectedMarioPrev && !mIsSelectedMario) {
        mCharaState->setNerve(GET_NERVE_DIRECT(FileSelectInfoSub, CharaStateNrvLuigi));
        reflectInfo();
    } else if (!mIsSelectedMarioPrev && mIsSelectedMario) {
        mCharaState->setNerve(GET_NERVE_DIRECT(FileSelectInfoSub, CharaStateNrvMario));
        reflectInfo();
    }

    mIsSelectedMarioPrev = mIsSelectedMario;
}

void FileSelectInfo::exeAppear() {
    if (MR::isFirstStep(this)) {
        reflectInfo();
    }

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE_ANON(FileSelectInfoNrvDisplay));
    }
}

void FileSelectInfo::exeDisplay() {
}

void FileSelectInfo::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::setAnimRate(this, -::sDisappearAnimRate, 0);
    }

    if (MR::getAnimFrame(this, 0) - ::sDisappearAnimRate <= 0.0f) {
        kill();
    }
}

void FileSelectInfo::control() {
    mSlideState->updateNerve();
    mCharaState->updateNerve();
}

void FileSelectInfo::reflectInfo() {
    MR::setTextBoxNumberRecursive(this, "FileNumber", mNumber);
    MR::setTextBoxMessageRecursive(this, "FileName", mName);
    MR::setTextBoxNumberRecursive(this, "Star", mStarNum);
    MR::setTextBoxNumberRecursive(this, "StarPiece", mStarPieceNum);
    MR::setTextBoxMessageRecursive(this, "TxtDay", mDateMessage.c_str());
    MR::setTextBoxMessageRecursive(this, "TxtTime", mTimeMessage.c_str());

    if (mMissNum >= 0) {
        MR::setTextBoxNumberRecursive(this, "MissCounter", mMissNum);
        MR::showPane(this, "MissCounter");
    } else {
        MR::hidePane(this, "MissCounter");
    }

    if (mIsViewNormalEnding) {
        MR::showPane(this, "Complete1");
    } else {
        MR::hidePane(this, "Complete1");
    }

    if (mIsViewCompleteEnding) {
        MR::showPane(this, "Complete2");
    } else {
        MR::hidePane(this, "Complete2");
    }

    bool bVar1 = !mIsSelectedMarioPrev || mIsViewCompleteEnding;

    if (bVar1) {
        MR::showPane(this, "BrosIcon");

        if (mIsSelectedMarioPrev) {
            MR::showPane(this, "TxtMario");
            MR::hidePane(this, "TxtLuigi");
        } else {
            MR::hidePane(this, "TxtMario");
            MR::showPane(this, "TxtLuigi");
        }
    } else {
        MR::hidePane(this, "BrosIcon");
    }
}

namespace FileSelectInfoSub {
    SlideState::SlideState(FileSelectInfo* pHost) : NerveExecutor("スライド状態"), mHost(pHost) {
        initNerve(GET_NERVE_GLOBAL(SlideStateNrvNormalPos));
    }

    void SlideState::exeNormalPos() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "ButtonAppear", 1);
            MR::stopAnim(mHost, 1);
        }
    }

    void SlideState::exeSliding() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "ButtonAppear", 1);
        }

        if (MR::isAnimStopped(mHost, 1)) {
            setNerve(GET_NERVE_GLOBAL(SlideStateNrvSlidePos));
        }
    }

    void SlideState::exeSlidePos() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "ButtonEnd", 1);
            MR::stopAnim(mHost, 1);
        }
    }

    void SlideState::exeSlidingBack() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "ButtonEnd", 1);
        }

        if (MR::isAnimStopped(mHost, 1)) {
            setNerve(GET_NERVE_GLOBAL(SlideStateNrvNormalPos));
        }
    }
};  // namespace FileSelectInfoSub

namespace FileSelectInfoSub {
    CharaState::CharaState(FileSelectInfo* pHost) : NerveExecutor("キャラ選択状態"), mHost(pHost) {
        initNerve(GET_NERVE_GLOBAL(CharaStateNrvMario));
    }

    void CharaState::exeMario() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "MarioWait", 2);
        }
    }

    void CharaState::exeToLuigi() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "MariottoLuigi", 2);
        }

        if (MR::isStep(this, cStep_exeToLuigi_1)) {
            mHost->reflectInfo();
        }

        if (MR::isAnimStopped(mHost, 2)) {
            setNerve(GET_NERVE_GLOBAL(CharaStateNrvLuigi));
        }
    }

    void CharaState::exeLuigi() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "LuigiWait", 2);
        }
    }

    void CharaState::exeToMario() {
        if (MR::isFirstStep(this)) {
            MR::startAnim(mHost, "LuigitoMario", 2);
        }

        if (MR::isStep(this, cStep_exeToMario_1)) {
            mHost->reflectInfo();
        }

        if (MR::isAnimStopped(mHost, 2)) {
            setNerve(GET_NERVE_GLOBAL(CharaStateNrvMario));
        }
    }
};  // namespace FileSelectInfoSub
