#include "Game/Screen/MiiSelect.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/FileSelectFunc.hpp"
#include "Game/Map/FileSelectIconID.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/MiiSelectIcon.hpp"
#include "Game/Util/BitArray.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Render/Clip.hpp"
#include "Game/Mii/Mii.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    NEW_NERVE(MiiSelectNrvAppear, MiiSelect, Appear);
    NEW_NERVE(MiiSelectNrvWait, MiiSelect, Wait);
    NEW_NERVE(MiiSelectNrvScrollRight, MiiSelect, ScrollRight);
    NEW_NERVE(MiiSelectNrvScrollLeft, MiiSelect, ScrollLeft);
    NEW_NERVE(MiiSelectNrvSelected, MiiSelect, Selected);
    NEW_NERVE(MiiSelectNrvDisappear, MiiSelect, Disappear);
    NEW_NERVE(MiiSelectNrvDummySelected, MiiSelect, DummySelected);

    typedef void (MiiSelect::*CallbackFuncPtr)();

    static const CallbackFuncPtr func[2] = {&MiiSelect::callbackLeft, &MiiSelect::callbackRight};
};  // namespace

// The library's generation the picker last collected (Miis imported while the game runs change it).
static u32 sCollectedMiiGeneration = 0;

MiiSelect::MiiSelect(const char* pName)
    : LayoutActor(pName, true), mFellowEnabled(new MR::BitArray(5)), mFellowIconNum(), mFavoriteMiiNum(), mMiiNum(), mMiiIndices(), mMiiIndexRoom(), mPageBaseIndex(), mSelectedIndex(), mSelectedTexMap(), mCurrentPage(), mOtherPage(),
      mIsIconProhibited(), mProhibitedIconID(new FileSelectIconID()) {
    lumacore::setClass(this, lumacore::kClassId< MiiSelect >);
    for (int i = 0; i < ARRAY_SIZE(mArrowButtons); i++) {
        mArrowButtons[i] = nullptr;
    }

    validateAllSpecialMii();
}

void MiiSelect::init(const TableIter& /*rIter*/) {
    initLayoutManager("MiiSelect", 1);
    MR::invalidateParentAnim(this);
    createButtons();
    MR::connectToSceneLayout(this);
    initNerve(GET_NERVE_ANON(MiiSelectNrvAppear));
    createPage();
    kill();
}

void MiiSelect::appear() {
    LayoutActor::appear();
    setNerve(GET_NERVE_ANON(MiiSelectNrvAppear));
    // Miis imported since the file select collected them (FileSelector::exeTitleEnd): collected again.
    if (sCollectedMiiGeneration != Mii::generation()) {
        collectValidMiiIndex();
    }
    mPageBaseIndex = 0;
    refresh();
    setCurrentPageGroupA();
    appearButtons();
    updateButtons();
    mCurrentPage->invalidateAllIcon();
    mOtherPage->invalidateAllIcon();
    MR::setTextBoxMessageRecursive(this, "TxtName", u"");
}

void MiiSelect::disappear() {
    mCurrentPage->invalidateAllIcon();
    mOtherPage->invalidateAllIcon();
    disappearButtons();
    setNerve(GET_NERVE_ANON(MiiSelectNrvDisappear));
}

void MiiSelect::calcAnim() {
    LayoutActor::calcAnim();
    mCurrentPage->calcAnim();
    mOtherPage->calcAnim();
}

bool MiiSelect::isAppearing() const {
    return isNerve(GET_NERVE_ANON(MiiSelectNrvAppear));
}

bool MiiSelect::isSelected() {
    return isNerve(GET_NERVE_ANON(MiiSelectNrvSelected));
}

bool MiiSelect::isDummySelected() {
    return isNerve(GET_NERVE_ANON(MiiSelectNrvDummySelected));
}

void MiiSelect::getSelectedID(FileSelectIconID* pSelectedID) {
    getIconID(pSelectedID, mSelectedIndex);
}

Ui::TexMap* MiiSelect::getSelectedMiiTexMap() {
    return mSelectedTexMap;
}

void MiiSelect::admitIcon() {
    mIsIconProhibited = 0;
}

void MiiSelect::prohibitIcon(const FileSelectIconID& rIconID) {
    mIsIconProhibited = 1;

    mProhibitedIconID->set(rIconID);
}

void MiiSelect::invalidateSpecialMii(FileSelectIconID::EFellowID fellowID) {
    if (mFellowEnabled->isOn(fellowID)) {
        mFellowEnabled->set(fellowID, false);

        mFellowIconNum--;
    }
}

