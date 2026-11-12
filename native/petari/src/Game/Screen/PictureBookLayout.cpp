#include "Game/Screen/PictureBookLayout.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaFrameGen.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/ButtonPaneController.hpp"
#include "Game/Screen/IconAButton.hpp"
#include "Game/Screen/PictureBookCloseButton.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include "Game/System/LumaCore.hpp"

namespace {
    inline void advanceBookIndex(s32& rIndex, const s32& rDirection) {
        rIndex += rDirection;
    }

    const s32 cBookOpenFrame = 60;
    // const s32 cBookCloseFrame
    const s32 cFadeFrame = 60;
    const s32 cWaitNoTextFrame = NebulaFrameClock::stepsOfSeconds(1);
    const s32 cFadeTextFrame = 30;
    // const s32 cReadedSpeedRate
    const s32 cPageNextEndNormalSeStep = 81;
    const s32 cPageNextEndFastSeStep = 27;
    const char* const cContentsPaneName[] = {
        "Chapter1", "Chapter2", "Chapter3", "Chapter4", "Chapter5", "Chapter6", "Chapter7", "Chapter8", "Chapter9",
    };
    const char* const cContentsPointingPaneName[] = {
        "BoxButton1", "BoxButton2", "BoxButton3", "BoxButton4", "BoxButton5", "BoxButton6", "BoxButton7", "BoxButton8", "BoxButton9",
    };

    s32 getTextureNum(s32 chapterNo) {
        s32 textureNo = 0;

        const MR::FormattedName chapterArcName("PictureBookChapter%d", chapterNo);

        for (;;) {
            const MR::FormattedName pageTexName("Chapter%dPage%d.bti", chapterNo, textureNo + 1);

            if (!MR::isExistResourceInArc(chapterArcName.c_str(), pageTexName.c_str())) {
                break;
            }

            textureNo++;
        }

        return textureNo;
    }

    s32 getPageNum(s32 chapterNo) {
        for (s32 pageNo = 0; pageNo < 100; pageNo++) {
            const MR::FormattedName messageId("PictureBookChapter%d_Page%d_%03d", chapterNo, pageNo + 1, 0);

            if (MR::isExistGameMessage(messageId.c_str())) {
                continue;
            }

            return pageNo;
        }

        return 1;
    }
};  // namespace

namespace NrvPictureBookLayout {
    NEW_NERVE(PictureBookLayoutNrvOpen, PictureBookLayout, Open);
    NEW_NERVE(PictureBookLayoutNrvContentsSelect, PictureBookLayout, ContentsSelect);
    NEW_NERVE(PictureBookLayoutNrvContentsFadeOut, PictureBookLayout, ContentsFadeOut);
    NEW_NERVE(PictureBookLayoutNrvFadeIn, PictureBookLayout, FadeIn);
    NEW_NERVE(PictureBookLayoutNrvWaitNoText, PictureBookLayout, WaitNoText);
    NEW_NERVE(PictureBookLayoutNrvFadeInText, PictureBookLayout, FadeInText);
    NEW_NERVE(PictureBookLayoutNrvWaitWithText, PictureBookLayout, WaitWithText);
    NEW_NERVE(PictureBookLayoutNrvFadeOutText, PictureBookLayout, FadeOutText);
    NEW_NERVE(PictureBookLayoutNrvPageNext, PictureBookLayout, PageNext);
    NEW_NERVE(PictureBookLayoutNrvFadeOut, PictureBookLayout, FadeOut);
    NEW_NERVE(PictureBookLayoutNrvClose, PictureBookLayout, Close);
};  // namespace NrvPictureBookLayout

