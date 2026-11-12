#include "Game/Util/TextString.hpp"
#include "Game/Screen/PowerStarList.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/FileSelectFunc.hpp"
#include "Game/Map/RaceManager.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/ReplaceTagProcessor.hpp"
#include "Game/Screen/SysInfoWindow.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataConst.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <Game/Ui/DrawInfo.hpp>
#include <Game/Ui/Pane.hpp>
#include "Game/System/LumaCore.hpp"


#define ITEMS_PER_PAGE 15

namespace {
    class ListItem {
    public:
        /// @brief Creates a new `ListItem`.
        ListItem() : mType(-1), mRaceId() {
        }

        s32 mType;
        union {
            GalaxyStatusAccessor mGalaxyAccessor;
            s32 mRaceId;
        };
    };

    static const char* const cGalaxyNamePaneTable[] = {
        "TxtStage1", "TxtStage2", "TxtStage3", "TxtStage4", "TxtStage5", "TxtStage6", "TxtStage7", "TxtStage8",
    };

    static const char* const cStarNumPaneTable[] = {
        "TxtStar1",
        "TxtStar2",
        "TxtStar3",
        "TxtStar4",
    };

    static const char* const cCoinPaneTable[] = {
        "TxtCoinPiece1",
        "TxtCoinPiece2",
        "TxtCoinPiece3",
        "TxtCoinPiece4",
    };

    static const char* const cCrownPaneTable[] = {
        "TxtComp1",
        "TxtComp2",
        "TxtComp3",
        "TxtComp4",
    };

    static const char* const cTimePaneTable[] = {
        "TxtTime1",
        "TxtTime2",
        "TxtTime3",
        "TxtTime4",
    };

    static const char* const cSeparatorPaneTable[] = {
        "PicBelt1",
        "PicBelt2",
        "PicBelt3",
        "PicBelt4",
    };

    static const char* const cSeparatorSizePaneTable[] = {
        "PicBelt1",
        "PicBelt2",
        "PicBelt3",
        "WinBelt4",
    };

    const f32 cSeparatorOffset = 20.0f;
    const f32 cTimePaneDefaultHeight = 72.0f;

    s32 getSortPriority(const GalaxyStatusAccessor* pAccessor) {
        const char* pName = pAccessor->getName();

        if (pAccessor->getNormalScenarioNum() == 3) {
            return 1;
        }

        if (MR::isEqualString(pName, "HeavensDoorGalaxy")) {
            return 2;
        }

        if (pAccessor->isExistGrandStar()) {
            return 3;
        }

        if (GameDataConst::isQuestionGalaxy(pName)) {
            return 4;
        }

        return 5;
    }

    s32 getDisplayRaceBestTimeNum() {
        s32 num = 0;

        for (s32 idx = 0; idx < 5; idx++) {
            if (RaceManagerFunction::hasPowerStarRaceScenario(idx)) {
                num++;
            }
        }

        return num;
    };

    void sortGalaxyAccessors(GalaxyStatusAccessor* pBegin, GalaxyStatusAccessor* pEnd) {
        for (GalaxyStatusAccessor* pCurrent = pBegin + 1; pCurrent != pEnd; pCurrent++) {
            for (GalaxyStatusAccessor* p = pCurrent; p != pBegin && getSortPriority(p) < getSortPriority(p - 1); p--) {
                GalaxyStatusAccessor temp = *p;
                *p = *(p - 1);
                *(p - 1) = temp;
            }
        }
    }

    // Every galaxy, then a page's room of gaps, then the races.
    s32 listRoom() {
        s32 galaxies = 0;
        for (ScenarioDataIter iter = MR::makeBeginScenarioDataIter(); !iter.isEnd(); iter.goNext()) {
            galaxies++;
        }
        return galaxies + 2 * ITEMS_PER_PAGE + 5;
    }