void MiiSelect::validateAllSpecialMii() {
    for (int i = 0; i < mFellowEnabled->size(); i++) {
        mFellowEnabled->set(i, true);
    }

    mFellowIconNum = mFellowEnabled->size();
}

void MiiSelect::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Appear", 0);
        MR::startSystemSE("SE_SY_FILE_SEL_MIISEL_OPEN");
        setCurrentPageNum();
    }

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE_ANON(MiiSelectNrvWait));
    }
}

void MiiSelect::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Wait", 0);
        validateAllIcon();
        setCurrentPageGroupB();

        for (int i = 0; i < ARRAY_SIZE(mArrowButtons); i++) {
            if (mArrowButtons[i]->isHidden()) {
                continue;
            }

            mArrowButtons[i]->mIsActive = true;
            mArrowButtons[i]->forceToWait();
        }

        setCurrentPageNum();
    }

    for (int i = 0; i < ARRAY_SIZE(mArrowButtons); i++) {
        if (mArrowButtons[i]->trySelect()) {
            (this->*::func[i])();
            return;
        }
    }

    if (!mArrowButtons[0]->isHidden() && (MR::isPressedLeft(PLAYER_1) || MR::isMoveStickPressedLeft(PLAYER_1))) {
        callbackLeft();
    } else if (!mArrowButtons[1]->isHidden() && (MR::isPressedRight(PLAYER_1) || MR::isMoveStickPressedRight(PLAYER_1))) {
        callbackRight();
    }
}

void MiiSelect::exeScrollLeft() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "PreviousMii", 0);

        mPageBaseIndex -= 8;

        if (mPageBaseIndex < 0) {
            mPageBaseIndex = ((getIconNum() - 1) / 8) * 8;
        }

        flipPage();
        setCurrentPageGroupB();
        refresh();
        updateButtons();
        mCurrentPage->invalidateAllIcon();
        mOtherPage->invalidateAllIcon();
    }

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE_ANON(MiiSelectNrvWait));
    }
}

void MiiSelect::exeScrollRight() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "NextMii", 0);

        mPageBaseIndex += 8;

        if (mPageBaseIndex >= getIconNum()) {
            mPageBaseIndex = 0;
        }

        flipPage();
        setCurrentPageGroupB();
        refresh();
        updateButtons();
        mCurrentPage->invalidateAllIcon();
        mOtherPage->invalidateAllIcon();
    }

    if (MR::isAnimStopped(this, 0)) {
        setNerve(GET_NERVE_ANON(MiiSelectNrvWait));
    }
}

void MiiSelect::exeSelected() {
}

void MiiSelect::exeDisappear() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "End", 0);
    }

    if (MR::isAnimStopped(this, 0)) {
        kill();
    }
}

void MiiSelect::exeDummySelected() {
}

void MiiSelect::control() {
    for (int i = 0; i < ARRAY_SIZE(mArrowButtons); i++) {
        mArrowButtons[i]->update();
    }

    FrameCtrl* pAnimCtrl;

    if (mArrowButtons[0]->isFirstStepWait() && !mArrowButtons[1]->isFirstStepWait() && mArrowButtons[1]->isWait()) {
        pAnimCtrl = MR::getPaneAnimCtrl(this, mArrowButtons[0]->mPaneName, 0);

        pAnimCtrl->setFrame(MR::getPaneAnimFrame(this, mArrowButtons[1]->mPaneName, 0));
    }

    if (mArrowButtons[1]->isFirstStepWait() && !mArrowButtons[0]->isFirstStepWait() && mArrowButtons[0]->isWait()) {
        pAnimCtrl = MR::getPaneAnimCtrl(this, mArrowButtons[1]->mPaneName, 0);

        pAnimCtrl->setFrame(MR::getPaneAnimFrame(this, mArrowButtons[0]->mPaneName, 0));
    }

    mCurrentPage->movement();
}

void MiiSelect::createButtons() {
    static const char* pane[] = {
        "Left",
        "Right",
    };
    static const char* bound[] = {
        "PicArrowL",
        "PicArrowR",
    };

    for (int i = 0; i < ARRAY_SIZE(mArrowButtons); i++) {
        MR::createAndAddPaneCtrl(this, pane[i], 1);

        mArrowButtons[i] = new ButtonPaneController(this, pane[i], bound[i], 0, true);
        mArrowButtons[i]->mDisappearsAfterDecide = false;
        // The D-pad and stick turn the pages here (NebulaMenuFocus leaves these buttons to the pointer).
        mArrowButtons[i]->mIsNebulaFocusable = false;
    }
}

void MiiSelect::callbackLeft() {
    mArrowButtons[0]->mIsActive = false;

    MR::startSystemSE("SE_SY_FILE_SEL_MIISEL_SCRL");
    setNerve(GET_NERVE_ANON(MiiSelectNrvScrollLeft));
}