PictureBookLayout::PictureBookLayout(s32 chapterMin, s32 chapterMax, bool isRosettaReading)
    : LayoutActor("絵本レイアウト", true), mChapterMin(chapterMin), mChapterMax(chapterMax), mChapterRosettaMax(chapterMax), mChapterNo(1), mPageNo(),
      mTextIndex(), mNotReadedChapterNo(-1), mNotReadedPageNo(-1), mNotReadedTextIndex(-1), mTexMaps(), mChapterTexMaps(), mTitleTexMap(), mCoverFrontTexMap(),
      mCoverBackTexMap(), mNextItemDir(1), mIsNextItemFast(), mIconAButton(), mContentsButtonPaneController(), mCloseButton() {
    lumacore::setClass(this, lumacore::kClassId< PictureBookLayout >);
    if (!isRosettaReading) {
        mContentsButtonPaneController = new ButtonPaneController*[getChapterMax()];
    }
}

void PictureBookLayout::init(const TableIter& /*rIter*/) {
    initLayoutManagerWithTextBoxBufferLength("PictureBook", 512, 1);
    MR::createAndAddPaneCtrl(this, "AButtonPosition", 1);
    MR::createAndAddPaneCtrl(this, "Text", 1);
    MR::connectToSceneLayout(this);
    initTexture();

    mIconAButton = MR::createAndSetupIconAButton(this, true, false);

    if (mContentsButtonPaneController != nullptr) {
        initContentsButton();

        mCloseButton = new PictureBookCloseButton(true);
        mCloseButton->initWithoutIter();
        // While a page shows, A and the D-pad turn the pages: NebulaMenuFocus leaves the close button to the pointer.
        mCloseButton->getPaneCtrl()->mIsNebulaFocusable = false;
    }

    initNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeIn));
}

void PictureBookLayout::appear() {
    mChapterNo = mChapterMin;
    mChapterMax = getChapterNumberMax();
    mPageNo = 0;
    mTextIndex = 0;
    mNotReadedChapterNo = -1;
    mNotReadedPageNo = -1;
    mNotReadedTextIndex = -1;
    mNextItemDir = 1;
    mIsNextItemFast = false;

    if (mContentsButtonPaneController == nullptr) {
        MR::hidePaneRecursive(this, "Contents");
    } else {
        hideContents();
    }

    LayoutActor::appear();

    if (mContentsButtonPaneController == nullptr) {
        setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeIn));
    } else {
        setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvOpen));
    }

    MR::requestMovementOn(this);
    MR::requestMovementOn(mIconAButton);
    MR::pauseOnCameraDirector();
    MR::deactivateGameSceneDraw3D();
}

void PictureBookLayout::kill() {
    LayoutActor::kill();
    mIconAButton->kill();

    if (mContentsButtonPaneController != nullptr) {
        mCloseButton->kill();
    }

    MR::startCurrentStageBGM();
    MR::pauseOffCameraDirector();
    MR::activateGameSceneDraw3D();
}

s32 PictureBookLayout::getChapterMax() {
    return 9;
}

void PictureBookLayout::control() {
    NebulaFrameGen::noteFullscreenUi();
    if (mContentsButtonPaneController == nullptr) {
        return;
    }

    for (s32 i = 0; i < getChapterMax(); i++) {
        mContentsButtonPaneController[i]->update();
    }
}

void PictureBookLayout::initTexture() {
    s32 textureNum = 0;

    for (s32 c = mChapterMin; c <= mChapterMax; c++) {
        textureNum += ::getTextureNum(c);
    }

    mTexMaps = new Ui::TexMap*[textureNum];

    s32 textureIndex = 0;

    for (s32 c = mChapterMin; c <= mChapterMax; c++) {
        const MR::FormattedName chapterArcName("PictureBookChapter%d", c);

        for (s32 p = 0; p < ::getTextureNum(c); p++) {
            const MR::FormattedName pageTexName("Chapter%dPage%d.bti", c, p + 1);

            mTexMaps[textureIndex] = MR::createLytTexMap(chapterArcName.c_str(), pageTexName.c_str());
            textureIndex++;
        }
    }

    mTitleTexMap = MR::createLytTexMap("PictureBookTexture", "PictureBookTitle.bti");
    mCoverFrontTexMap = MR::createLytTexMap("PictureBookTexture", "PictureBookCoverFront.bti");
    mCoverBackTexMap = MR::createLytTexMap("PictureBookTexture", "PictureBookCoverBack.bti");
}