    s32 getSortedList(ListItem* pItems) {
        GalaxyStatusAccessor* const galaxyAccessors = new GalaxyStatusAccessor[listRoom()];
        s32 availableGalaxies = 0;

        ScenarioDataIter iter = MR::makeBeginScenarioDataIter();

        for (; !iter.isEnd();) {
            GalaxyStatusAccessor accessor = iter.makeAccessor();
            bool open = accessor.getPowerStarNum() == 0 ? false : MR::isOnGameEventFlagGalaxyOpen(accessor.getName());

            if (open) {
                galaxyAccessors[availableGalaxies] = iter.makeAccessor();
                availableGalaxies++;
            }

            iter.goNext();
        }

        sortGalaxyAccessors(galaxyAccessors, galaxyAccessors + availableGalaxies);

        s32 outCount = 0;
        for (s32 readIndex = 0; readIndex < availableGalaxies; readIndex++) {
            pItems[outCount].mType = 0;
            pItems[outCount].mGalaxyAccessor = galaxyAccessors[readIndex];
            outCount++;
        }
        delete[] galaxyAccessors;

        if (getDisplayRaceBestTimeNum() > 0) {
            while (outCount % ITEMS_PER_PAGE != 0) {
                pItems[outCount].mType = 3;
                outCount++;
            }

            while (outCount % ITEMS_PER_PAGE != 3) {
                pItems[outCount].mType = 3;
                outCount++;
            }

            for (s32 raceIdx = 0; raceIdx < 5; raceIdx++) {
                if (RaceManagerFunction::hasPowerStarRaceScenario(raceIdx)) {
                    pItems[outCount].mType = 1;
                    pItems[outCount].mRaceId = raceIdx;
                    outCount++;
                }
            }
        }

        return outCount;
    };
};  // namespace

namespace NrvPowerStarList {
    NEW_NERVE(PowerStarListNrvAppear, PowerStarList, Appear);
    NEW_NERVE(PowerStarListNrvWait, PowerStarList, Wait);
    NEW_NERVE(PowerStarListNrvDisappear, PowerStarList, Disappear);
    NEW_NERVE(PowerStarListNrvPageNext, PowerStarList, PageNext);
    NEW_NERVE(PowerStarListNrvPagePrev, PowerStarList, PagePrev);
};  // namespace NrvPowerStarList

PowerStarList::PowerStarList()
    : LayoutActor("全パワースターリスト", true), mArrowUpButtonCtrl(), mArrowDownButtonCtrl(), mSysInfoWindow(), mPageNo() {
    lumacore::setClass(this, lumacore::kClassId< PowerStarList >);
    mSeparatorArray = new Separator[4]();
}

PowerStarList::Separator::Separator() {
}

void PowerStarList::init(const TableIter& /*rIter*/) {
    initLayoutManager("AllStarList", 1);
    MR::createAndAddPaneCtrl(this, "PicBG", 1);
    MR::createAndAddPaneCtrl(this, "List1", 1);
    MR::createAndAddPaneCtrl(this, "List2", 1);
    MR::createAndAddPaneCtrl(this, "Title", 1);
    MR::createAndAddPaneCtrl(this, "Belt", 1);

    const char* pPaneName;
    for (u32 i = 0; i < ARRAY_SIZE(::cSeparatorPaneTable); i++) {
        pPaneName = ::cSeparatorPaneTable[i];
        MR::createAndAddPaneCtrl(this, pPaneName, 1);
        MR::setFollowPos(&mSeparatorArray[i].mOffset, this, pPaneName);
        MR::setFollowTypeAdd(this, pPaneName);
    }


    mArrowUpButtonCtrl = createButtonController("ArrowUpButton", "BoxButton1_00");
    mArrowDownButtonCtrl = createButtonController("ArrowDownButton", "BoxButton1_01");
    // No message board (B-31, Q5), no capture button.
    MR::hidePaneRecursive(this, "Capture");
    mSysInfoWindow = MR::createSysInfoWindow();
    MR::connectToSceneLayoutOnPause(mSysInfoWindow);
    MR::setTextBoxGameMessageRecursive(this, "Title", "AllStarList_Title");

    ListItem* const pItems = new ListItem[::listRoom()];
    mPageNum = (::getSortedList(pItems) - 1) / ITEMS_PER_PAGE + 1;
    delete[] pItems;

    MR::setTextBoxGameMessageRecursive(this, "PageNumber", "AllStarList_Page");
    MR::setTextBoxGameMessageRecursive(this, "Photo", "AllStarList_Page");
    MR::hidePaneRecursive(this, "Photo");
    initNerve(GET_NERVE(PowerStarList, PowerStarListNrvWait));

}

