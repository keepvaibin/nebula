#include "Game/Screen/NebulaSettingsMenu.hpp"

#ifdef TARGET_PC
#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>
#include <nw4r/lyt/layout.h>
#include <revolution/os.h>
#include <revolution/wpad.h>
#include <wstring.h>

using namespace NebulaUi;

namespace {
    // Screen units (NebulaUi::readScreen): 456 high, 810 wide at 16:9.
    const f32 cMargin = 22.0f;     // the window from the screen's edges
    const f32 cMaxWidth = 900.0f;  // a wide screen keeps a window that reads as one
    const f32 cPad = 26.0f;        // the window's inside
    const f32 cTitleSize = 21.0f;
    const f32 cTabHeight = 28.0f;
    const f32 cTabSize = 12.5f;
    const f32 cTabGap = 5.0f;
    const f32 cRowHeight = 26.0f;
    const f32 cTextSize = 13.5f;
    const f32 cHeadingSize = 12.0f;
    const f32 cFooterSize = 12.0f;
    const f32 cFooterLine = 19.0f;
    const f32 cArrowSize = 8.0f;
    const f32 cArrowGap = 9.0f;
    const f32 cButtonHeight = 28.0f;
    const f32 cButtonSize = 12.5f;
    const f32 cButtonGap = 10.0f;
    const f32 cMinSqueeze = 0.6f;  // the narrowest a line of text is drawn
    const f32 cKeyGap = 6.0f;  // a key from its word
    const f32 cHintGap = 18.0f;
    const f32 cQuestionWidth = 470.0f;
    const f32 cQuestionHeight = 180.0f;
    const s32 cDropFrames = 16;  // the window's drop from above, and its rise away
    const s32 cRowsMax = NEBULA_SETTINGS_ROWS;
    const s32 cHintsMax = 3;

    // The quit question's window and text; the game's blue for what is chosen.
    const u32 cText = 0x4A5274FF;
    const u32 cSubText = 0x737B9AFF;
    const u32 cAccent = 0x2F6FD0FF;
    const u32 cDisabled = 0x9AA2BCFF;
    const u32 cWarning = 0xC8402EFF;
    const u32 cHighlightFill = 0xFFFFFFFF;
    const u32 cHighlightRing = 0x7EC8F4FF;
    const u32 cRule = 0xB4C2DEFF;
    const u32 cTrack = 0x4A527428;
    const u32 cThumb = 0x7EA8E8FF;
    const u32 cButtonText = 0x505050FF;

    enum { FooterApply, FooterBack, FooterNum };
    enum { ChoiceNum = 3 };

    // What the hints and buttons name a key or button for. KeyAction: a row that does something (opens a folder,
    // imports) does it with Right or the bottom button.
    enum { KeyApply, KeyUndo, KeyBack, KeyPage, KeyChange, KeyBind, KeyAction, KeyNum };