void PictureBookLayout::initContentsButton() {
    MR::FormattedName messageId;

    for (s32 i = 0; i < getChapterMax(); i++) {
        mContentsButtonPaneController[i] = new ButtonPaneController(this, ::cContentsPaneName[i], ::cContentsPointingPaneName[i], 0, false);
        mContentsButtonPaneController[i]->invalidateAppearance();

        messageId.format("PictureBookChapter%d_Title", i + 1);

        MR::setTextBoxGameMessageRecursive(this, ::cContentsPaneName[i], messageId.c_str());
    }
}

namespace {
    inline void setChapterTitle(LayoutActor* pActor, const char* pPaneName, s32 chapter) {
        const MR::FormattedName messageId("PictureBookChapter%d_Title", chapter);
        MR::setTextBoxGameMessageRecursive(pActor, pPaneName, messageId.c_str());
    }
};  // namespace
namespace {
    inline bool setPageText(LayoutActor* pActor, MR::FormattedName& rMessageId, const s32& rChapter, const s32& rPage, const s32& rTextIndex) {
        rMessageId.format("PictureBookChapter%d_Page%d_%03d", rChapter, rPage, rTextIndex);

        if (MR::isExistGameMessage(rMessageId.c_str())) {
            MR::setTextBoxGameMessageRecursive(pActor, "Text", rMessageId.c_str());

            return true;
        }

        return false;
    }
};  // namespace
bool PictureBookLayout::updateText() {
    if (mPageNo == 0) {
        ::setChapterTitle(this, "Title", mChapterNo);

        return true;
    } else {
        MR::FormattedName messageId;
        return ::setPageText(this, messageId, mChapterNo, mPageNo, mTextIndex);
    }
}

void PictureBookLayout::updateTexture() {
    s32 textureNum = ::getTextureNum(mChapterNo);
    s32 pageNo;

    if (mNextItemDir > 0) {
        pageNo = mPageNo - 1;
    } else {
        pageNo = mPageNo;
    }

    if (pageNo == 0) {
        Ui::TexMap* pTexMap = mTitleTexMap;

        MR::replacePaneTexture(this, "PicLeftPage", pTexMap, 0);
        MR::replacePaneTexture(this, "PicTurnRightPage", pTexMap, 0);
    } else {
        s32 texMapIndex = textureNum + pageNo - 1;
        s32 ind = (textureNum != 0 ? texMapIndex % textureNum : texMapIndex) + 1;
        Ui::TexMap* pTexMap = mChapterTexMaps[ind - 1];

        MR::replacePaneTexture(this, "PicLeftPage", pTexMap, 0);
        MR::replacePaneTexture(this, "PicTurnRightPage", pTexMap, 0);
    }

    if (mNextItemDir > 0) {
        pageNo = mPageNo;
    } else {
        pageNo = mPageNo + 1;
    }

    if (pageNo == 0) {
        Ui::TexMap* pTexMap = mTitleTexMap;

        MR::replacePaneTexture(this, "PicRightPage", pTexMap, 0);
        MR::replacePaneTexture(this, "PicTurnLeftPage", pTexMap, 0);
    } else {
        s32 texMapIndex = textureNum + pageNo - 1;
        s32 ind = (textureNum != 0 ? texMapIndex % textureNum : texMapIndex) + 1;
        Ui::TexMap* pTexMap = mChapterTexMaps[ind - 1];

        MR::replacePaneTexture(this, "PicRightPage", pTexMap, 0);
        MR::replacePaneTexture(this, "PicTurnLeftPage", pTexMap, 0);
    }
}

bool PictureBookLayout::textNext() {
    if (mPageNo == 0) {
        return false;
    }

    mTextIndex += mNextItemDir;

    if (mTextIndex < 0) {
        return false;
    }

    return updateText();
}