void PowerStarList::calcAnim() {
    LayoutActor::calcAnim();
    setSeparatorPaneSize();
}

void PowerStarList::appear() {
    updateList(0, true);
    startScrollAnimNext(true);
    LayoutActor::appear();
    setNerve(GET_NERVE(PowerStarList, PowerStarListNrvAppear));
}

bool PowerStarList::isAppearing() const {
    return isNerve(GET_NERVE(PowerStarList, PowerStarListNrvAppear));
}

void PowerStarList::control() {
    mArrowUpButtonCtrl->update();
    mArrowDownButtonCtrl->update();

    if (mArrowUpButtonCtrl->isPointingTrigger() || mArrowDownButtonCtrl->isPointingTrigger()) {
        MR::startSystemSE("SE_SY_GALAMAP_CURSOR_ON");
    }
}

ButtonPaneController* PowerStarList::createButtonController(const char* pParam1, const char* pParam2) {
    ButtonPaneController* pController = new ButtonPaneController(this, pParam1, pParam2, 0, true);

    pController->mWaitAnimName = "Wait";
    pController->invalidateAppearance();
    // The D-pad and stick turn the pages here (NebulaMenuFocus leaves these buttons to the pointer).
    pController->mIsNebulaFocusable = false;

    return pController;
}

void PowerStarList::resetButtonAll() {
    mArrowUpButtonCtrl->forceToWait();
    mArrowDownButtonCtrl->forceToWait();
}

void PowerStarList::updateButtonAppearance() {
    if (mPageNum == 1) {
        mArrowDownButtonCtrl->mIsActive = false;
        mArrowUpButtonCtrl->mIsActive = false;

        MR::hidePane(this, "ArrowDownButton");
        MR::hidePane(this, "ArrowUpButton");
        MR::hidePane(this, "PageNumber");
    } else {
        MR::showPane(this, "ArrowDownButton");
        MR::showPane(this, "ArrowUpButton");
        MR::showPane(this, "PageNumber");

        if (mPageNo < mPageNum - 1) {
            mArrowDownButtonCtrl->mIsActive = true;
            MR::setPaneAlpha(this, "ArrowDownButton", -1);
        } else {
            mArrowDownButtonCtrl->mIsActive = false;
            MR::setPaneAlpha(this, "ArrowDownButton", 80);
        }

        if (mPageNo > 0) {
            mArrowUpButtonCtrl->mIsActive = true;
            MR::setPaneAlpha(this, "ArrowUpButton", -1);
        } else {
            mArrowUpButtonCtrl->mIsActive = false;
            MR::setPaneAlpha(this, "ArrowUpButton", 80);
        }

        const char* pPaneName = "PageNumber";
        MR::setTextBoxArgNumberRecursive(this, pPaneName, mPageNo + 1, 0);
        MR::setTextBoxArgNumberRecursive(this, pPaneName, mPageNum, 1);
    }
}

namespace {
    class TextBuffer {
    public:
        TextBuffer(LayoutActor* pActor, const char* const* pPaneNames, s32 rowsPerPane)
            : mActor(pActor), mPaneNames(pPaneNames), mRowsPerPane(rowsPerPane), mTail(0) {
        }

        // The tail, with room for `units` more units after it (the text may move).
        char16_t* tailFor(u32 units) {
            return mBuffer.grow(mTail + 1, mTail + units + 1) + mTail;
        }

        void setTail(const char16_t* pTail) {
            mTail = static_cast< u32 >(pTail - mBuffer.c_str());
        }

        void addNewLine(s32 row) {
            s32 paneIndex = row / mRowsPerPane;

            if (row - paneIndex * mRowsPerPane != 0) {
                setTail(MR::addNewLine(tailFor(2)));
            }
        }