void MiiSelect::callbackRight() {
    mArrowButtons[1]->mIsActive = false;

    MR::startSystemSE("SE_SY_FILE_SEL_MIISEL_SCRL");
    setNerve(GET_NERVE_ANON(MiiSelectNrvScrollRight));
}

void MiiSelect::appearButtons() {
    for (int i = 0; i < ARRAY_SIZE(mArrowButtons); i++) {
        mArrowButtons[i]->appear();
    }
}

void MiiSelect::disappearButtons() {
    for (int i = 0; i < ARRAY_SIZE(mArrowButtons); i++) {
        mArrowButtons[i]->disappear();
    }
}

void MiiSelect::updateButtons() {
    if (getIconNum() > 16) {
        if (mArrowButtons[0]->isHidden()) {
            mArrowButtons[0]->appear();
        }

        if (mArrowButtons[1]->isHidden()) {
            mArrowButtons[1]->appear();
        }
    } else {
        if (mPageBaseIndex - 8 < 0) {
            if (!mArrowButtons[0]->isHidden()) {
                mArrowButtons[0]->disappear();
            }
        } else if (mArrowButtons[0]->isHidden()) {
            mArrowButtons[0]->appear();
        }

        if (mPageBaseIndex + 8 >= getIconNum()) {
            if (!mArrowButtons[1]->isHidden()) {
                mArrowButtons[1]->disappear();
            }
        } else if (mArrowButtons[1]->isHidden()) {
            mArrowButtons[1]->appear();
        }
    }
}

void MiiSelect::collectValidMiiIndex() {
    // From empty each time: the picker collects again when Miis were imported since (appear).
    mFavoriteMiiNum = 0;
    mMiiNum = 0;
    sCollectedMiiGeneration = Mii::generation();
    const u32 count = Mii::libraryCount();
    if (mMiiIndexRoom < count) {
        delete[] mMiiIndices;
        mMiiIndices = new u32[count];
        mMiiIndexRoom = count;
    }
    // Two passes over the library: its favourites, then the others, each in the library's order.
    for (u32 pass = 0; pass < 2; pass++) {
        for (u32 i = 0; i < count; i++) {
            Mii::Character mii;

            if (!Mii::character(Mii::Ref{Mii::SourceLibrary, i}, &mii) || (mii.favorite != 0) != (pass == 0)) {
                continue;
            }
            mMiiIndices[mFavoriteMiiNum + mMiiNum] = i;
            if (pass == 0) {
                mFavoriteMiiNum++;
            } else {
                mMiiNum++;
            }
        }
    }
}

void MiiSelect::createPage() {
    mCurrentPage = new MiiSelectSub::Page(this);
    mOtherPage = new MiiSelectSub::Page(this);
}

void MiiSelect::flipPage() {
    MiiSelectSub::Page* pPage = mCurrentPage;

    mCurrentPage = mOtherPage;
    mOtherPage = pPage;
}

void MiiSelect::setCurrentPageGroupA() {
    mCurrentPage->mIsGroupA = 1;
    mOtherPage->mIsGroupA = 0;
}

void MiiSelect::setCurrentPageGroupB() {
    mCurrentPage->mIsGroupA = 0;
    mOtherPage->mIsGroupA = 1;
}

void MiiSelect::setCurrentPageNum() {
    s32 v2 = (getIconNum() - 1) / 8;
    s32 v1 = mPageBaseIndex / 8;

    MR::setTextBoxFormatRecursive(this, "TxtPage", u"%d/%d", v1 + 1, v2 + 1);
}

void MiiSelect::validateAllIcon() {
    mCurrentPage->validateAllIcon();
    mOtherPage->validateAllIcon();

    if (mIsIconProhibited) {
        mCurrentPage->prohibitIcon(*mProhibitedIconID);
        mOtherPage->prohibitIcon(*mProhibitedIconID);
    }
}

void MiiSelect::refresh() {
    mCurrentPage->refresh(mPageBaseIndex);

    if (mIsIconProhibited) {
        mCurrentPage->prohibitIcon(*mProhibitedIconID);
        mOtherPage->prohibitIcon(*mProhibitedIconID);
    }
}

void MiiSelect::getIconID(FileSelectIconID* pIconID, s32 param2) const {
    if (static_cast< u32 >(param2) < mFellowIconNum) {
        s32 v = 0;

        for (int i = 0; i < mFellowEnabled->size(); i++) {
            if (!mFellowEnabled->isOn(i)) {
                continue;
            }

            if (v == param2) {
                pIconID->setFellowID(static_cast< FileSelectIconID::EFellowID >(i));
                break;
            }

            v++;
        }
    } else {
        pIconID->setMiiIndex(mMiiIndices[param2 - mFellowIconNum]);
    }
}