bool PictureBookLayout::pageNext() {
    ::advanceBookIndex(mPageNo, mNextItemDir);

    if (mPageNo < 0) {
        return false;
    }

    if (mPageNo != 0) {
        if (mNextItemDir > 0) {
            mTextIndex = 0;
        } else {
            mTextIndex = getCurrentMaxTextIndex();
        }
    }

    return updateText();
}

bool PictureBookLayout::chapterNext() {
    ::advanceBookIndex(mChapterNo, mNextItemDir);

    if (mChapterMax < mChapterNo) {
        return false;
    }

    if (mNextItemDir > 0) {
        mPageNo = 0;
        mTextIndex = 0;
    } else {
        mPageNo = ::getPageNum(mChapterNo);
        mTextIndex = getCurrentMaxTextIndex();
    }

    return true;
}

void PictureBookLayout::updateTexMapChapterBase() {
    mChapterTexMaps = mTexMaps;

    for (s32 c = mChapterMin; c <= mChapterMax; c++) {
        if (c == mChapterNo) {
            break;
        }

        mChapterTexMaps = &mChapterTexMaps[::getTextureNum(c)];
    }
}

bool PictureBookLayout::isReadedCurrentText() const {
    if (mContentsButtonPaneController != nullptr) {
        return true;
    }

    return mChapterNo < mNotReadedChapterNo || (mChapterNo == mNotReadedChapterNo && mPageNo < mNotReadedPageNo) ||
           (mChapterNo == mNotReadedChapterNo && mPageNo == mNotReadedPageNo && mTextIndex <= mNotReadedTextIndex);
}

s32 PictureBookLayout::getReadSpeed() const {
    bool b = mContentsButtonPaneController != nullptr || mIsNextItemFast;

    if (b) {
        return 3;
    } else {
        return 1;
    }
}

bool PictureBookLayout::isBookEndCurrentText() const {
    bool r31 = false;
    bool r30 = false;

    if (mChapterNo == 9) {
        if (mPageNo == ::getPageNum(mChapterNo)) {
            r30 = true;
        }
    }

    if (r30) {
        if (getCurrentMaxTextIndex() == mTextIndex) {
            r31 = true;
        }
    }

    return r31;
}

void PictureBookLayout::setTextAlpha(f32 alpha) {
    MR::setPaneAlphaFloat(this, "Text", alpha);
    MR::setPaneAlphaFloat(this, "Title", alpha);
    MR::setPaneAlphaFloat(this, "Contents", alpha);
}

s32 PictureBookLayout::getChapterNumberMax() const {
    if (mContentsButtonPaneController == nullptr) {
        return mChapterRosettaMax;
    }

    // A save that counts chapters the book lacks (or fewer than none) is held to the book's nine.
    const s32 chapters = MR::getPictureBookChapterAlreadyRead();
    return chapters > getChapterMax() ? getChapterMax() : (chapters < 0 ? 0 : chapters);
}

bool PictureBookLayout::isValidCloseButton() const {
    if (mContentsButtonPaneController == nullptr) {
        return false;
    }

    return isNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvOpen)) ||
           isNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvContentsSelect)) ||
           isNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvOpen)) || mPageNo == 0;
}

bool PictureBookLayout::isSelectedCloseButton() const {
    return isValidCloseButton() && mCloseButton->isSelected();
}

s32 PictureBookLayout::getCurrentMaxTextIndex() const {
    s32 chapterNo;
    s32 pageNo;

    pageNo = mPageNo;
    chapterNo = mChapterNo;

    for (s32 i = 0; i < 100; i++) {
        const MR::FormattedName messageId("PictureBookChapter%d_Page%d_%03d", chapterNo, pageNo, i);

        if (MR::isExistGameMessage(messageId.c_str())) {
            continue;
        }

        return i - 1;
    }

    return 0;
}