        void update(s32 row) {
            s32 paneIndex = row / mRowsPerPane;

            if (row - paneIndex * mRowsPerPane == 0) {
                MR::setTextBoxMessageRecursive(mActor, mPaneNames[paneIndex - 1], mBuffer.c_str());
                mTail = 0;
                mBuffer.room(1);
            }
        }

        void finish(s32 row) {
            s32 paneIndex = row / mRowsPerPane;

            if (row - paneIndex * mRowsPerPane == 0) {
                return;
            }

            MR::setTextBoxMessageRecursive(mActor, mPaneNames[paneIndex], mBuffer.c_str());
        }

        LayoutActor* mActor;
        const char* const* mPaneNames;
        s32 mRowsPerPane;
        MR::Text16 mBuffer;
        u32 mTail;  // where the text ends
    };
};  // namespace

void PowerStarList::updateList(s32 pageNumber, bool isSinglePage) {
    if (pageNumber >= 0) {
        mPageNo = pageNumber;
    }

    for (u32 idx = 0; idx < ARRAY_SIZE(::cGalaxyNamePaneTable); idx++) {
        MR::setTextBoxMessageRecursive(this, ::cGalaxyNamePaneTable[idx], u"");
    }

    for (u32 idx = 0; idx < ARRAY_SIZE(::cStarNumPaneTable); idx++) {
        MR::setTextBoxMessageRecursive(this, ::cStarNumPaneTable[idx], u"");
    }

    for (u32 idx = 0; idx < ARRAY_SIZE(::cCoinPaneTable); idx++) {
        MR::setTextBoxMessageRecursive(this, ::cCoinPaneTable[idx], u"");
    }

    for (u32 idx = 0; idx < ARRAY_SIZE(::cCrownPaneTable); idx++) {
        MR::setTextBoxMessageRecursive(this, ::cCrownPaneTable[idx], u"");
    }

    for (u32 idx = 0; idx < ARRAY_SIZE(::cTimePaneTable); idx++) {
        MR::setTextBoxMessageRecursive(this, ::cTimePaneTable[idx], u"");
    }

    for (u32 idx = 0; idx < ARRAY_SIZE(::cSeparatorPaneTable); idx++) {
        MR::hidePaneRecursive(this, ::cSeparatorPaneTable[idx]);
    }

    // Built for this page and freed at its end: the list belongs to the scene showing it.
    ListItem* const listItems = new ListItem[::listRoom()];

    s32 itemCount = ::getSortedList(listItems);
    mPageNum = (itemCount - 1) / ITEMS_PER_PAGE + 1;

    TextBuffer galaxyBuffer(this, ::cGalaxyNamePaneTable, 4);
    TextBuffer starBuffer(this, ::cStarNumPaneTable, 8);
    TextBuffer coinBuffer(this, ::cCoinPaneTable, 8);
    TextBuffer crownBuffer(this, ::cCrownPaneTable, 8);
    TextBuffer timeBuffer(this, ::cTimePaneTable, 8);

    ListItem* pEnd;
    bool isOdd = mPageNo % 2;
    s32 row;
    s32 rowLimit = isSinglePage ? 15 : 30;
    s32 bestTimeNum = ::getDisplayRaceBestTimeNum();
    ListItem* pItem = listItems + mPageNo * ITEMS_PER_PAGE;
    pEnd = listItems + itemCount;
    row = isOdd;
    s32 prevSeparator = -1;
    s32 galaxyCount;
    s32 separatorRows = 0;
    galaxyCount = 0;

    // A page past the list's end has no rows (it was a != compare).
    for (; pItem < pEnd; pItem++) {
        switch (pItem->mType) {
        case 0: {
            galaxyBuffer.mTail = makeGalaxyNameText(galaxyBuffer.mBuffer, &pItem->mGalaxyAccessor, row % 4 == 0);
            // A newline, a code for each star, and the terminator.
            starBuffer.setTail(makeStarNumText(starBuffer.tailFor(pItem->mGalaxyAccessor.getPowerStarNum() + 2), &pItem->mGalaxyAccessor, row % 8 == 0));
            // A newline, the tag, three digits and the terminator.
            coinBuffer.setTail(makeCoinText(coinBuffer.tailFor(16), &pItem->mGalaxyAccessor, row % 8 == 0));
            crownBuffer.setTail(makeCrownText(crownBuffer.tailFor(3), &pItem->mGalaxyAccessor, row % 8 == 0));
            timeBuffer.addNewLine(row);

            s32 priority = ::getSortPriority(&pItem->mGalaxyAccessor);
            s32 separator;
            if (priority <= 1) {
                separator = 0;
            } else {
                separator = priority <= 3 ? 1 : 2;
            }

            if (prevSeparator != separator) {
                if (prevSeparator >= 0) {
                    tryShowSeparator(prevSeparator, ::cSeparatorOffset * separatorRows);
                }

                Vec2f& rOffset = mSeparatorArray[separator].mOffset;
                rOffset.x = 0.0f;
                rOffset.y = ::cSeparatorOffset * row;
                prevSeparator = separator;
                separatorRows = 0;
            }

            separatorRows++;
            galaxyCount++;
            break;
        }
        case 1:
            galaxyBuffer.mTail = makeRaceNameText(galaxyBuffer.mBuffer, pItem->mRaceId, row % 4 == 0);
            timeBuffer.mTail = makeRaceTimeText(timeBuffer.mBuffer, pItem->mRaceId, row % 8 == 0);
            starBuffer.addNewLine(row);
            coinBuffer.addNewLine(row);
            crownBuffer.addNewLine(row);

            if (tryShowSeparator(3, ::cTimePaneDefaultHeight + ::cSeparatorOffset * (bestTimeNum - 1))) {
                Vec2f& rOffset = mSeparatorArray[3].mOffset;
                rOffset.x = 0.0f;
                rOffset.y = ::cSeparatorOffset * row;
            }

            break;
        case 3:
            galaxyBuffer.addNewLine(row);
            starBuffer.addNewLine(row);
            coinBuffer.addNewLine(row);
            crownBuffer.addNewLine(row);
            timeBuffer.addNewLine(row);
            break;
        }

        row++;
        galaxyBuffer.update(row);
        starBuffer.update(row);
        coinBuffer.update(row);
        crownBuffer.update(row);
        timeBuffer.update(row);

        if (rowLimit <= row - isOdd) {
            break;
        }
    }

    if (galaxyCount % ITEMS_PER_PAGE > 0) {
        separatorRows += ITEMS_PER_PAGE - galaxyCount % ITEMS_PER_PAGE;
    }

    tryShowSeparator(prevSeparator, ::cSeparatorOffset * separatorRows);
    galaxyBuffer.finish(row);
    starBuffer.finish(row);
    coinBuffer.finish(row);
    crownBuffer.finish(row);
    timeBuffer.finish(row);
    delete[] listItems;
}