void MiiSelect::onSelect(s32 param1, Ui::TexMap* pTexMap) {
    mSelectedIndex = param1;
    mSelectedTexMap = pTexMap;

    mCurrentPage->invalidateAllIcon();
    mOtherPage->invalidateAllIcon();
    disappearButtons();
    setNerve(GET_NERVE_ANON(MiiSelectNrvSelected));
}

void MiiSelect::onSelectDummy() {
    mCurrentPage->invalidateAllIcon();
    mOtherPage->invalidateAllIcon();
    disappearButtons();
    setNerve(GET_NERVE_ANON(MiiSelectNrvDummySelected));
}

namespace MiiSelectSub {
    Page::Page(MiiSelect* pHost) : mIsGroupA(true), mHost(pHost) {
        for (u32 i = 0; i < ARRAY_SIZE(mIconArray); i++) {
            mIconArray[i] = new MiiSelectIcon(-1, -1, -1, "Miiセレクト用アイコン");
        }
    }

    void Page::refresh(s32 baseIndex) {
        mBaseIndex = baseIndex;

        s32 iconNum = mHost->getIconNum();

        for (s32 i = 0; i < ARRAY_SIZE(mIconArray); i++) {
            if (mHost->isExistMiiIcon()) {
                if (baseIndex + i >= iconNum) {
                    mIconArray[i]->kill();
                    continue;
                }
            } else if (baseIndex + i == iconNum) {
                mIconArray[i]->appearMiiDummy();
                continue;
            } else if (baseIndex + i > iconNum) {
                mIconArray[i]->kill();
                continue;
            }

            FileSelectIconID iconID;
            mHost->getIconID(&iconID, baseIndex + i);
            mIconArray[i]->appear(iconID);
        }
    }

    inline void setPointedMiiName(MiiSelect* pHost, s32 index) {
        char16_t name[Mii::NameLength + 1];  // a Mii's name: the Mii record's field and the terminator
        FileSelectFunc::copyMiiName(reinterpret_cast< u16* >(name), pHost->makeIconID(index));
        MR::setTextBoxMessageRecursive(pHost, "TxtName", name);
    }

    void Page::movement() {
        bool isPointingAny = false;

        for (s32 i = 0; i < ARRAY_SIZE(mIconArray); i++) {
            if (!MR::isDead(mIconArray[i])) {
                mIconArray[i]->movement();

                if (mIconArray[i]->isMiiDummy()) {
                    if (mIconArray[i]->isSelected()) {
                        mHost->onSelectDummy();
                        mIconArray[i]->invalidate();
                    }
                } else if (mIconArray[i]->isSelected()) {
                    mHost->onSelect(mBaseIndex + i, mIconArray[i]->getTexMap());
                    mIconArray[i]->invalidate();
                } else if (mIconArray[i]->isPointing()) {
                    setPointedMiiName(mHost, mBaseIndex + i);

                    isPointingAny = true;
                }
            }
        }

        if (!isPointingAny) {
            MR::setTextBoxMessageRecursive(mHost, "TxtName", u"");
        }
    }

    void Page::calcAnim() {
        s32 v = 9;

        if (mIsGroupA) {
            v = 1;
        }

        for (s32 i = 0; i < ARRAY_SIZE(mIconArray); i++) {
            MR::FormattedName paneName("Mii%02d", v + i);

            MR::setLayoutPosAtPaneTrans(mIconArray[i], mHost, paneName.c_str());
            mIconArray[i]->calcAnim();
        }
    }

    void Page::invalidateAllIcon() {
        for (int i = 0; i < ARRAY_SIZE(mIconArray); i++) {
            mIconArray[i]->invalidate();
        }
    }

    void Page::validateAllIcon() {
        for (int i = 0; i < ARRAY_SIZE(mIconArray); i++) {
            mIconArray[i]->validate();
        }
    }

    void Page::prohibitIcon(const FileSelectIconID& rIconID) {
        for (int i = 0; i < ARRAY_SIZE(mIconArray); i++) {
            if (MR::isDead(mIconArray[i])) {
                continue;
            }

            if (mIconArray[i]->isMiiDummy()) {
                continue;
            }

            FileSelectIconID iconID = FileSelectIconID();

            mIconArray[i]->copyIconID(&iconID);

            if (iconID != rIconID) {
                continue;
            }

            mIconArray[i]->prohibit();
        }
    }
};  // namespace MiiSelectSub

s32 MiiSelect::getIconNum() {
    return mMiiNum + mFavoriteMiiNum + mFellowIconNum;
}