void PictureBookLayout::exeOpen() {
    Ui::TexMap* pTexMap;

    if (MR::isFirstStep(this)) {
        updateTexMapChapterBase();
        updateText();
        MR::hidePaneRecursive(this, "Text");
        MR::hidePaneRecursive(this, "Title");
        MR::hidePaneRecursive(this, "PicToneDown");
        pTexMap = mCoverFrontTexMap;
        MR::replacePaneTexture(this, "PicLeftPage", pTexMap, 0);
        MR::replacePaneTexture(this, "PicTurnRightPage", pTexMap, 0);
        pTexMap = mTitleTexMap;
        MR::replacePaneTexture(this, "PicRightPage", pTexMap, 0);
        MR::replacePaneTexture(this, "PicTurnLeftPage", pTexMap, 0);
        MR::startAnim(this, "Appear", 0);
        MR::setAnimFrameAndStop(this, 0.0f, 0);
        MR::openWipeFade(::cFadeFrame);
        MR::startStageBGM("STM_PROLOGUE_01", false);
    }

    if (MR::isStep(this, ::cBookOpenFrame)) {
        MR::setAnimRate(this, 1.0f, 0);
    }

    if (MR::isStep(this, 140)) {
        MR::startSystemSE("SE_SY_PICTUREBOOK_NEXT_ST");
    }

    s32 animFrameMax = MR::getAnimFrameMax(this, (u32)0);
    s32 stepMin = animFrameMax + ::cBookOpenFrame;
    s32 stepMax = stepMin + ::cFadeTextFrame;

    if (MR::isStep(this, stepMin)) {
        for (s32 i = 0; i < mChapterMax; i++) {
            mContentsButtonPaneController[i]->appear();
        }

        mCloseButton->appear();
    }

    if (MR::isGreaterEqualStep(this, stepMin)) {
        setTextAlpha(MR::calcNerveRate(this, stepMin, stepMax));

        if (isValidCloseButton()) {
            MR::requestStarPointerModePictureBook(this);
        }
    }

    MR::setNerveAtStep(this, GET_NERVE(PictureBookLayout, PictureBookLayoutNrvContentsSelect), stepMax);
}

void PictureBookLayout::exeContentsSelect() {
    if (isValidCloseButton()) {
        MR::requestStarPointerModePictureBook(this);
    }

    if (mCloseButton->trySelect()) {
        setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvContentsFadeOut));
    } else {
        for (s32 i = 0; i < mChapterMax; i++) {
            if (mContentsButtonPaneController[i]->isPointingTrigger()) {
                MR::startSystemSE("SE_SY_PICBOOK_CONTENTS_CUR");
            }

            if (mContentsButtonPaneController[i]->trySelect()) {
                MR::startSystemSE("SE_SY_TALK_OK");
                mChapterNo = i + 1;
                mCloseButton->disappear();
                setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvContentsFadeOut));
                break;
            }
        }
    }
}

void PictureBookLayout::exeContentsFadeOut() {
    s32 stepMin = MR::getPaneAnimFrameMax(this, ::cContentsPaneName[mChapterNo - 1], 0);
    s32 stepMax = stepMin + ::cFadeTextFrame;

    if (MR::isGreaterEqualStep(this, stepMin)) {
        setTextAlpha(1.0f - MR::calcNerveRate(this, stepMin, stepMax));
    }

    if (MR::isLessStep(this, stepMax)) {
        if (isValidCloseButton()) {
            MR::requestStarPointerModePictureBook(this);
        }
    }

    if (isSelectedCloseButton()) {
        if (MR::isGreaterStep(this, stepMax)) {
            if (MR::isDead(mCloseButton)) {
                hideContents();
                setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvClose));
            }
        }
    } else {
        if (MR::isStep(this, stepMax)) {
            MR::closeWipeFade(::cFadeFrame);
        }

        if (MR::isGreaterStep(this, stepMax) && !MR::isWipeActive()) {
            hideContents();
            setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeIn));
        }
    }
}