namespace {
    // `rBuffer`'s text with `pName` as a line after it (a new text when `isFirstLine`); its length.
    u32 addLine(MR::Text16& rBuffer, const char16_t* pName, bool isFirstLine) {
        const u32 nameLength = static_cast< u32 >(MR::textLength(pName));
        const u32 used = isFirstLine ? 0 : static_cast< u32 >(MR::textLength(rBuffer.c_str())) + 1;
        char16_t* const pText = rBuffer.grow(used, used + nameLength + 1);
        if (!isFirstLine) {
            pText[used - 1] = u'\n';
        }
        MR::copyText(pText + used, pName, nameLength + 1);
        return used + nameLength;
    }
};  // namespace

u32 PowerStarList::makeGalaxyNameText(MR::Text16& rBuffer, const GalaxyStatusAccessor* pAccessor, bool isFirstLine) const {
    return addLine(rBuffer, MR::getGalaxyNameShortOnCurrentLanguage(pAccessor->getName()), isFirstLine);
}

char16_t* PowerStarList::makeStarNumText(char16_t* pBuffer, const GalaxyStatusAccessor* pAccessor, bool isFirstLine) const {
    char16_t* pText = pBuffer;
    if (!isFirstLine) {
        *pText = u'\n';
        pText++;
    }

    const char* pName = pAccessor->getName();
    for (s32 idx = 0; idx < pAccessor->getPowerStarNum(); idx++) {
        s32 starId = idx + 1;
        if (!MR::isOnGameEventFlagPowerStarSuccess(pName, starId)) {
            continue;
        } else if (GameDataConst::isGrandStar(pName, starId)) {
            *pText = u'K';
            pText++;
        } else if (GameDataConst::isPowerStarGreen(pName, starId)) {
            *pText = u'O';
            pText++;
        } else if (GameDataConst::isPowerStarRed(pName, starId)) {
            *pText = u'S';
            pText++;
        } else if (pAccessor->isCometStar(starId)) {
            *pText = u'e';
            pText++;
        } else {
            *pText = u'7';
            pText++;
        }
    }

    *pText = u'\0';
    return pText;
}