    // The questions the host asks over the page (NEBULA_SETTINGS_STATUS_*), with their answers: each one's word, what it
    // sends and the key or button that gives it.
    struct Question {
        u32 mStatus;
        const char* mText;     // with {n}: the status's number
        const char* mTextOne;  // when the number is 1
        s32 mChoiceCount;
        const char* mWords[ChoiceNum];
        s32 mActions[ChoiceNum];
        s32 mKeys[ChoiceNum];
    };
    const Question cQuestions[] = {
        {NEBULA_SETTINGS_STATUS_LEAVE, "SettingsLeave", "SettingsLeaveOne", 3,
         {"SettingsChoiceApply", "SettingsChoiceDiscard", "SettingsChoiceStay"},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_REVERT, NEBULA_SETTINGS_CLOSE},
         {KeyApply, KeyUndo, KeyBack}},
        {NEBULA_SETTINGS_STATUS_KEEP_DISPLAY, "SettingsKeepDisplay", "SettingsKeepDisplayOne", 2,
         {"SettingsChoiceKeep", "SettingsChoiceUndo", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
        {NEBULA_SETTINGS_STATUS_DLSS_ASK, "SettingsDlssAsk", "SettingsDlssAsk", 2,
         {"SettingsChoiceDownload", "SettingsChoiceNotNow", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
        {NEBULA_SETTINGS_STATUS_DLSS_RESTART, "SettingsDlssRestart", "SettingsDlssRestart", 2,
         {"SettingsChoiceRestart", "SettingsChoiceLater", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
    };
    const s32 cQuestionNum = sizeof(cQuestions) / sizeof(cQuestions[0]);

    struct Row {
        Pane* mRoot;
        TextBox* mLabel;
        TextBox* mValue;
        TextBox* mLeft;
        TextBox* mRight;
        Window* mRule;
        f32 mLabelWidth;
        f32 mValueWidth;
        bool mIndented;
    };

    // A button in the game's art with its key before its word ("Enter  Apply").
    struct Button {
        Chip mChip;
        TextBox* mKey;
        f32 mKeyWidth;
        f32 mWordWidth;
        s32 mAction;
    };

    struct Hint {
        TextBox* mKey;
        TextBox* mWord;
        f32 mKeyWidth;
        f32 mWordWidth;
    };

    // UTF-8 (the host's text) as the game's 16-bit text.
    void fromUtf8(wchar_t* pDst, s32 capacity, const char* pSrc) {
        s32 length = 0;
        const u8* p = reinterpret_cast< const u8* >(pSrc);
        while (*p != 0 && length + 1 < capacity) {
            u32 c = *p++;
            if (c >= 0xE0 && p[0] != 0 && p[1] != 0) {
                c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
                p += 2;
            } else if (c >= 0xC0 && p[0] != 0) {
                c = ((c & 0x1F) << 6) | (p[0] & 0x3F);
                p += 1;
            }
            pDst[length++] = static_cast< wchar_t >(c);
        }
        pDst[length] = 0;
    }

    // `pPattern` with each {0} to {3} as that argument and {n} as the first.
    void expand(wchar_t* pDst, s32 capacity, const wchar_t* pPattern, const wchar_t* const* ppArgs, s32 argCount) {
        s32 length = 0;
        for (const wchar_t* s = pPattern; *s != 0 && length + 1 < capacity; s++) {
            s32 index = -1;
            if (s[0] == L'{' && s[1] != 0 && s[2] == L'}') {
                index = s[1] == L'n' ? 0 : s[1] >= L'0' && s[1] <= L'3' ? s[1] - L'0' : -1;
            }
            if (index < 0) {
                pDst[length++] = *s;
                continue;
            }
            for (const wchar_t* p = index < argCount ? ppArgs[index] : L""; *p != 0 && length + 1 < capacity; p++) {
                pDst[length++] = *p;
            }
            s += 2;
        }
        pDst[length] = 0;
    }

    // NebulaText's `pId` with its {n} or {0}... as the arguments.
    void getExpanded(wchar_t* pDst, s32 capacity, const char* pId, const wchar_t* const* ppArgs, s32 argCount) {
        wchar_t pattern[256];
        NebulaText::get(pattern, 256, pId);
        expand(pDst, capacity, pattern, ppArgs, argCount);
    }

    // NebulaText's `pId` with {n} as `number`.
    void getNumbered(wchar_t* pDst, s32 capacity, const char* pId, s32 number) {
        char digits[16];
        snprintf(digits, sizeof(digits), "%d", static_cast< int >(number));
        wchar_t wide[16];
        fromUtf8(wide, 16, digits);
        const wchar_t* const args[] = {wide};
        getExpanded(pDst, capacity, pId, args, 1);
    }

    // A host text's entry in the played language: its NebulaText id is "S." and the text with each space as _ and each
    // number as #, and the entry names the numbers {0} to {3} in their order ("S.#_FPS" = "{0} FPS"). False when the
    // table has none.
    bool lookUp(wchar_t* pDst, s32 capacity, const char* pText) {
        char id[160] = "S.";
        s32 idLength = 2;
        wchar_t numbers[4][16];
        s32 numberCount = 0;
        for (const char* p = pText; *p != 0 && idLength + 2 < static_cast< s32 >(sizeof(id));) {
            if (*p < '0' || *p > '9') {
                id[idLength++] = *p == ' ' ? '_' : *p;
                p++;
                continue;
            }
            s32 length = 0;
            while ((*p >= '0' && *p <= '9') || (*p == '.' && p[1] >= '0' && p[1] <= '9')) {
                if (numberCount < 4 && length + 1 < 16) {
                    numbers[numberCount][length++] = static_cast< wchar_t >(*p);
                }
                p++;
            }
            if (numberCount < 4) {
                numbers[numberCount++][length] = 0;
            }
            id[idLength++] = '#';
        }
        id[idLength] = 0;
        if (!NebulaText::has(id)) {
            return false;
        }
        const wchar_t* const args[] = {numbers[0], numbers[1], numbers[2], numbers[3]};
        getExpanded(pDst, capacity, id, args, numberCount);
        return true;
    }

    // `pText`'s first `length` letters (no space at their end) and an ellipsis.
    void shorten(wchar_t* pDst, s32 capacity, const wchar_t* pText, s32 length) {
        s32 i = 0;
        for (; i < length && pText[i] != 0 && i + 2 < capacity; i++) {
            pDst[i] = pText[i];
        }
        while (i > 0 && pDst[i - 1] == L' ') {
            i--;
        }
        pDst[i++] = 0x2026;
        pDst[i] = 0;
    }

    // A sentence's first letter as a capital (the Latin letters the game's font has; other scripts have none).
    void capitalize(wchar_t* pText) {
        const wchar_t c = pText[0];
        if ((c >= L'a' && c <= L'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) {
            pText[0] = static_cast< wchar_t >(c - 0x20);
        }
    }

    // A host text in the played language: its entry, or else piece by piece, each piece the same way: the words before
    // a closing "(...)" and those inside it, then its sentences, the words before ": " and after it, a list's items
    // ("8X (OFF WITH DLSS / FSR 3)", "RIIVOLUTION, FILES. TEXT: ENGLISH"), joined as the language joins them. A name
    // only whole (the keyboard's "Keyboard and mouse"); words the table does not have as the host gives them.
    void translate(wchar_t* pDst, s32 capacity, const char* pText, bool name) {
        pDst[0] = 0;
        if (pText == nullptr || pText[0] == 0 || lookUp(pDst, capacity, pText)) {
            return;
        }
        static const char* const cSeparators[] = {" (", ". ", ": ", ", "};
        static const char* const cJoins[] = {"SettingsJoinAside", "SettingsJoinSentence", "SettingsJoinLabel", "SettingsJoinList"};
        const s32 textLength = static_cast< s32 >(strlen(pText));
        for (s32 i = 0; i < 4 && !name; i++) {
            const char* pAt = nullptr;
            s32 secondEnd = textLength;
            if (i == 0) {
                if (pText[textLength - 1] != ')') {
                    continue;
                }
                for (const char* p = strstr(pText, " ("); p != nullptr; p = strstr(p + 1, " (")) {
                    pAt = p;
                }
                secondEnd = textLength - 1;
            } else {
                pAt = strstr(pText, cSeparators[i]);
            }
            if (pAt == nullptr) {
                continue;
            }
            s32 firstLength = static_cast< s32 >(pAt - pText);
            while (firstLength > 0 && pText[firstLength - 1] == ' ') {
                firstLength--;
            }
            const s32 secondStart = static_cast< s32 >(pAt - pText) + 2;
            char first[192];
            char second[192];
            snprintf(first, sizeof(first), "%.*s", static_cast< int >(firstLength), pText);
            snprintf(second, sizeof(second), "%.*s", static_cast< int >(secondEnd - secondStart), pText + secondStart);
            wchar_t firstText[192];
            wchar_t secondText[192];
            translate(firstText, 192, first, false);
            translate(secondText, 192, second, false);
            if (i == 1) {
                capitalize(firstText);
                capitalize(secondText);
            }
            const wchar_t* const args[] = {firstText, secondText};
            getExpanded(pDst, capacity, cJoins[i], args, 2);
            return;
        }
        fromUtf8(pDst, capacity, pText);
    }

    // A host text as a whole line or value: translated, its first letter a capital unless it is a name ("on" inside
    // "(off with MSAA)" stays small).
    void translateLine(wchar_t* pDst, s32 capacity, const char* pText, bool name) {
        translate(pDst, capacity, pText, name);
        if (!name) {
            capitalize(pDst);
        }
    }

    // The key or button for `key` on `input` (NEBULA_SETTINGS_INPUT_*); `controls`: the bindings page's own keys (Enter
    // adds an input, Space applies). A controller's buttons change, close, undo and apply as the host reads them: an
    // Xbox controller's A, B, X, Y; a Switch controller's by their labels; a PlayStation controller's by place.
    void getKeyName(wchar_t* pDst, s32 capacity, s32 key, u32 input, bool controls) {
        if (key == KeyChange) {
            append(pDst, pDst + capacity, L"\u2190\u2192");
            return;
        }
        if (key == KeyAction && input == NEBULA_SETTINGS_INPUT_KEYBOARD) {
            append(pDst, pDst + capacity, L"\u2192");
            return;
        }
        if (input == NEBULA_SETTINGS_INPUT_KEYBOARD) {
            static const char* const cKeys[KeyNum] = {"KeyEnter", "KeyBackspace", "KeyEscape", "KeyTab", nullptr, "KeyEnter", nullptr};
            NebulaText::get(pDst, capacity, key == KeyApply && controls ? "KeySpace" : cKeys[key]);
            return;
        }
        static const wchar_t* const cPadNames[3][5] = {
            // change, close, undo, apply, the pages
            {L"A", L"B", L"X", L"Y", L"LB/RB"},
            {L"A", L"B", L"X", L"Y", L"L/R"},
            {L"\u00D7", L"\u25CB", L"\u25A1", L"\u25B3", L"L1/R1"},
        };
        static const s32 cPlace[KeyNum] = {3, 2, 1, 4, 0, 0, 0};
        append(pDst, pDst + capacity, cPadNames[input <= NEBULA_SETTINGS_INPUT_PLAYSTATION ? input - 1 : 0][cPlace[key]]);
    }

    // The page's row `row` (counted from its first), or null outside the part of the page the host sent.
    const NebulaSettingsRow* rowAt(const NebulaSettingsView& rView, u32 row) {
        return row >= rView.first_row && row - rView.first_row < rView.row_count ? &rView.rows[row - rView.first_row] : nullptr;
    }

    // A row whose value steps left and right; not one that does something ("PRESS RIGHT": opens a folder, imports).
    bool hasArrows(const NebulaSettingsRow* pRow) {
        return pRow != nullptr && (pRow->flags & NEBULA_SETTINGS_ROW_CHANGES) != 0 && strcmp(pRow->value, "PRESS RIGHT") != 0;
    }

    bool isInside(f32 x, f32 y, f32 cx, f32 cy, f32 w, f32 h) {
        return x >= cx - w * 0.5f && x <= cx + w * 0.5f && y >= cy - h * 0.5f && y <= cy + h * 0.5f;
    }
};  // namespace

// The menu's screen: the dimmed game behind the quit question's white window, the pages as the game's buttons, the
// rows with the selected one's highlight, the line under the page with the hints and the pointer's Apply and Back, and
// a question over it all when the host asks one. GameSystem draws it over the scene, which waits while it is shown.
class NebulaSettingsScreen : public LayoutActor {
public:
    NebulaSettingsScreen();

    virtual void init(const JMapInfoIter& rIter);

    void step(const NebulaSettingsView& rView);
    void drawMenu() const;
    bool isBuilt() const { return mBuilt; }
    bool isShown() const { return mOpen > 0.0f; }
    bool isOpenFully() const { return mOpen >= 1.0f && !mClosing; }

private:
    bool build();
    bool makeButton(Pane* pParent, Button* pButton, const char* pName);
    void arrange();
    void fillTexts(const NebulaSettingsView& rView, const NebulaSettingsView* pShown);
    void fillButton(Button& rButton, const wchar_t* pKey, const char* pWordId);
    void setQuestion(s32 question);
    void readPointer(const NebulaSettingsView& rView);
    void placeButton(Button& rButton, f32 x, f32 y, f32 width);
    void fitStatus(f32 room);
    void place(const NebulaSettingsView& rView);

    NebulaPaneKit mModalKit;   // SysInfoWindow: the quit question's dimmer, window and shadow
    NebulaPaneKit mTextKit;    // InformationWindow: the game's message font
    NebulaPaneKit mWindowKit;  // SysInfoWindowMini: the window with a ring, repainted for the highlight
    NebulaPaneKit mButtonKit;  // BackButton: the game's button
    Kits mKits;
    bool mBuilt;

    Pane* mRoot;
    Pane* mDim;
    Pane* mMenu;
    Window* mShadow;
    Window* mPanel;
    TextBox* mTitle;
    Chip mTabs[NEBULA_SETTINGS_PAGES];
    f32 mTabWordWidths[NEBULA_SETTINGS_PAGES];
    Pane* mList;
    Window* mHighlight;
    Row mRows[cRowsMax];
    Window* mTrack;
    Window* mThumb;
    TextBox* mStatus;
    wchar_t mStatusText[256];  // the line under the page, whole
    f32 mStatusWidth;          // as shown
    f32 mStatusRoom;           // the room it was fitted to
    Hint mHints[cHintsMax];
    Button mFooter[FooterNum];
    Pane* mQuestion;
    Pane* mQuestionDim;
    Window* mQuestionShadow;
    Window* mQuestionPanel;
    TextBox* mQuestionText;
    Button mChoices[ChoiceNum];
    s32 mChoiceCount;

    NebulaSettingsView mShown;  // the host's view as drawn
    f32 mOpen;                  // 0 away, 1 in place
    bool mClosing;
    s32 mQuestionShown;  // cQuestions' index; -1 none
    s32 mFrame;
    f32 mScroll;
    f32 mScrollTarget;
    f32 mHighlightY;
    bool mPointerMode;  // the pointer moved last: the row under it is the selected one
    f32 mPointerX;
    f32 mPointerY;
    s32 mPointerRow;  // the row the pointer asked for
    s32 mHoverRow;
    s32 mHoverTab;
    s32 mHoverArrow;  // the selected row's arrow under the pointer: -1 left, 1 right
    Button* mHoverButton;

    // The layout, again whenever the screen's shape changes.
    f32 mScaleX;
    f32 mWidth;
    f32 mHeight;
    f32 mPanelWidth;
    f32 mPanelHeight;
    f32 mLeft;
    f32 mRight;
    f32 mTitleY;
    f32 mTabY;
    f32 mListTop;
    f32 mListBottom;
    f32 mStatusY;
    f32 mHintsY;
    f32 mFooterY;
    f32 mValueRight;
};

namespace {
    NebulaSettingsScreen* sScreen = nullptr;
    NebulaSettingsView sView;
    bool sFreezing = false;     // the scene waits: the menu open, dropping in or rising away
    bool sMusicPaused = false;  // the music stopped once faded, as under the pause menu
};  // namespace

NebulaSettingsScreen::NebulaSettingsScreen()
    : LayoutActor("NebulaSettingsMenu", true), mBuilt(false), mRoot(nullptr), mDim(nullptr), mMenu(nullptr), mShadow(nullptr),
      mPanel(nullptr), mTitle(nullptr), mList(nullptr), mHighlight(nullptr), mTrack(nullptr), mThumb(nullptr), mStatus(nullptr),
      mStatusWidth(0.0f), mStatusRoom(-1.0f), mQuestion(nullptr), mQuestionDim(nullptr), mQuestionShadow(nullptr), mQuestionPanel(nullptr),
      mQuestionText(nullptr), mChoiceCount(0), mOpen(0.0f), mClosing(false), mQuestionShown(-1), mFrame(0), mScroll(0.0f),
      mScrollTarget(0.0f), mHighlightY(0.0f), mPointerMode(false), mPointerX(0.0f), mPointerY(0.0f), mPointerRow(-1), mHoverRow(-1),
      mHoverTab(-1), mHoverArrow(0), mHoverButton(nullptr), mScaleX(1.0f), mWidth(0.0f), mHeight(0.0f), mPanelWidth(0.0f),
      mPanelHeight(0.0f), mLeft(0.0f), mRight(0.0f), mTitleY(0.0f), mTabY(0.0f), mListTop(0.0f), mListBottom(0.0f), mStatusY(0.0f),
      mHintsY(0.0f), mFooterY(0.0f), mValueRight(0.0f) {
    memset(mTabs, 0, sizeof(mTabs));
    memset(mTabWordWidths, 0, sizeof(mTabWordWidths));
    memset(mRows, 0, sizeof(mRows));
    memset(mHints, 0, sizeof(mHints));
    memset(mFooter, 0, sizeof(mFooter));
    memset(mChoices, 0, sizeof(mChoices));
    memset(&mShown, 0, sizeof(mShown));
    mKits.mText = &mTextKit;
    mKits.mWindow = &mWindowKit;
    mKits.mButton = &mButtonKit;
}

void NebulaSettingsScreen::init(const JMapInfoIter& rIter) {
    initLayoutManager("SysInfoWindow", 0);
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaSettingsMenu";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));
    mBuilt = build();
    if (!mBuilt) {
        OSReport("NebulaSettingsMenu: a layout or pane is missing\n");
    }
    kill();
}

bool NebulaSettingsScreen::makeButton(Pane* pParent, Button* pButton, const char* pName) {
    if (!makeChip(mKits, pParent, &pButton->mChip, pName, false, cButtonSize, cButtonText)) {
        return false;
    }
    char name[17];
    snprintf(name, sizeof(name), "%sK", pName);
    pButton->mKey = makeText(mKits, pButton->mChip.mRoot, name, 16);
    setTextStyle(pButton->mKey, cButtonSize, cAccent, cAccent, 4);
    return pButton->mKey != nullptr;
}

bool NebulaSettingsScreen::build() {
    LayoutManager* pManager = getLayoutManager();
    Pane* pRoot = pManager->mLayout->mpRootPane;
    if (pRoot == nullptr || !mModalKit.init(pManager->mLayoutHolder) || !mTextKit.init(MR::createAndAddLayoutHolder("InformationWindow.arc")) ||
        !mWindowKit.init(MR::createAndAddLayoutHolder("SysInfoWindowMini.arc")) || !mButtonKit.init(MR::createAndAddLayoutHolder("BackButton.arc"))) {
        return false;
    }
    // The question window's own panes stay hidden; the menu is built beside them.
    for (nw4r::lyt::PaneList::Iterator it = pRoot->GetChildList().GetBeginIter(); it != pRoot->GetChildList().GetEndIter(); ++it) {
        it->SetVisible(false);
    }
    mRoot = makeGroup(pRoot, "NsRoot");
    mDim = mModalKit.make("BG", "NsDim");
    if (mRoot == nullptr || mDim == nullptr) {
        return false;
    }
    mDim->mBasePosition = 4;
    mRoot->AppendChild(mDim);
    mMenu = makeGroup(mRoot, "NsMenu");
    mShadow = mModalKit.makeWindow("ShaInfo", "NsShadow");
    mPanel = mModalKit.makeWindow("WinInfo", "NsPanel");
    if (mMenu == nullptr || mShadow == nullptr || mPanel == nullptr) {
        return false;
    }
    mShadow->mBasePosition = 4;
    mPanel->mBasePosition = 4;
    mMenu->AppendChild(mShadow);
    mMenu->AppendChild(mPanel);
    mTitle = makeText(mKits, mMenu, "NsTitle", 64);
    if (mTitle == nullptr) {
        return false;
    }
    setTextStyle(mTitle, cTitleSize, cText, cText, 3);
    for (s32 i = 0; i < NEBULA_SETTINGS_PAGES; i++) {
        char name[16];
        snprintf(name, sizeof(name), "NsTab%d", static_cast< int >(i));
        if (!makeChip(mKits, mMenu, &mTabs[i], name, false, cTabSize, cButtonText)) {
            return false;
        }
    }

    mList = makeGroup(mMenu, "NsList");
    mHighlight = mList != nullptr ? makeWindow(mKits, mList, "NsHighlight") : nullptr;
    if (mHighlight == nullptr) {
        return false;
    }
    paint(mHighlight, cHighlightFill, cHighlightRing);
    for (s32 i = 0; i < cRowsMax; i++) {
        char name[16];
        Row& rRow = mRows[i];
        snprintf(name, sizeof(name), "NsRow%d", static_cast< int >(i));
        rRow.mRoot = makeGroup(mList, name);
        if (rRow.mRoot == nullptr) {
            return false;
        }
        snprintf(name, sizeof(name), "NsRule%d", static_cast< int >(i));
        rRow.mRule = makeWindow(mKits, rRow.mRoot, name);
        snprintf(name, sizeof(name), "NsLabel%d", static_cast< int >(i));
        rRow.mLabel = makeText(mKits, rRow.mRoot, name, 96);
        snprintf(name, sizeof(name), "NsValue%d", static_cast< int >(i));
        rRow.mValue = makeText(mKits, rRow.mRoot, name, 128);
        snprintf(name, sizeof(name), "NsLeft%d", static_cast< int >(i));
        rRow.mLeft = makeText(mKits, rRow.mRoot, name, 4);
        snprintf(name, sizeof(name), "NsRight%d", static_cast< int >(i));
        rRow.mRight = makeText(mKits, rRow.mRoot, name, 4);
        if (rRow.mRule == nullptr || rRow.mLabel == nullptr || rRow.mValue == nullptr || rRow.mLeft == nullptr || rRow.mRight == nullptr) {
            return false;
        }
        paint(rRow.mRule, cRule, cRule);
        // The font's triangle, turned to point left and right.
        setTextStyle(rRow.mLeft, cArrowSize * 2.0f, cAccent, cAccent, 4);
        setTextStyle(rRow.mRight, cArrowSize * 2.0f, cAccent, cAccent, 4);
        setText(rRow.mLeft, L"\u25B2");
        setText(rRow.mRight, L"\u25B2");
        rRow.mLeft->mRotate.z = 90.0f;
        rRow.mRight->mRotate.z = -90.0f;
    }
    mTrack = makeWindow(mKits, mMenu, "NsTrack");
    mThumb = makeWindow(mKits, mMenu, "NsThumb");
    mStatus = makeText(mKits, mMenu, "NsStatus", 256);
    if (mTrack == nullptr || mThumb == nullptr || mStatus == nullptr) {
        return false;
    }
    paint(mTrack, cTrack, cTrack);
    paint(mThumb, cThumb, cThumb);
    setTextStyle(mStatus, cFooterSize, cSubText, cSubText, 3);
    for (s32 i = 0; i < cHintsMax; i++) {
        char name[16];
        snprintf(name, sizeof(name), "NsHintK%d", static_cast< int >(i));
        mHints[i].mKey = makeText(mKits, mMenu, name, 16);
        snprintf(name, sizeof(name), "NsHintW%d", static_cast< int >(i));
        mHints[i].mWord = makeText(mKits, mMenu, name, 48);
        if (mHints[i].mKey == nullptr || mHints[i].mWord == nullptr) {
            return false;
        }
        setTextStyle(mHints[i].mKey, cFooterSize, cAccent, cAccent, 3);
        setTextStyle(mHints[i].mWord, cFooterSize, cSubText, cSubText, 3);
    }
    if (!makeButton(mMenu, &mFooter[FooterApply], "NsApply") || !makeButton(mMenu, &mFooter[FooterBack], "NsBack")) {
        return false;
    }
    mFooter[FooterApply].mAction = NEBULA_SETTINGS_APPLY;
    mFooter[FooterBack].mAction = NEBULA_SETTINGS_CLOSE;

    // A question over the page: leave without applying? keep the new display?
    mQuestion = makeGroup(mMenu, "NsQuestion");
    mQuestionDim = mModalKit.make("BG", "NsQDim");
    mQuestionShadow = mModalKit.makeWindow("ShaInfo", "NsQShadow");
    mQuestionPanel = mModalKit.makeWindow("WinInfo", "NsQPanel");
    if (mQuestion == nullptr || mQuestionDim == nullptr || mQuestionShadow == nullptr || mQuestionPanel == nullptr) {
        return false;
    }
    mQuestionDim->mBasePosition = 4;
    mQuestionShadow->mBasePosition = 4;
    mQuestionPanel->mBasePosition = 4;
    mQuestion->AppendChild(mQuestionDim);
    mQuestion->AppendChild(mQuestionShadow);
    mQuestion->AppendChild(mQuestionPanel);
    mQuestionText = makeText(mKits, mQuestion, "NsQText", 256);
    if (mQuestionText == nullptr) {
        return false;
    }
    setTextStyle(mQuestionText, cTextSize + 1.0f, cText, cText, 4);
    for (s32 i = 0; i < ChoiceNum; i++) {
        char name[16];
        snprintf(name, sizeof(name), "NsChoice%d", static_cast< int >(i));
        if (!makeButton(mQuestion, &mChoices[i], name)) {
            return false;
        }
    }
    mQuestion->SetVisible(false);
    setInfluencedAlphaRecursive(mRoot, 0);
    return true;
}

// Where everything goes for the screen's shape: the window as large as the screen allows, at most cMaxWidth wide.
void NebulaSettingsScreen::arrange() {
    readScreen(&mScaleX, &mWidth, &mHeight);
    mRoot->mScale = nw4r::math::VEC2(mScaleX, 1.0f);
    mPanelWidth = mWidth - cMargin * 2.0f;
    mPanelWidth = mPanelWidth > cMaxWidth ? cMaxWidth : mPanelWidth;
    mPanelHeight = mHeight - cMargin * 2.0f;
    const f32 top = mPanelHeight * 0.5f - cPad;
    mLeft = -mPanelWidth * 0.5f + cPad;
    mRight = mPanelWidth * 0.5f - cPad;
    mTitleY = top - cTitleSize * 0.5f;
    mTabY = mTitleY - cTitleSize * 0.5f - 14.0f - cTabHeight * 0.5f;
    mListTop = mTabY - cTabHeight * 0.5f - 12.0f;
    mFooterY = -mPanelHeight * 0.5f + cPad * 0.8f + cButtonHeight * 0.5f;
    mHintsY = mFooterY - cFooterLine * 0.5f + 3.0f;
    mStatusY = mHintsY + cFooterLine;
    // The list holds whole rows: at rest none is cut at its edges.
    const f32 listRoom = mListTop - (mStatusY + cFooterSize * 0.5f + 12.0f);
    mListBottom = mListTop - cRowHeight * static_cast< s32 >(listRoom / cRowHeight);
    mValueRight = mRight - 16.0f - cArrowSize - cArrowGap;

    mDim->mSize = nw4r::lyt::Size(mWidth * 1.5f, mHeight * 1.5f);
    mPanel->mSize = nw4r::lyt::Size(mPanelWidth, mPanelHeight);
    mShadow->mSize = nw4r::lyt::Size(mPanelWidth, mPanelHeight);
    mShadow->mTranslate = nw4r::math::VEC3(6.0f, -6.0f, 0.0f);
    mQuestionDim->mSize = nw4r::lyt::Size(mPanelWidth, mPanelHeight);
    mQuestionPanel->mSize = nw4r::lyt::Size(cQuestionWidth, cQuestionHeight);
    mQuestionShadow->mSize = nw4r::lyt::Size(cQuestionWidth, cQuestionHeight);
    mQuestionShadow->mTranslate = nw4r::math::VEC3(6.0f, -6.0f, 0.0f);
}

void NebulaSettingsScreen::fillButton(Button& rButton, const wchar_t* pKey, const char* pWordId) {
    wchar_t text[64];
    f32 height = 0.0f;
    NebulaText::get(text, 64, pWordId);
    setText(rButton.mChip.mText, text);
    measure(rButton.mChip.mText, 4000.0f, &rButton.mWordWidth, &height);
    setText(rButton.mKey, pKey);
    measure(rButton.mKey, 4000.0f, &rButton.mKeyWidth, &height);
}

// The texts in the played language, again for whatever changed from `pShown` (all of them when it is null): the page
// names, the rows, the line under the page, the hints and the question.
void NebulaSettingsScreen::fillTexts(const NebulaSettingsView& rView, const NebulaSettingsView* pShown) {
    wchar_t text[256];
    f32 width = 0.0f;
    f32 height = 0.0f;
    if (pShown == nullptr) {
        NebulaText::get(text, 256, "SettingsTitle");
        setText(mTitle, text);
        measure(mTitle, 4000.0f, &width, &height);
        for (u32 i = 0; i < NEBULA_SETTINGS_PAGES; i++) {
            Chip& rTab = mTabs[i];
            rTab.mRoot->SetVisible(i < rView.page_count);
            if (i >= rView.page_count) {
                continue;
            }
            translateLine(text, 64, rView.pages[i], false);
            setText(rTab.mText, text);
            measure(rTab.mText, 4000.0f, &mTabWordWidths[i], &height);
        }
    }
    const bool samePage = pShown != nullptr && pShown->page == rView.page && pShown->first_row == rView.first_row &&
                          pShown->input == rView.input;
    for (s32 i = 0; i < cRowsMax; i++) {
        Row& rRow = mRows[i];
        const bool shown = i < static_cast< s32 >(rView.row_count);
        rRow.mRoot->SetVisible(shown);
        if (!shown || (samePage && i < static_cast< s32 >(pShown->row_count) &&
                       memcmp(&rView.rows[i], &pShown->rows[i], sizeof(NebulaSettingsRow)) == 0)) {
            continue;
        }
        const NebulaSettingsRow& rData = rView.rows[i];
        const bool heading = (rData.flags & NEBULA_SETTINGS_ROW_HEADING) != 0;
        // A mod's own option ("  NAME") keeps its indent.
        rRow.mIndented = rData.label[0] == ' ' && rData.label[1] == ' ';
        translateLine(text, 128, rRow.mIndented ? rData.label + 2 : rData.label, (rData.flags & NEBULA_SETTINGS_ROW_LABEL_NAME) != 0);
        setText(rRow.mLabel, text);
        setTextStyle(rRow.mLabel, heading ? cHeadingSize : cTextSize, heading ? cAccent : cText, heading ? cAccent : cText, 3);
        measure(rRow.mLabel, 4000.0f, &rRow.mLabelWidth, &height);
        rRow.mRule->SetVisible(heading);
        rRow.mValue->SetVisible(!heading);
        rRow.mValueWidth = 0.0f;
        if (heading) {
            rRow.mLeft->SetVisible(false);
            rRow.mRight->SetVisible(false);
            continue;
        }
        u32 valueColor = (rData.flags & NEBULA_SETTINGS_ROW_CHANGES) != 0 ? cText : cDisabled;
        if (strcmp(rData.value, "PRESS RIGHT") == 0) {
            getKeyName(text, 192, KeyAction, rView.input, false);
            valueColor = cAccent;
        } else {
            translateLine(text, 192, rData.value, (rData.flags & NEBULA_SETTINGS_ROW_NAME) != 0);
        }
        if ((rData.flags & NEBULA_SETTINGS_ROW_ACTIVE) != 0) {
            wchar_t inUse[48] = L"  ";
            NebulaText::get(inUse + 2, 46, "SettingsInUse");
            append(text + wcslen(text), text + 191, inUse);
        }
        setText(rRow.mValue, text);
        setTextStyle(rRow.mValue, cTextSize, valueColor, valueColor, 5);
        measure(rRow.mValue, 4000.0f, &rRow.mValueWidth, &height);
    }

    if (pShown != nullptr && pShown->status == rView.status && pShown->status_number == rView.status_number &&
        strcmp(pShown->help, rView.help) == 0 && strcmp(pShown->status_text, rView.status_text) == 0 &&
        pShown->page == rView.page && pShown->input == rView.input) {
        return;
    }

    // The line under the page: what is happening, or the selected row's description.
    const char* pStatusId = nullptr;
    const char* pStatusText = rView.help;
    u32 statusColor = cAccent;
    switch (rView.status) {
    case NEBULA_SETTINGS_STATUS_CAPTURE_KEY:
        pStatusId = "SettingsCaptureKey";
        break;
    case NEBULA_SETTINGS_STATUS_CAPTURE_PAD:
        pStatusId = "SettingsCapturePad";
        break;
    case NEBULA_SETTINGS_STATUS_MESSAGE:
        pStatusText = rView.status_text;
        statusColor = cWarning;
        break;
    case NEBULA_SETTINGS_STATUS_DLSS_INSTALLING:
        pStatusId = "SettingsDlssInstalling";
        break;
    case NEBULA_SETTINGS_STATUS_DLSS_FAILED:
        pStatusId = "SettingsDlssFailed";
        statusColor = cWarning;
        break;
    case NEBULA_SETTINGS_STATUS_SAVE_FAILED:
        pStatusId = "SettingsSaveFailed";
        statusColor = cWarning;
        break;
    case NEBULA_SETTINGS_STATUS_PENDING:
        pStatusId = rView.status_number == 1 ? "SettingsPendingOne" : "SettingsPending";
        break;
    case NEBULA_SETTINGS_STATUS_APPLIED:
        pStatusId = "SettingsApplied";
        break;
    case NEBULA_SETTINGS_STATUS_LANGUAGE_RESTART:
        pStatusId = "SettingsLanguageRestart";
        break;
    case NEBULA_SETTINGS_STATUS_MODS_RESTART:
        pStatusId = "SettingsModsRestart";
        break;
    case NEBULA_SETTINGS_STATUS_GPU_RESTART:
        pStatusId = "SettingsGpuRestart";
        break;
    default:
        statusColor = cSubText;
        break;
    }
    if (pStatusId != nullptr) {
        getNumbered(mStatusText, 256, pStatusId, rView.status_number);
    } else {
        translateLine(mStatusText, 256, pStatusText, false);
    }
    setTextColor(mStatus, statusColor, statusColor);
    mStatusRoom = -1.0f;  // fitted again where it is placed

    // The hints: changing, the page's own key, the pages.
    const bool controls = rView.page == rView.controls_page;
    static const s32 cPageKeys[cHintsMax] = {KeyChange, KeyUndo, KeyPage};
    static const char* const cPageWords[cHintsMax] = {"SettingsHintChange", "SettingsHintUndo", "SettingsHintPage"};
    static const s32 cControlsKeys[cHintsMax] = {KeyBind, KeyUndo, KeyPage};
    static const char* const cControlsWords[cHintsMax] = {"SettingsHintBind", "SettingsHintClear", "SettingsHintPage"};
    for (s32 i = 0; i < cHintsMax; i++) {
        Hint& rHint = mHints[i];
        getKeyName(text, 64, controls ? cControlsKeys[i] : cPageKeys[i], rView.input, controls);
        setText(rHint.mKey, text);
        measure(rHint.mKey, 4000.0f, &rHint.mKeyWidth, &height);
        NebulaText::get(text, 64, controls ? cControlsWords[i] : cPageWords[i]);
        setText(rHint.mWord, text);
        measure(rHint.mWord, 4000.0f, &rHint.mWordWidth, &height);
    }
    getKeyName(text, 64, KeyApply, rView.input, controls);
    fillButton(mFooter[FooterApply], text, "SettingsApply");
    getKeyName(text, 64, KeyBack, rView.input, controls);
    fillButton(mFooter[FooterBack], text, "SettingsBack");

    // A question over the page.
    s32 question = -1;
    for (s32 i = 0; i < cQuestionNum; i++) {
        if (rView.status == cQuestions[i].mStatus) {
            question = i;
        }
    }
    if (question != mQuestionShown) {
        mQuestionShown = question;
        setQuestion(question);
        if (question >= 0) {
            MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");
        }
    }
    if (question < 0) {
        return;
    }
    const Question& rQuestion = cQuestions[question];
    getNumbered(text, 256, rView.status_number == 1 ? rQuestion.mTextOne : rQuestion.mText, rView.status_number);
    setText(mQuestionText, text);
    measure(mQuestionText, cQuestionWidth - 48.0f, &width, &height);
    // The answers' keys follow what the player last used.
    for (s32 i = 0; i < mChoiceCount; i++) {
        Button& rChoice = mChoices[i];
        getKeyName(text, 64, rQuestion.mKeys[i], rView.input, false);
        setText(rChoice.mKey, text);
        measure(rChoice.mKey, 4000.0f, &rChoice.mKeyWidth, &height);
    }
}

// The question's answers as the game's buttons, for the pointer; the keys and controllers answer through the host.
void NebulaSettingsScreen::setQuestion(s32 question) {
    mChoiceCount = question >= 0 ? cQuestions[question].mChoiceCount : 0;
    for (s32 i = 0; i < ChoiceNum; i++) {
        Button& rChoice = mChoices[i];
        rChoice.mChip.mRoot->SetVisible(i < mChoiceCount);
        rChoice.mChip.mFocusLevel = 0.0f;
        if (i < mChoiceCount) {
            fillButton(rChoice, L"", cQuestions[question].mWords[i]);
            rChoice.mAction = cQuestions[question].mActions[i];
        }
    }
    mQuestion->SetVisible(question >= 0);
}

// The pointer, in the menu's units: over a page's button, a row, the selected row's arrows, the footer's buttons or the
// question's. Moving it picks the row under it; A chooses what it is over.
void NebulaSettingsScreen::readPointer(const NebulaSettingsView& rView) {
    const s32 lastTab = mHoverTab;
    Button* pLastButton = mHoverButton;
    mHoverRow = -1;
    mHoverTab = -1;
    mHoverArrow = 0;
    mHoverButton = nullptr;
    // A binding that waits for an input takes the mouse's buttons itself, wherever the pointer is.
    if (!MR::isStarPointerInScreen(WPAD_CHAN0) || rView.status == NEBULA_SETTINGS_STATUS_CAPTURE_KEY ||
        rView.status == NEBULA_SETTINGS_STATUS_CAPTURE_PAD) {
        return;
    }
    TVec2f screen = MR::getStarPointerScreenPositionOrEdge(WPAD_CHAN0);
    TVec2f layout;
    MR::convertScreenPosToLayoutPos(&layout, screen);
    const f32 x = layout.x / mScaleX;
    const f32 y = layout.y - mMenu->mTranslate.y;
    if (__builtin_fabsf(x - mPointerX) > 0.5f || __builtin_fabsf(y - mPointerY) > 0.5f) {
        mPointerMode = true;
    }
    mPointerX = x;
    mPointerY = y;

    if (mQuestionShown >= 0) {
        for (s32 i = 0; i < mChoiceCount; i++) {
            const Chip& rChip = mChoices[i].mChip;
            if (isInside(x, y, rChip.mX, rChip.mRoot->mTranslate.y, rChip.mWidth, cButtonHeight + 4.0f)) {
                mHoverButton = &mChoices[i];
            }
        }
    } else {
        for (u32 i = 0; i < rView.page_count; i++) {
            if (i != rView.page && isInside(x, y, mTabs[i].mX, mTabY, mTabs[i].mWidth, cTabHeight + 4.0f)) {
                mHoverTab = static_cast< s32 >(i);
            }
        }
        for (s32 i = 0; i < FooterNum; i++) {
            if (isInside(x, y, mFooter[i].mChip.mX, mFooterY, mFooter[i].mChip.mWidth, cButtonHeight + 4.0f)) {
                mHoverButton = &mFooter[i];
            }
        }
        if (x >= mLeft && x <= mRight && y <= mListTop && y >= mListBottom) {
            const s32 row = static_cast< s32 >((mListTop - y + mScroll) / cRowHeight);
            const NebulaSettingsRow* pData = row >= 0 ? rowAt(rView, static_cast< u32 >(row)) : nullptr;
            if (pData != nullptr && (pData->flags & NEBULA_SETTINGS_ROW_HEADING) == 0) {
                mHoverRow = row;
                const Row& rRow = mRows[row - static_cast< s32 >(rView.first_row)];
                if (row == static_cast< s32 >(rView.selected) && hasArrows(pData)) {
                    const f32 leftArrow = mValueRight - rRow.mValueWidth * rRow.mValue->mScale.x - cArrowGap - cArrowSize * 0.5f;
                    if (x >= mValueRight + cArrowGap * 0.5f) {
                        mHoverArrow = 1;
                    } else if (__builtin_fabsf(x - leftArrow) <= cArrowSize + 4.0f) {
                        mHoverArrow = -1;
                    }
                }
            }
        }
    }
    if ((mHoverButton != nullptr && mHoverButton != pLastButton) || (mHoverTab >= 0 && mHoverTab != lastTab)) {
        MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
    }
    if (mPointerMode && mHoverRow >= 0 && mHoverRow != static_cast< s32 >(rView.selected)) {
        mPointerRow = mHoverRow;
        __NebulaSettingsInput(NEBULA_SETTINGS_SELECT_ROW, mHoverRow);
    }
    if (!MR::testCorePadTriggerA(WPAD_CHAN0)) {
        return;
    }
    if (mHoverButton != nullptr) {
        __NebulaSettingsInput(mHoverButton->mAction, 0);
    } else if (mHoverTab >= 0) {
        __NebulaSettingsInput(NEBULA_SETTINGS_SELECT_PAGE, mHoverTab);
    } else if (mHoverRow >= 0 && (rowAt(rView, static_cast< u32 >(mHoverRow))->flags & NEBULA_SETTINGS_ROW_CHANGES) != 0) {
        // A click changes the value: back by its left arrow, on anywhere else on the row.
        mPointerMode = true;
        mPointerRow = mHoverRow;
        __NebulaSettingsInput(NEBULA_SETTINGS_SELECT_ROW, mHoverRow);
        __NebulaSettingsInput(NEBULA_SETTINGS_CHANGE, mHoverArrow < 0 ? -1 : 1);
    }
}

// The line under the page in the room beside Apply and Back: narrowed down to cMinSqueeze, then cut short with an
// ellipsis (a mod's long list of conflicts).
void NebulaSettingsScreen::fitStatus(f32 room) {
    mStatusRoom = room;
    f32 height = 0.0f;
    setText(mStatus, mStatusText);
    measure(mStatus, 4000.0f, &mStatusWidth, &height);
    if (mStatusWidth * cMinSqueeze <= room) {
        return;
    }
    // The longest start that fits with its ellipsis.
    wchar_t cut[256];
    s32 fits = 0;
    s32 longest = static_cast< s32 >(wcslen(mStatusText)) - 1;
    while (fits < longest) {
        const s32 length = (fits + longest + 1) / 2;
        shorten(cut, 256, mStatusText, length);
        setText(mStatus, cut);
        measure(mStatus, 4000.0f, &mStatusWidth, &height);
        if (mStatusWidth * cMinSqueeze <= room) {
            fits = length;
        } else {
            longest = length - 1;
        }
    }
    shorten(cut, 256, mStatusText, fits);
    setText(mStatus, cut);
    measure(mStatus, 4000.0f, &mStatusWidth, &height);
}

// A button's art for `width`, its key and word centred together on it; it grows and shines under the pointer.
void NebulaSettingsScreen::placeButton(Button& rButton, f32 x, f32 y, f32 width) {
    Chip& rChip = rButton.mChip;
    rChip.mFocusLevel += ((mHoverButton == &rButton ? 1.0f : 0.0f) - rChip.mFocusLevel) * 0.35f;
    rChip.mX = x;
    rChip.mWidth = width;
    rChip.mHeight = cButtonHeight;
    shapeChip(rChip, width, cButtonHeight);
    const f32 grow = 1.0f + 0.05f * rChip.mFocusLevel;
    rChip.mRoot->mTranslate = nw4r::math::VEC3(x, y, 0.0f);
    rChip.mRoot->mScale = nw4r::math::VEC2(grow, grow);
    rChip.mLayers[LayerShine]->SetVisible(rChip.mFocusLevel > 0.02f);
    rChip.mLayers[LayerShine]->mAlpha = toAlpha(rChip.mFocusLevel * 0.7f);
    // Narrowed together when a language runs longer than the button's middle (its rounded ends stay clear).
    const f32 gap = rButton.mKeyWidth > 0.0f ? cKeyGap : 0.0f;
    const f32 natural = rButton.mKeyWidth + gap + rButton.mWordWidth;
    const f32 room = width - cButtonHeight;
    const f32 squeeze = natural > room && natural > 0.0f ? clamp(room / natural, 0.6f, 1.0f) : 1.0f;
    const f32 total = natural * squeeze;
    rButton.mKey->mScale = nw4r::math::VEC2(squeeze, 1.0f);
    rChip.mText->mScale = nw4r::math::VEC2(squeeze, 1.0f);
    rButton.mKey->mTranslate =
        nw4r::math::VEC3(-total * 0.5f + rButton.mKeyWidth * squeeze * 0.5f, textCentre(0.0f, cButtonSize), 0.0f);
    rChip.mText->mTranslate =
        nw4r::math::VEC3(total * 0.5f - rButton.mWordWidth * squeeze * 0.5f, textCentre(0.0f, cButtonSize), 0.0f);
}

// Where each piece is this frame: the drop, the pages' buttons, the rows scrolled to the selection, the highlight,
// the footer, the question.
void NebulaSettingsScreen::place(const NebulaSettingsView& rView) {
    mDim->mAlpha = toAlpha(clamp(mOpen * 1.5f, 0.0f, 1.0f));
    mMenu->mTranslate = nw4r::math::VEC3(0.0f, (1.0f - easeOut(mOpen)) * (mHeight * 0.5f + mPanelHeight * 0.5f + 16.0f), 0.0f);
    mTitle->mTranslate = nw4r::math::VEC3(mLeft + 2.0f, textCentre(mTitleY, cTitleSize), 0.0f);

    // The pages' buttons share the window's width; a word too long for its button is narrowed to fit.
    const s32 pages = static_cast< s32 >(rView.page_count);
    const f32 tabWidth = pages > 0 ? (mRight - mLeft - cTabGap * (pages - 1)) / pages : 0.0f;
    for (s32 i = 0; i < pages; i++) {
        Chip& rTab = mTabs[i];
        const bool active = i == static_cast< s32 >(rView.page);
        rTab.mFocusLevel += ((active ? 1.0f : i == mHoverTab ? 0.6f : 0.0f) - rTab.mFocusLevel) * 0.35f;
        rTab.mX = mLeft + tabWidth * 0.5f + i * (tabWidth + cTabGap);
        rTab.mWidth = tabWidth;
        rTab.mHeight = cTabHeight;
        shapeChip(rTab, tabWidth, cTabHeight);
        const f32 grow = 1.0f + 0.05f * rTab.mFocusLevel;
        rTab.mRoot->mTranslate = nw4r::math::VEC3(rTab.mX, mTabY, 0.0f);
        rTab.mRoot->mScale = nw4r::math::VEC2(grow, grow);
        rTab.mLayers[LayerShine]->SetVisible(rTab.mFocusLevel > 0.02f);
        rTab.mLayers[LayerShine]->mAlpha = toAlpha(rTab.mFocusLevel * 0.7f);
        const f32 room = tabWidth - 22.0f;
        const f32 word = mTabWordWidths[i];
        rTab.mText->mScale = nw4r::math::VEC2(word > room && word > 0.0f ? room / word : 1.0f, 1.0f);
        rTab.mText->mTranslate = nw4r::math::VEC3(0.0f, textCentre(0.0f, cTabSize), 0.0f);
        setTextColor(rTab.mText, active ? cAccent : cButtonText, active ? cAccent : cButtonText);
    }

    // The rows, scrolled so that the selection keeps a row of room above and below it; a row the pointer picked is
    // under it already, and the list holds still under the pointer.
    const f32 listHeight = mListTop - mListBottom;
    const f32 content = rView.total_rows * cRowHeight;
    const f32 maxScroll = content > listHeight ? content - listHeight : 0.0f;
    const f32 selectedTop = rView.selected * cRowHeight;
    const f32 room = mPointerMode ? 0.0f : cRowHeight;
    if (selectedTop - room < mScrollTarget) {
        mScrollTarget = selectedTop - room;
    }
    if (selectedTop + cRowHeight + room > mScrollTarget + listHeight) {
        mScrollTarget = selectedTop + cRowHeight + room - listHeight;
    }
    mScrollTarget = clamp(mScrollTarget, 0.0f, maxScroll);
    mScroll = mFrame <= 1 ? mScrollTarget : mScroll + (mScrollTarget - mScroll) * 0.35f;
    const bool asking = mQuestionShown >= 0;
    for (s32 i = 0; i < static_cast< s32 >(rView.row_count) && i < cRowsMax; i++) {
        Row& rRow = mRows[i];
        const NebulaSettingsRow& rData = rView.rows[i];
        const bool heading = (rData.flags & NEBULA_SETTINGS_ROW_HEADING) != 0;
        const s32 row = static_cast< s32 >(rView.first_row) + i;
        const f32 y = mListTop - row * cRowHeight - cRowHeight * 0.5f + mScroll;
        rRow.mRoot->mTranslate = nw4r::math::VEC3(0.0f, y, 0.0f);
        rRow.mRoot->SetVisible(y < mListTop + cRowHeight && y > mListBottom - cRowHeight);
        const f32 labelX = mLeft + (rRow.mIndented ? 26.0f : heading ? 2.0f : 12.0f);
        rRow.mLabel->mTranslate = nw4r::math::VEC3(labelX, textCentre(heading ? -2.0f : 0.0f, heading ? cHeadingSize : cTextSize), 0.0f);
        if (heading) {
            const f32 start = labelX + rRow.mLabelWidth + 10.0f;
            shape(rRow.mRule, (start + mRight) * 0.5f, -2.0f, mRight > start ? mRight - start : 0.0f, 2.0f, 1.0f);
            continue;
        }
        // A value too long for the room its label leaves is narrowed to fit.
        const f32 room = mValueRight - labelX - rRow.mLabelWidth - 24.0f;
        const f32 squeeze = rRow.mValueWidth > room && rRow.mValueWidth > 0.0f ? clamp(room / rRow.mValueWidth, 0.55f, 1.0f) : 1.0f;
        rRow.mValue->mScale = nw4r::math::VEC2(squeeze, 1.0f);
        rRow.mValue->mTranslate = nw4r::math::VEC3(mValueRight, textCentre(0.0f, cTextSize), 0.0f);
        const bool arrows = row == static_cast< s32 >(rView.selected) && !asking && hasArrows(&rData);
        rRow.mLeft->SetVisible(arrows);
        rRow.mRight->SetVisible(arrows);
        if (arrows) {
            // They lean out and back, and stand out under the pointer.
            const f32 lean = 1.5f * (0.5f + 0.5f * __builtin_sinf(mFrame * 0.15f));
            const f32 leftX = mValueRight - rRow.mValueWidth * squeeze - cArrowGap - cArrowSize * 0.5f;
            const f32 rightX = mValueRight + cArrowGap + cArrowSize * 0.5f;
            rRow.mLeft->mTranslate = nw4r::math::VEC3(leftX - (mHoverArrow < 0 ? 2.0f : lean), glyphCentre(0.0f, cArrowSize), 0.0f);
            rRow.mRight->mTranslate = nw4r::math::VEC3(rightX + (mHoverArrow > 0 ? 2.0f : lean), glyphCentre(0.0f, cArrowSize), 0.0f);
        }
    }

    // The selection's highlight glides to its row.
    const f32 selectedY = mListTop - rView.selected * cRowHeight - cRowHeight * 0.5f + mScroll;
    mHighlightY = mFrame <= 1 ? selectedY : mHighlightY + (selectedY - mHighlightY) * 0.45f;
    shape(mHighlight, (mLeft + mRight) * 0.5f, mHighlightY, mRight - mLeft, cRowHeight - 2.0f, 9.0f);
    mHighlight->SetVisible(rView.row_count > 0 && !asking);

    // The scroll bar, when the page is longer than the list.
    const bool scrolls = maxScroll > 0.5f;
    mTrack->SetVisible(scrolls);
    mThumb->SetVisible(scrolls);
    if (scrolls) {
        const f32 trackX = mRight + 12.0f;
        shape(mTrack, trackX, (mListTop + mListBottom) * 0.5f, 4.0f, listHeight, 2.0f);
        const f32 thumbHeight = listHeight * listHeight / content;
        const f32 thumbY = mListTop - thumbHeight * 0.5f - (listHeight - thumbHeight) * (mScroll / maxScroll);
        shape(mThumb, trackX, thumbY, 4.0f, thumbHeight, 2.0f);
    }

    // The footer: Apply and Back for the pointer on the right; the line under the page and the hints in the room left,
    // narrowed when a language runs long.
    f32 buttonWidths[FooterNum];
    f32 buttonsWidth = 0.0f;
    for (s32 i = 0; i < FooterNum; i++) {
        const Button& rButton = mFooter[i];
        buttonWidths[i] = rButton.mKeyWidth + cKeyGap + rButton.mWordWidth + 44.0f;
        buttonWidths[i] = buttonWidths[i] < 110.0f ? 110.0f : buttonWidths[i];
        buttonsWidth += buttonWidths[i] + (i > 0 ? cButtonGap : 0.0f);
    }
    f32 x = mRight;
    for (s32 i = FooterNum - 1; i >= 0; i--) {
        placeButton(mFooter[i], x - buttonWidths[i] * 0.5f, mFooterY, buttonWidths[i]);
        x -= buttonWidths[i] + cButtonGap;
    }
    const f32 textRoom = mRight - mLeft - buttonsWidth - 20.0f;
    if (mStatusRoom != textRoom) {
        fitStatus(textRoom);
    }
    const f32 statusScale = mStatusWidth > textRoom && mStatusWidth > 0.0f ? clamp(textRoom / mStatusWidth, cMinSqueeze, 1.0f) : 1.0f;
    mStatus->mScale = nw4r::math::VEC2(statusScale, 1.0f);
    mStatus->mTranslate = nw4r::math::VEC3(mLeft + 2.0f, textCentre(mStatusY, cFooterSize), 0.0f);
    f32 hintsWidth = 0.0f;
    for (s32 i = 0; i < cHintsMax; i++) {
        hintsWidth += mHints[i].mKeyWidth + cKeyGap + mHints[i].mWordWidth + (i > 0 ? cHintGap : 0.0f);
    }
    const f32 hintScale = hintsWidth > textRoom && hintsWidth > 0.0f ? clamp(textRoom / hintsWidth, 0.6f, 1.0f) : 1.0f;
    x = mLeft + 2.0f;
    for (s32 i = 0; i < cHintsMax; i++) {
        Hint& rHint = mHints[i];
        rHint.mKey->mScale = nw4r::math::VEC2(hintScale, 1.0f);
        rHint.mWord->mScale = nw4r::math::VEC2(hintScale, 1.0f);
        rHint.mKey->mTranslate = nw4r::math::VEC3(x, textCentre(mHintsY, cFooterSize), 0.0f);
        x += (rHint.mKeyWidth + cKeyGap) * hintScale;
        rHint.mWord->mTranslate = nw4r::math::VEC3(x, textCentre(mHintsY, cFooterSize), 0.0f);
        x += (rHint.mWordWidth + cHintGap) * hintScale;
    }

    // The question, centred on the window, its answers along its foot.
    if (asking) {
        mQuestionDim->mAlpha = 96;
        const f32 answersY = -cQuestionHeight * 0.5f + 22.0f + cButtonHeight * 0.5f;
        const f32 textY = (cQuestionHeight * 0.5f + answersY + cButtonHeight * 0.5f) * 0.5f;
        mQuestionText->mTranslate = nw4r::math::VEC3(0.0f, textCentre(textY, cTextSize + 1.0f), 0.0f);
        const f32 width = (cQuestionWidth - 44.0f - cButtonGap * (mChoiceCount - 1)) / (mChoiceCount > 0 ? mChoiceCount : 1);
        for (s32 i = 0; i < mChoiceCount; i++) {
            placeButton(mChoices[i], -cQuestionWidth * 0.5f + 22.0f + width * 0.5f + i * (width + cButtonGap), answersY, width);
        }
    }
}

// A frame: open or close as the host's menu does (dropping in, rising away), the pointer, the sounds of what changed.
void NebulaSettingsScreen::step(const NebulaSettingsView& rView) {
    if (!mBuilt) {
        return;
    }
    if (rView.open && (!isShown() || mClosing)) {
        if (!isShown()) {
            appear();
            mFrame = 0;
            mScroll = 0.0f;
            mScrollTarget = 0.0f;
            mPointerMode = false;
            mPointerRow = -1;
            mHoverButton = nullptr;
            mHoverTab = -1;
            mQuestionShown = -1;
            mQuestion->SetVisible(false);
            for (s32 i = 0; i < NEBULA_SETTINGS_PAGES; i++) {
                mTabs[i].mFocusLevel = 0.0f;
            }
            mFooter[FooterApply].mChip.mFocusLevel = 0.0f;
            mFooter[FooterBack].mChip.mFocusLevel = 0.0f;
            arrange();
            memcpy(&mShown, &rView, sizeof(rView));
            fillTexts(mShown, nullptr);
        }
        mClosing = false;
        MR::startSystemSE("SE_SY_PAUSE_ON");
    } else if (!rView.open && isShown() && !mClosing) {
        mClosing = true;
        MR::startSystemSE("SE_SY_PAUSE_OFF");
    }
    if (mClosing) {
        mOpen = clamp(mOpen - 1.0f / cDropFrames, 0.0f, 1.0f);
        if (mOpen <= 0.0f) {
            mClosing = false;
            kill();
            return;
        }
    } else if (rView.open) {
        mOpen = clamp(mOpen + 1.0f / cDropFrames, 0.0f, 1.0f);
    }
    if (mOpen <= 0.0f) {
        return;
    }
    mFrame++;
    f32 scaleX;
    f32 width;
    f32 height;
    readScreen(&scaleX, &width, &height);
    if (scaleX != mScaleX || __builtin_fabsf(width - mWidth) > 0.5f || __builtin_fabsf(height - mHeight) > 0.5f) {
        arrange();
    }
    if (rView.open && memcmp(&rView, &mShown, sizeof(rView)) != 0) {
        // What changed, as the game's menus sound it.
        if (rView.page != mShown.page) {
            MR::startSystemSE("SE_SY_GALAMAP_PAGE_CHANGE");
            mScrollTarget = 0.0f;
            mFrame = 1;  // the new page's rows and highlight start in place
        } else if (rView.selected != mShown.selected) {
            MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");
            if (static_cast< s32 >(rView.selected) != mPointerRow) {
                mPointerMode = false;  // the keys or a controller moved it: the pointer waits until it moves
            }
        } else {
            const NebulaSettingsRow* pNow = rowAt(rView, rView.selected);
            const NebulaSettingsRow* pBefore = rowAt(mShown, rView.selected);
            if (pNow != nullptr && pBefore != nullptr && strcmp(pNow->value, pBefore->value) != 0) {
                MR::startSystemSE("SE_SY_SELECT_PAUSE_ITEM");
            }
        }
        if (rView.status == NEBULA_SETTINGS_STATUS_APPLIED && mShown.status != NEBULA_SETTINGS_STATUS_APPLIED) {
            MR::startSystemSE("SE_SY_TALK_OK");
        }
        fillTexts(rView, &mShown);
        memcpy(&mShown, &rView, sizeof(rView));
    }
    if (rView.open && !mClosing) {
        readPointer(mShown);
    } else {
        mHoverButton = nullptr;
        mHoverTab = -1;
    }
    place(mShown);
}

// The menu, then its list cut to the list's box (what scrolls out never covers the pages' buttons or the footer), then
// the question over both.
void NebulaSettingsScreen::drawMenu() const {
    if (!mBuilt || mOpen <= 0.0f || getLayoutManager()->mIsScreenHidden) {
        return;
    }
    const bool asking = mQuestion->IsVisible();
    mList->SetVisible(false);
    mQuestion->SetVisible(false);
    draw();
    mList->SetVisible(true);
    drawClipped(mList, getLayoutManager()->mDrawInfo, mMenu->mTranslate.y, mLeft - 4.0f, mRight + 18.0f, mListTop + 2.0f,
                mListBottom - 2.0f);
    if (asking) {
        mQuestion->SetVisible(true);
        mQuestion->Draw(getLayoutManager()->mDrawInfo);
    }
}

namespace NebulaSettingsMenu {
    void init() {
        sScreen = new NebulaSettingsScreen();
        sScreen->initWithoutIter();
    }

    void update() {
        // Unbuilt (a layout missing), the host's own overlay draws the menu and the game runs on.
        if (sScreen == nullptr || !sScreen->isBuilt()) {
            return;
        }
        __NebulaSettingsView(&sView);
        sScreen->step(sView);
        // The whole game waits from the first frame of the drop to the last of the rise. Its sound fades and the music
        // stops as under the pause menu, and both come back as they were.
        const bool freezing = sView.open || sScreen->isShown();
        AudSystem* pAudio = AudWrap::getSystem();
        if (freezing != sFreezing) {
            sFreezing = freezing;
            if (pAudio != nullptr) {
                pAudio->holdForSettingsMenu(freezing, NebulaCutsceneSkip::isFreezingScene() || NebulaCutsceneSkip::isSkipping());
                if (!freezing && sMusicPaused) {
                    pAudio->unpause();
                }
            }
            sMusicPaused = false;
        }
        if (freezing && !sMusicPaused && sScreen->isOpenFully() && pAudio != nullptr && !pAudio->mIsPaused &&
            !pAudio->isPauseMenuActive() && !pAudio->isHomeButtonMenuActive()) {
            pAudio->pause();
            sMusicPaused = true;
        }
        setSharp(SharpSettings, sScreen->isShown());
        if (sScreen->isShown()) {
            MR::requestStarPointerModePauseMenu(sScreen);
            MR::updateStarPointerModeWhileSceneHeld();
            sScreen->calcAnim();
        }
    }

    void draw() {
        if (sScreen != nullptr) {
            sScreen->drawMenu();
        }
    }

    bool isFreezingScene() {
        return sFreezing;
    }

    bool isShown() {
        return sScreen != nullptr && sScreen->isShown();
    }

    void requestOpen() {
        __NebulaSettingsInput(NEBULA_SETTINGS_OPEN, 0);
    }
};  // namespace NebulaSettingsMenu
#endif