void PictureBookLayout::exeFadeIn() {
    if (MR::isFirstStep(this)) {
        updateTexMapChapterBase();
        updateText();
        MR::hidePaneRecursive(this, "Text");
        MR::hidePaneRecursive(this, "Title");
        MR::hidePaneRecursive(this, "PicToneDown");
        MR::showPaneRecursive(this, "PicToneDown");

        f32 alpha;

        if (mNextItemDir > 0) {
            alpha = 0.0f;
        } else {
            alpha = 1.0f;
        }

        MR::setPaneAlphaFloat(this, "PicToneDown", alpha);
        updateTexture();
        MR::startAnim(this, "PageNext", 0);

        f32 animFrame;

        if (mNextItemDir > 0) {
            animFrame = MR::getAnimFrameMax(this, (u32)0);
        } else {
            animFrame = 0.0f;
        }

        MR::setAnimFrameAndStop(this, animFrame, 0);
        MR::openWipeFade(::cFadeFrame / getReadSpeed());

        if (mContentsButtonPaneController == nullptr) {
            MR::startStageBGM("STM_PROLOGUE_01", false);
        }
    }

    if (!MR::isWipeActive()) {
        setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvWaitNoText));
    }
}

void PictureBookLayout::exeWaitNoText() {
    bool b = mContentsButtonPaneController != nullptr || mIsNextItemFast;

    MR::setNerveAtStep(this, GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeInText), b ? 0 : ::cWaitNoTextFrame);
}

void PictureBookLayout::exeFadeInText() {
    if (MR::isFirstStep(this)) {
        if (mPageNo == 0) {
            MR::showPaneRecursive(this, "Title");

            if (mContentsButtonPaneController != nullptr) {
                mCloseButton->appear();
            }
        } else {
            MR::showPaneRecursive(this, "Text");
            MR::showPaneRecursive(this, "PicToneDown");
            MR::startPaneAnim(this, "Text", "TextColor", 0);

            if (isReadedCurrentText()) {
                MR::setPaneAnimFrameAndStop(this, "Text", 1.0f, 0);
            } else {
                MR::setPaneAnimFrameAndStop(this, "Text", 0.0f, 0);
            }
        }

        if (mChapterNo == 7) {
            if (mPageNo <= 5 && MR::isStopOrFadeoutBgmName("STM_PROLOGUE_01")) {
                MR::startStageBGM("STM_PROLOGUE_01", false);
            }

            if (mPageNo == 6) {
                MR::stopStageBGM(120);
            }

            if (mPageNo == 7 && MR::isStopOrFadeoutBgmName("STM_PROLOGUE_01_B")) {
                MR::startStageBGM("STM_PROLOGUE_01_B", false);
            }

            if (mPageNo == 8) {
                if (mTextIndex >= 1) {
                    MR::stopStageBGM(120);
                } else if (MR::isStopOrFadeoutBgmName("STM_PROLOGUE_01_B")) {
                    MR::startStageBGM("STM_PROLOGUE_01_B", false);
                }
            }
        } else if (MR::isStopOrFadeoutBgmName("STM_PROLOGUE_01")) {
            MR::startStageBGM("STM_PROLOGUE_01", 0);
        }

        if (isBookEndCurrentText()) {
            MR::setTextBoxHorizontalPositionCenterRecursive(this, "Text");
            MR::setTextBoxVerticalPositionCenterRecursive(this, "Text");
        } else {
            MR::setTextBoxHorizontalPositionLeftRecursive(this, "Text");
            MR::setTextBoxVerticalPositionTopRecursive(this, "Text");
        }

        if (mIsNextItemFast && !isReadedCurrentText()) {
            mIsNextItemFast = false;
        }
    }

    if (isValidCloseButton()) {
        MR::requestStarPointerModePictureBook(this);
    }

    // Half a second at the normal reading speed.
    s32 step = NebulaFrameClock::stepsOfSeconds(1) / 2 / getReadSpeed();
    f32 alpha = MR::calcNerveRate(this, step);

    MR::setPaneAlphaFloat(this, "Text", alpha);
    MR::setPaneAlphaFloat(this, "Title", alpha);
    MR::setPaneAlphaFloat(this, "Contents", alpha);
    MR::setPaneAlphaFloat(this, "PicToneDown", getFadeInAlphaTextBG(alpha));
    MR::setNerveAtStep(this, GET_NERVE(PictureBookLayout, PictureBookLayoutNrvWaitWithText), step);
}