char16_t* PowerStarList::makeCoinText(char16_t* pBuffer, const GalaxyStatusAccessor* pAccessor, bool isFirstLine) const {
    if (!isFirstLine) {
        *pBuffer = u'\n';
        pBuffer++;
    }

    s32 coinScore = MR::getCoinBestScore(pAccessor->getName());
    // Three digits: a save's score outside 0..999 is held to them.
    if (coinScore < 0) {
        coinScore = 0;
    } else if (coinScore > 999) {
        coinScore = 999;
    }

    pBuffer = MR::addPictureFontTag(pBuffer, u'A');

    *pBuffer = coinScore / 100 + u'0';

    coinScore -= coinScore / 100 * 100;
    s32 value = coinScore / 10;

    pBuffer[1] = value + u'0';
    coinScore -= 10 * value;
    pBuffer[2] = coinScore + u'0';
    pBuffer[3] = u'\0';

    return pBuffer + 3;
}

char16_t* PowerStarList::makeCrownText(char16_t* pBuffer, const GalaxyStatusAccessor* pAccessor, bool isFirstLine) const {
    if (!isFirstLine) {
        *pBuffer = u'\n';
        pBuffer++;
    }

    if (MR::isStarComplete(pAccessor->getName())) {
        *pBuffer = u'P';
        pBuffer++;
    }

    *pBuffer = u'\0';
    return pBuffer;
}

u32 PowerStarList::makeRaceNameText(MR::Text16& rBuffer, int raceId, bool isFirstLine) const {
    return addLine(rBuffer, MR::getRaceNameOnCurrentLanguage(raceId), isFirstLine);
}

u32 PowerStarList::makeRaceTimeText(MR::Text16& rBuffer, int raceId, bool isFirstLine) const {
    MR::Text16 raceBest;
    return addLine(rBuffer, MR::makeRaceBestTimeString(raceBest, raceId), isFirstLine);
}

void PowerStarList::startScrollAnimNext(bool set) {
    const char* pAnimName = mPageNo % 2 ? "TurnOverPage2" : "TurnOverPage1";

    MR::startPaneAnim(this, "List1", pAnimName, 0);
    MR::startPaneAnim(this, "List2", pAnimName, 0);
    MR::startPaneAnim(this, "PicBG", pAnimName, 0);
    MR::startPaneAnim(this, "Belt", pAnimName, 0);

    if (set) {
        MR::setPaneAnimFrameAndStop(this, "List1", 0.0f, 0);
        MR::setPaneAnimFrameAndStop(this, "List2", 0.0f, 0);
        MR::setPaneAnimFrameAndStop(this, "PicBG", 0.0f, 0);
        MR::setPaneAnimFrameAndStop(this, "Belt", 0.0f, 0);
    }
}

void PowerStarList::startScrollAnimPrev() {
    const char* pAnimName = mPageNo % 2 ? "TurnOverPage2" : "TurnOverPage1";

    MR::startPaneAnimReverseOneTime(this, "List1", pAnimName, 0);
    MR::startPaneAnimReverseOneTime(this, "List2", pAnimName, 0);
    MR::startPaneAnimReverseOneTime(this, "PicBG", pAnimName, 0);
    MR::startPaneAnimReverseOneTime(this, "Belt", pAnimName, 0);
}