void PictureBookLayout::exeWaitWithText() {
    if (MR::isFirstStep(this)) {
        mIconAButton->openWithTurn();
    }

    if (isValidCloseButton()) {
        MR::requestStarPointerModePictureBook(this);
    }

    if (isValidCloseButton() && mCloseButton->trySelect()) {
        setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeOutText));
    } else {
        bool isTriggerNextPage =
            MR::isPressedJump(PLAYER_1) || MR::isPressedRight(PLAYER_1) || MR::isMoveStickPressedRight(PLAYER_1);
        // A skipped reading (Rosalina's, a cutscene) turns each page as soon as it is up.
        isTriggerNextPage = isTriggerNextPage || NebulaCutsceneSkip::isSkipping();

        if (isTriggerNextPage) {
            MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");

            if (mNextItemDir > 0 && !isReadedCurrentText()) {
                mNotReadedChapterNo = mChapterNo;
                mNotReadedPageNo = mPageNo;
                mNotReadedTextIndex = mTextIndex;
            }

            if (isValidCloseButton()) {
                mCloseButton->disappear();
            }

            mNextItemDir = 1;

            setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeOutText));
        } else {
            bool isTriggerPrevPage = MR::isPressedLeft(PLAYER_1) || MR::isMoveStickPressedLeft(PLAYER_1);

            if (isTriggerPrevPage) {
                if (mChapterMin < mChapterNo || mPageNo > 0 || mTextIndex > 0) {
                    MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");

                    if (isValidCloseButton()) {
                        mCloseButton->disappear();
                    }

                    mIsNextItemFast = true;
                    mNextItemDir = -1;

                    setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeOutText));
                }
            }
        }
    }
}

void PictureBookLayout::exeFadeOutText() {
    if (MR::isFirstStep(this)) {
        mIconAButton->term();
    }

    if (isValidCloseButton()) {
        MR::requestStarPointerModePictureBook(this);
    }

    s32 step = NebulaFrameClock::stepsOfSeconds(1) / 2 / getReadSpeed();
    f32 alpha = 1.0f - MR::calcNerveRate(this, step);

    MR::setPaneAlphaFloat(this, "Text", alpha);
    MR::setPaneAlphaFloat(this, "Title", alpha);
    MR::setPaneAlphaFloat(this, "Contents", alpha);
    MR::setPaneAlphaFloat(this, "PicToneDown", getFadeOutAlphaTextBG(alpha));

    if (isSelectedCloseButton()) {
        if (MR::isGreaterStep(this, step) && MR::isDead(mCloseButton)) {
            setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvClose));
        }
    } else if (MR::isStep(this, step)) {
        if (textNext()) {
            setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeInText));
        } else if (pageNext()) {
            setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvPageNext));
        } else if (chapterNext()) {
            if (MR::isPlayingStageBgmName("STM_PROLOGUE_01_B")) {
                MR::stopStageBGM(120);
            }

            setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeOut));
        } else if (mContentsButtonPaneController == nullptr) {
            setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeOut));
        } else {
            setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvClose));
        }
    }
}

void PictureBookLayout::exePageNext() {
    if (MR::isFirstStep(this)) {
        MR::hidePaneRecursive(this, "Text");
        MR::hidePaneRecursive(this, "Title");
        MR::hidePaneRecursive(this, "PicToneDown");
        updateTexture();

        if (mNextItemDir > 0) {
            MR::startAnim(this, "PageNext", 0);
        } else {
            MR::startAnimReverseOneTime(this, "PageNext", 0);
        }

        if (mIsNextItemFast) {
            MR::startSystemSE("SE_SY_PICTUREBOOK_NEXT_F_ST");
        } else {
            MR::startSystemSE("SE_SY_PICTUREBOOK_NEXT_ST");
        }

        MR::setAnimRate(this, mNextItemDir * getReadSpeed(), 0);
    }

    if (mIsNextItemFast) {
        if (MR::isStep(this, ::cPageNextEndFastSeStep)) {
            MR::startSystemSE("SE_SY_PICTUREBOOK_NEXT_F_ED");
        }
    } else if (MR::isStep(this, ::cPageNextEndNormalSeStep)) {
        MR::startSystemSE("SE_SY_PICTUREBOOK_NEXT_ED");
    }

    MR::setNerveAtAnimStopped(this, GET_NERVE(PictureBookLayout, PictureBookLayoutNrvWaitNoText), 0);
}

void PictureBookLayout::exeFadeOut() {
    if (MR::isFirstStep(this)) {
        MR::closeWipeFade(::cFadeFrame / getReadSpeed());

        if (mContentsButtonPaneController == nullptr) {
            MR::stopStageBGM(::cFadeFrame / getReadSpeed());
        }
    }

    if (MR::isWipeActive()) {
        return;
    }

    if (mChapterMax >= mChapterNo) {
        setNerve(GET_NERVE(PictureBookLayout, PictureBookLayoutNrvFadeIn));
    } else {
        kill();
    }
}

void PictureBookLayout::exeClose() {
    Ui::TexMap* pTexMap;

    if (MR::isFirstStep(this)) {
        MR::hidePaneRecursive(this, "Text");
        MR::hidePaneRecursive(this, "Title");
        MR::hidePaneRecursive(this, "PicToneDown");
        MR::startAnim(this, "End", 0);

        if (isSelectedCloseButton()) {
            pTexMap = mTitleTexMap;

            MR::replacePaneTexture(this, "PicLeftPage", pTexMap, 0);
            MR::replacePaneTexture(this, "PicTurnRightPage", pTexMap, 0);
        } else {
            pTexMap = mChapterTexMaps[mPageNo - 2];

            MR::replacePaneTexture(this, "PicLeftPage", pTexMap, 0);
            MR::replacePaneTexture(this, "PicTurnRightPage", pTexMap, 0);
        }

        pTexMap = mCoverBackTexMap;

        MR::replacePaneTexture(this, "PicRightPage", pTexMap, 0);
        MR::replacePaneTexture(this, "PicTurnLeftPage", pTexMap, 0);
    }

    s32 step = MR::getAnimFrameMax(this, (u32)0);

    if (MR::isStep(this, step)) {
        MR::closeWipeFade(::cFadeFrame);
        MR::stopStageBGM(::cFadeFrame);
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfMilliseconds(2500))) {
        MR::startSystemSE("SE_SY_PICTUREBOOK_END");
    }

    if (MR::isGreaterStep(this, step)) {
        if (!MR::isWipeActive()) {
            kill();
        }
    }
}

void PictureBookLayout::hideContents() {
    for (s32 i = 0; i < getChapterMax(); i++) {
        mContentsButtonPaneController[i]->forceToHide();
    }
}

f32 PictureBookLayout::getFadeInAlphaTextBG(f32 alpha) const {
    bool var;

    if (!mPageNo || isBookEndCurrentText()) {
        return 0.0f;
    }

    var = false;

    if (mNextItemDir > 0) {
        if (!mTextIndex) {
            var = true;
        }
    } else if (getCurrentMaxTextIndex() == mTextIndex) {
        if (mPageNo < ::getTextureNum(mChapterNo)) {
            var = true;
        }
    }

    if (var) {
        return alpha;
    }

    return 1.0f;
}

f32 PictureBookLayout::getFadeOutAlphaTextBG(f32 alpha) const {
    bool var;

    if (!mPageNo || isBookEndCurrentText()) {
        return 0.0f;
    }

    var = false;

    if (mNextItemDir > 0) {
        if (getCurrentMaxTextIndex() == mTextIndex) {
            if (mPageNo < ::getTextureNum(mChapterNo)) {
                var = true;
            }
        }
    } else if (mTextIndex == 0 && mPageNo > 0) {
        var = true;
    }

    if (var) {
        return alpha;
    }

    return 1.0f;
}