bool PowerStarList::tryShowSeparator(s32 separatorIdx, f32 myFloat) {
    // A page with no galaxy row (the race page) has no separator yet (-1).
    if (separatorIdx < 0 || separatorIdx >= 4) {
        return false;
    }

    const char* pPaneName = ::cSeparatorPaneTable[separatorIdx];

    if (MR::isHiddenPane(this, pPaneName)) {
        MR::showPaneRecursive(this, pPaneName);
        mSeparatorArray[separatorIdx].mHeight = myFloat;

        return true;
    }

    return false;
}

void PowerStarList::setSeparatorPaneSize() {
    const char* pPaneName;
    for (u32 idx = 0; idx < ARRAY_SIZE(::cSeparatorSizePaneTable); idx++) {
        pPaneName = ::cSeparatorSizePaneTable[idx];

        if (MR::isHiddenPane(this, pPaneName)) {
            continue;
        }

        Ui::Pane* pPane = MR::getPane(this, pPaneName);
        Ui::Size size = pPane->mSize;
        size.height = mSeparatorArray[idx].mHeight;
        pPane->mSize = size;
    }
}

void PowerStarList::exeAppear() {
    ButtonPaneController* pController;

    if (MR::isFirstStep(this)) {
        pController = mArrowUpButtonCtrl;
        pController->appear();
        pController->mIsActive = false;

        pController = mArrowDownButtonCtrl;
        pController->appear();
        pController->mIsActive = false;

        updateButtonAppearance();
        MR::startAnim(this, "Appear", 0);
    }

    if (MR::isAnimStopped(this, 0)) {
        mArrowUpButtonCtrl->mIsActive = true;
        mArrowDownButtonCtrl->mIsActive = true;
        setNerve(GET_NERVE(PowerStarList, PowerStarListNrvWait));
    }
}

void PowerStarList::exeWait() {
    if (MR::isFirstStep(this)) {
        startScrollAnimNext(true);
        updateList(-1, false);
        updateButtonAppearance();
    }

    if (mPageNo < mPageNum - 1 && (mArrowDownButtonCtrl->trySelect() || MR::isHeldDown(0) || MR::isMoveStickPressedDown(0))) {
        setNerve(GET_NERVE(PowerStarList, PowerStarListNrvPageNext));
    } else if (mPageNo > 0 && (mArrowUpButtonCtrl->trySelect() || MR::isHeldUp(0) || MR::isMoveStickPressedUp(0))) {
        setNerve(GET_NERVE(PowerStarList, PowerStarListNrvPagePrev));
    }
}

void PowerStarList::exeDisappear() {
    if (MR::isFirstStep(this)) {
        mArrowUpButtonCtrl->mIsActive = false;
        mArrowDownButtonCtrl->mIsActive = false;

        updateList(-1, true);

        MR::startAnim(this, mPageNo % 2 ? "End2" : "End", 0);
    }

    MR::killAtAnimStopped(this, 0);
}

void PowerStarList::exePageNext() {
    if (MR::isFirstStep(this)) {
        startScrollAnimNext(false);
        mPageNo++;
        MR::startSystemSE("SE_SY_GALAMAP_SCROLL");
    }

    if (MR::isPaneAnimStopped(this, "List1", 0) && MR::isPaneAnimStopped(this, "List2", 0)) {
        resetButtonAll();
        setNerve(GET_NERVE(PowerStarList, PowerStarListNrvWait));
    }
}

void PowerStarList::exePagePrev() {
    if (MR::isFirstStep(this)) {
        updateList(mPageNo - 1, false);
        startScrollAnimPrev();
        MR::startSystemSE("SE_SY_GALAMAP_SCROLL");
    }

    if (MR::isPaneAnimStopped(this, "List1", 0) && MR::isPaneAnimStopped(this, "List2", 0)) {
        resetButtonAll();
        setNerve(GET_NERVE(PowerStarList, PowerStarListNrvWait));
    }
}

