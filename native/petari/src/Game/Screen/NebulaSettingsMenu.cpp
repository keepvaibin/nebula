#include "Game/Util/TextString.hpp"
#include "Game/Screen/NebulaSettingsMenu.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include <nebula_module.h>

#include "Game/AudioLib/AudSystem.hpp"
#include "Game/AudioLib/AudWrap.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaIcons.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/NebulaCutsceneSkip.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Screen/NebulaHomeMenu.hpp"
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>
#include <Game/Ui/Layout.hpp>
#include "Game/System/NebulaInput.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaHost.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/Render/RenderTexture.hpp"
#include <nebula_cooked_texture.h>

using namespace NebulaUi;

namespace {
    // Screen units (NebulaUi::readScreen): 456 high, 810 wide at 16:9.
    NEBULA_TUNABLE(f32, cMargin, 16.0f);     // the window from the screen's edges
    NEBULA_TUNABLE(f32, cMaxWidth, 900.0f);  // a wide screen keeps a window that reads as one
    NEBULA_TUNABLE(f32, cMinWindowWidth, 480.0f);  // the tabs and the footer keep their shape down to this
    NEBULA_TUNABLE(f32, cReadableTextDp, 14.0f);  // a row's text on the screen, at the least, unless the player chooses otherwise
    NEBULA_TUNABLE(f32, cZoomMax, 1.8f);
    NEBULA_TUNABLE(f32, cPad, 26.0f);        // the window's inside
    NEBULA_TUNABLE(f32, cTitleSize, 21.0f);
    NEBULA_TUNABLE(f32, cTabHeight, 30.0f);
    NEBULA_TUNABLE(f32, cTabSize, 12.5f);
    NEBULA_TUNABLE(f32, cTabGap, 5.0f);
    NEBULA_TUNABLE(f32, cRowHeight, 52.0f);
    NEBULA_TUNABLE(s32, cSwipeHoverFrames, 12);
    NEBULA_TUNABLE(f32, cRowGap, 3.0f);  // between two rows' frames
    NEBULA_TUNABLE(f32, cModHeight, 40.0f);  // a mod in the MODS page's lists
    NEBULA_TUNABLE(f32, cTextSize, 15.5f);
    NEBULA_TUNABLE(f32, cHeadingSize, 13.5f);
    NEBULA_TUNABLE(f32, cFooterSize, 12.0f);
    NEBULA_TUNABLE(f32, cFooterLine, 19.0f);
    NEBULA_TUNABLE(f32, cButtonHeight, 30.0f);
    NEBULA_TUNABLE(f32, cButtonSize, 12.5f);
    NEBULA_TUNABLE(f32, cButtonGap, 10.0f);
    NEBULA_TUNABLE(f32, cRowButtonHeight, 36.0f);  // a row's own buttons (IMPORT, OPEN) and a setting's arrows
    NEBULA_TUNABLE(f32, cRowButtonSize, 14.0f);
    NEBULA_TUNABLE(f32, cRowButtonGap, 8.0f);
    NEBULA_TUNABLE(f32, cRowButtonMin, 76.0f);
    NEBULA_TUNABLE(f32, cArrowWidth, 58.0f);
    NEBULA_TUNABLE(f32, cArrowSize, 20.0f);
    NEBULA_TUNABLE(f32, cValueMin, 120.0f);  // a setting's value between its arrows, the same on every row
    NEBULA_TUNABLE(f32, cValueMax, 250.0f);
    NEBULA_TUNABLE(f32, cSliderHeight, 8.0f);  // a slider's track; its knob is the game's round button
    NEBULA_TUNABLE(f32, cKnobSize, 32.0f);
    NEBULA_TUNABLE(f32, cSliderValue, 64.0f);  // its value after it
    NEBULA_TUNABLE(s32, cRepeatDelay, 18);     // an arrow held down: the frames before it steps again, then between steps
    NEBULA_TUNABLE(s32, cRepeatEvery, 5);
    NEBULA_TUNABLE(f32, cEdgeDrop, 2.0f);  // a row's own button's darker edge under it; the two centred together on the row
    NEBULA_TUNABLE(f32, cRowButtonRadius, 11.0f);  // a row's own buttons have the rows' corners, near enough (the rows': 12)
    NEBULA_TUNABLE(f32, cReach, 8.0f);  // how far past a button's edge above and below the pointer still has it
    NEBULA_TUNABLE(f32, cMinSqueeze, 0.6f);  // the narrowest a line of text is drawn
    NEBULA_TUNABLE(f32, cKeyGap, 6.0f);  // a key from its word
    NEBULA_TUNABLE(f32, cBadgeScale, 1.35f);  // a controller's button: its height for the text's size
    NEBULA_TUNABLE(f32, cBadgeGap, 3.0f);
    const s32 cGlyphPad = 'C';  // the picture font's +Control Pad
    NEBULA_TUNABLE(f32, cHintGap, 18.0f);
    NEBULA_TUNABLE(f32, cQuestionWidth, 470.0f);
    NEBULA_TUNABLE(f32, cLicenseTextSize, 12.0f);
    NEBULA_TUNABLE(f32, cLicenseInset, 12.0f);  // the license's window inside the menu's
    NEBULA_TUNABLE(f32, cQuestionHeight, 180.0f);
    NEBULA_TUNABLE(f32, cQuestionMargin, 22.0f);  // the answers from the window's sides and foot
    NEBULA_TUNABLE(f32, cQuestionRing, 6.4f);     // the window's white ring (measured), inside its edges
    // The Pair Joy-Con question: the letters L and R, the halves (the owner's drawing, cJoyConUnit of the menu's units
    // to its each; NebulaIcons paints them), its text and its answer, as far from each other as the answer is from the
    // window's ring.
    NEBULA_TUNABLE(f32, cJoyConUnit, 3.4f);
    const f32 cJoyConGap = 5.36f;  // between the rails
    const f32 cJoyConCentre = cJoyConGap * 0.5f + NebulaIcons::cJoyConRail + NebulaIcons::cJoyConBody[0] * 0.5f;  // a body's middle
    NEBULA_TUNABLE(f32, cJoyConLetter, 1.0f);             // a letter toward its half's outer side
    NEBULA_TUNABLE(f32, cJoyConLetterGap, 6.0f);          // the letters' feet to the shoulders' tops, menu units
    NEBULA_TUNABLE(u32, cJoyConPaintAhead, 1500);         // painting the pictures a frame, us: on the Controls page
    NEBULA_TUNABLE(u32, cJoyConPaintShown, 6000);         // and on the Pair Joy-Con screen
    NEBULA_TUNABLE(f32, cHomeWidth, 340.0f);  // the HOME Menu's window: its title over its buttons, one above the other
    NEBULA_TUNABLE(f32, cHomeButtonWidth, 250.0f);
    NEBULA_TUNABLE(f32, cHomeGap, 12.0f);
    NEBULA_TUNABLE(s32, cDropFrames, 16);  // the window's drop from above, and its rise away
    NEBULA_TUNABLE(s32, cRowsFirst, 96);  // the rows made with the screen; more when a page has more
    NEBULA_TUNABLE(s32, cTabsFirst, 8);   // the page buttons made with the screen; more when there are more pages
    const s32 cHintsMax = 3;
    NEBULA_TUNABLE(s32, cRowButtonsFirst, 6);  // the launcher's buttons made with the screen; more when a row has more
    NEBULA_TUNABLE(f32, cModsGap, 26.0f);      // the MODS page: between its two lists
    NEBULA_TUNABLE(f32, cDragStart, 5.0f);     // how far the pointer moves something held down before it is dragged
    NEBULA_TUNABLE(f32, cEdgeScrollRows, 8.0f);  // a list under a dragged mod at its edge scrolls this many rows a second at most
    NEBULA_TUNABLE(s32, cRowChipsFirst, 48);   // the rows' buttons made with the screen; more when the rows in sight need them

    // The quit question's window and text; the game's blue for what is chosen.
    const u32 cText = 0x4A5274FF;
    NEBULA_TUNABLE(u32, cSubText, 0x737B9AFF);
    NEBULA_TUNABLE(u32, cAccent, 0x2F6FD0FF);
    const u32 cDisabled = NEBULA_RGBA(0x9A, 0xA2, 0xBC, 0xFF);
    const u32 cWarning = NEBULA_RGBA(0xC8, 0x40, 0x2E, 0xFF);
    NEBULA_TUNABLE(u32, cHighlightFill, 0xFFFFFFFF);
    NEBULA_TUNABLE(u32, cFrameFill, 0xFFFFFFB4);   // a row's frame
    NEBULA_TUNABLE(u32, cFrameFixed, 0xFFFFFF5A);  // a row that only shows something
    const u32 cFrameRing = NEBULA_RGBA(0xC6, 0xD3, 0xEB, 0xFF);
    NEBULA_TUNABLE(u32, cHighlightRing, 0x7EC8F4FF);
    NEBULA_TUNABLE(u32, cRule, 0xB4C2DEFF);
    NEBULA_TUNABLE(u32, cTrack, 0x4A527428);
    const u32 cSliderTrack = NEBULA_RGBA(0xCB, 0xD6, 0xEB, 0xFF);
    NEBULA_TUNABLE(u32, cSliderFill, 0x6FA6EAFF);
    NEBULA_TUNABLE(u32, cRowButtonFill, 0xE4F6FFFF);  // a row's own button: its face, its ring, the edge under it
    NEBULA_TUNABLE(u32, cRowButtonRing, 0x5CC4F0FF);
    NEBULA_TUNABLE(u32, cRowButtonLit, 0xFFFFFFFF);  // focused
    NEBULA_TUNABLE(u32, cRowButtonLitRing, 0x2F9AEEFF);
    NEBULA_TUNABLE(u32, cRowButtonEdge, 0x3E86C8FF);
    NEBULA_TUNABLE(u32, cRowButtonOff, 0xEEF1F6FF);  // unavailable
    NEBULA_TUNABLE(u32, cThumb, 0x7EA8E8FF);
    NEBULA_TUNABLE(u32, cButtonText, 0x505050FF);

    enum { FooterApply, FooterBack, FooterNum };
    enum { ChoiceNum = 3 };

    // What the hints and buttons name a key or button for. KeyPress: a row's button (IMPORT, OPEN) is pressed with
    // Space or the bottom button.
    enum { KeyApply, KeyUndo, KeyBack, KeyPage, KeyChange, KeyBind, KeyPress, KeyNum };

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
        {NEBULA_SETTINGS_STATUS_DLSS_ASK, "SettingsDlssAsk", "SettingsDlssAsk", 3,
         {"SettingsChoiceDownload", "SettingsChoiceLicense", "SettingsChoiceNotNow"},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_REVERT, NEBULA_SETTINGS_CLOSE},
         {KeyApply, KeyUndo, KeyBack}},
        {NEBULA_SETTINGS_STATUS_XESS_ASK, "SettingsXessAsk", "SettingsXessAsk", 3,
         {"SettingsChoiceDownload", "SettingsChoiceLicense", "SettingsChoiceNotNow"},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_REVERT, NEBULA_SETTINGS_CLOSE},
         {KeyApply, KeyUndo, KeyBack}},
        {NEBULA_SETTINGS_STATUS_DLSS_INSTALLING, "SettingsDlssInstalling", "SettingsDlssInstalling", 1,
         {"SettingsChoiceCancel", nullptr, nullptr},
         {NEBULA_SETTINGS_CLOSE, 0, 0},
         {KeyBack, 0, 0}},
        {NEBULA_SETTINGS_STATUS_XESS_INSTALLING, "SettingsXessInstalling", "SettingsXessInstalling", 1,
         {"SettingsChoiceCancel", nullptr, nullptr},
         {NEBULA_SETTINGS_CLOSE, 0, 0},
         {KeyBack, 0, 0}},
        {NEBULA_SETTINGS_STATUS_XESS_RESTART, "SettingsXessRestart", "SettingsXessRestart", 2,
         {"SettingsChoiceRestart", "SettingsChoiceLater", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
        // Its text is the page the host sends (fillTexts).
        {NEBULA_SETTINGS_STATUS_LICENSE, "SettingsLicenseLoading", "SettingsLicenseLoading", 3,
         {"SettingsChoicePrevious", "SettingsChoiceNext", "SettingsBack"},
         {NEBULA_SETTINGS_REVERT, NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE},
         {KeyUndo, KeyApply, KeyBack}},
        {NEBULA_SETTINGS_STATUS_DLSS_RESTART, "SettingsDlssRestart", "SettingsDlssRestart", 2,
         {"SettingsChoiceRestart", "SettingsChoiceLater", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
        {NEBULA_SETTINGS_STATUS_MODS_RESTART_ASK, "SettingsModsRestartAsk", "SettingsModsRestartAsk", 2,
         {"SettingsChoiceRestart", "SettingsChoiceLater", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
        {NEBULA_SETTINGS_STATUS_RT_RESTART_ASK, "SettingsRtRestartAsk", "SettingsRtRestartAsk", 2,
         {"SettingsChoiceRestart", "SettingsChoiceLater", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
        {NEBULA_SETTINGS_STATUS_MODS_CODE_ASK, "SettingsModsCodeAsk", "SettingsModsCodeAsk", 2,
         {"SettingsChoiceAllow", "SettingsChoiceNotNow", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
        {NEBULA_SETTINGS_STATUS_DIAGNOSTICS_ASK, "SettingsDiagnosticsAsk", "SettingsDiagnosticsAsk", 2,
         {"SettingsChoiceRestart", "SettingsChoiceNotNow", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
        // Its text follows the halves (fillTexts).
        {NEBULA_SETTINGS_STATUS_JOYCON, "SettingsJoyConPress", "SettingsJoyConPress", 1,
         {"SettingsBack", nullptr, nullptr},
         {NEBULA_SETTINGS_CLOSE, 0, 0},
         {KeyBack, 0, 0}},
        // The phone's on-screen controls dragged over the picture: the window alone, the page under it hidden.
        {NEBULA_SETTINGS_STATUS_ARRANGE, "SettingsArrange", "SettingsArrange", 3,
         {"SettingsChoiceSave", "SettingsChoiceReset", "SettingsChoiceCancel"},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_REVERT, NEBULA_SETTINGS_CLOSE},
         {KeyApply, KeyUndo, KeyBack}},
        // A mod's description and pictures, paged as a license: its text the page the host sends, its picture under it
        // (fillTexts, placeModPicture).
        {NEBULA_SETTINGS_STATUS_MOD_ABOUT, "SettingsModPictureLoading", "SettingsModPictureLoading", 3,
         {"SettingsChoicePrevious", "SettingsChoiceNext", "SettingsBack"},
         {NEBULA_SETTINGS_REVERT, NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE},
         {KeyUndo, KeyApply, KeyBack}},
        // To the recycle bin (1) or deleted (0).
        {NEBULA_SETTINGS_STATUS_MOD_REMOVE_ASK, "SettingsModRemoveAsk", "SettingsModRemoveAskBin", 2,
         {"SettingsChoiceRemove", "SettingsChoiceKeep", nullptr},
         {NEBULA_SETTINGS_APPLY, NEBULA_SETTINGS_CLOSE, 0},
         {KeyApply, KeyBack, 0}},
    };
    // The ABOUT window's pictures: at most this many texels a side (the host fits them so).
    const u32 cModPictureSide = 1024;
    const s32 cQuestionNum = sizeof(cQuestions) / sizeof(cQuestions[0]);

    // A half of the Pair Joy-Con picture: its texture (made when the screen first shows) and its shoulder's letter.
    struct JoyConHalf {
        Picture* mPicture;
        TextBox* mLetter;
        Ui::TexMap* mMap;  // its picture's texture as set
        s32 mPresses;  // its button presses as last seen (-1: not yet)
        f32 mPop;      // a press's pop, 1 at the press and down to 0
    };

    // The HOME Menu (the host's NEBULA_HOME_MENU_VIEW): its rows top to bottom, then "are you sure?" about the choice it asks
    // about (1 leave, 2 reset), answered no or yes as the game's own questions are.
    const char* const cHomeRows[] = {"HomeMenuReturn", "HomeMenuReset", "Quit"};
    const char* const cHomeAnswers[] = {"HomeMenuNo", "HomeMenuYes"};
    const s32 cHomeRowNum = sizeof(cHomeRows) / sizeof(cHomeRows[0]);
    const s32 cHomeAnswerNum = sizeof(cHomeAnswers) / sizeof(cHomeAnswers[0]);

    // One of a row's buttons: its word, whether it is unavailable, and where it was drawn.
    struct RowButton {
        MR::GrowBuffer< char16_t > mWord;
        bool mOff;
        f32 mX;
        f32 mWidth;
    };

    struct Row {
        Pane* mRoot;
        Window* mFrame;  // the row as a button
        TextBox* mLabel;
        TextBox* mValue;
        Window* mRule;
        f32 mLabelWidth;
        f32 mValueWidth;
        bool mIndented;
        s32 mOnPlace;  // the number drawn before its label (a mod on), 0 for none
        // A row of buttons: their words in the played language, those unavailable, and where each was drawn (its centre
        // and width).
        s32 mButtonCount;
        MR::GrowBuffer< RowButton > mButtons;
        u32 mButtonsVersion;  // changes with the words
        // A setting's arrows (their centres) or its slider (its track's ends), where they were drawn.
        bool mArrows;
        f32 mArrowX[2];
        f32 mSliderLeft;
        f32 mSliderRight;
        Window* mSliderTrack;
        Window* mSliderFill;
    };

    // The row's next button, its word and place empty: the room for them grows with the row's buttons.
    RowButton& addRowButton(Row& rRow) {
        const u32 had = rRow.mButtons.mRoom;
        RowButton* const pButtons = rRow.mButtons.room(static_cast< u32 >(rRow.mButtonCount) + 1);
        for (u32 i = had; i < rRow.mButtons.mRoom; i++) {
            memset(static_cast< void* >(&pButtons[i]), 0, sizeof(RowButton));
        }
        return pButtons[rRow.mButtonCount++];
    }

    // One of the buttons drawn on the rows in sight, and the row's button it is now: one of a row of buttons, a
    // setting's arrow back or on, a slider's knob.
    enum { ChipLess = -1, ChipMore = -2, ChipKnob = -3 };
    struct RowChip {
        Chip mChip;    // its word (the game's button art stays hidden: its ends are a fixed pill)
        Window* mEdge;  // drawn as a box with the rows' corners: a darker edge under its face
        Window* mFace;
        s32 mRow;  // mRows' index; -1 unused
        s32 mButton;
        u32 mVersion;
        f32 mInkX;  // an arrow's ink from its middle (NebulaUi::inkOffset)
        f32 mInkY;
    };

    // A key or a controller's buttons, as what the player last used shows it: the keyboard's key in words (Enter), the
    // D-pad as the game's own picture, a controller's other buttons as they look on it: its face buttons round in their
    // colours and its shoulders as pills.
    struct KeyCap {
        TextBox* mWord;  // the keyboard's key, or the D-pad
        Window* mBadges[2];
        TextBox* mLabels[2];
        s32 mBadgeCount;
        f32 mBadgeWidths[2];
        f32 mInkX[2];  // each label's ink from its middle (NebulaUi::inkOffset)
        f32 mInkY[2];
        Vec2f mLabelScales[2];
        bool mGlyph;  // mWord is the D-pad
        bool mKeyIcon;  // mWord is the keyboard's keys (NebulaIcons::keyCode)
        f32 mSize;    // the size of the text it stands with
        f32 mWidth;
    };

    // A controller's button: its label, its fill and its label's colour.
    struct PadButton {
        const char16_t* mLabel;
        u32 mFill;
        u32 mInk;
    };

    // Change, close, undo and apply on the controllers (NEBULA_SETTINGS_INPUT_XBOX for Xbox and the others, _NINTENDO,
    // _PLAYSTATION) as the host reads their buttons (Xbox's A, B, X and Y; a Switch controller's by their labels, a
    // PlayStation controller's by place): Xbox's in their colours, the others white with the window's ink; then the
    // shoulders, which turn the pages.
    const PadButton cPadFaces[3][4] = {
        {{u"A", 0x5FA83CFF, 0xFFFFFFFF}, {u"B", NEBULA_RGBA(0xD8, 0x44, 0x3A, 0xFF), 0xFFFFFFFF}, {u"X", 0x3C78D8FF, 0xFFFFFFFF}, {u"Y", 0xE6B422FF, 0x3A3A3AFF}},
        {{u"A", 0xFFFFFFFF, cText}, {u"B", 0xFFFFFFFF, cText}, {u"X", 0xFFFFFFFF, cText}, {u"Y", 0xFFFFFFFF, cText}},
        {{u"\u00D7", 0xFFFFFFFF, cText}, {u"\u25CB", 0xFFFFFFFF, cText}, {u"\u25A1", 0xFFFFFFFF, cText}, {u"\u25B3", 0xFFFFFFFF, cText}},
    };
    const char16_t* const cPadShoulders[3][2] = {{u"LB", u"RB"}, {u"L", u"R"}, {u"L1", u"R1"}};
    const PadButton cShoulders[3] = {{nullptr, 0x45474EFF, 0xFFFFFFFF}, {nullptr, 0xFFFFFFFF, cText}, {nullptr, 0xFFFFFFFF, cText}};
    NEBULA_TUNABLE(u32, cBadgeRing, 0xB4C2DEFF);  // round a white badge, as the window's rules

    // A button in the game's art with its key before its word ("Enter  Apply").
    struct Button {
        Chip mChip;
        KeyCap mKey;
        f32 mWordWidth;
        s32 mAction;
    };

    struct Hint {
        KeyCap mKey;
        TextBox* mWord;
        f32 mWordWidth;
    };

    // UTF-8 (the host's text) as the game's 16-bit text, after the first `used` units of `rDst`.
    void fromUtf8(MR::Text16& rDst, u32 used, const char* pSrc) {
        // A unit takes at least one byte.
        char16_t* const pDst = rDst.grow(used, used + static_cast< u32 >(strlen(pSrc)) + 1);
        u32 length = used;
        const u8* p = reinterpret_cast< const u8* >(pSrc);
        while (*p != 0) {
            u32 c = *p++;
            if (c >= 0xE0 && p[0] != 0 && p[1] != 0) {
                c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
                p += 2;
            } else if (c >= 0xC0 && p[0] != 0) {
                c = ((c & 0x1F) << 6) | (p[0] & 0x3F);
                p += 1;
            }
            pDst[length++] = static_cast< char16_t >(c);
        }
        pDst[length] = 0;
    }

    // The argument a {n}, {0}, {1}... at `s` names (past {3}: one the text has), its index; -1 when `s` is none. *pLength
    // is the token's length.
    s32 argumentAt(const char16_t* s, s32 argCount, s32* pLength) {
        if (s[0] != u'{') {
            return -1;
        }
        if (s[1] == u'n' && s[2] == u'}') {
            *pLength = 3;
            return 0;
        }
        s32 value = 0;
        s32 length = 1;
        while (s[length] >= u'0' && s[length] <= u'9' && value < 100000) {
            value = value * 10 + (s[length] - u'0');
            length++;
        }
        if (length > 1 && s[length] == u'}' && (value <= 3 || value < argCount)) {
            *pLength = length + 1;
            return value;
        }
        return -1;
    }

    // `pPattern` with each {0} to {3} (and past them, the arguments there are) as that argument and {n} as the first.
    void expand(MR::Text16& rDst, const char16_t* pPattern, const char16_t* const* ppArgs, s32 argCount) {
        char16_t* pOut = nullptr;
        for (s32 pass = 0; pass < 2; pass++) {
            u32 length = 0;
            for (const char16_t* s = pPattern; *s != 0; s++) {
                s32 tokenLength = 0;
                const s32 index = argumentAt(s, argCount, &tokenLength);
                if (index < 0) {
                    if (pOut != nullptr) {
                        pOut[length] = *s;
                    }
                    length++;
                    continue;
                }
                for (const char16_t* p = index < argCount ? ppArgs[index] : u""; *p != 0; p++) {
                    if (pOut != nullptr) {
                        pOut[length] = *p;
                    }
                    length++;
                }
                s += tokenLength - 1;
            }
            if (pass == 0) {
                pOut = rDst.room(length + 1);
            } else {
                pOut[length] = 0;
            }
        }
    }

    // NebulaText's `pId` with its {n} or {0}... as the arguments.
    __attribute__((noinline)) void getExpanded(MR::Text16& rDst, const char* pId, const char16_t* const* ppArgs, s32 argCount) {
        MR::Text16 pattern;
        getText(pattern, pId);
        expand(rDst, pattern.c_str(), ppArgs, argCount);
    }

    // A line too long in a language written without spaces (Japanese, Chinese), in two: between the characters at its
    // middle, moved on past closing punctuation, which never starts a line. False when it has a break already.
    bool breakUnspaced(MR::Text16& rText) {
        char16_t* pText = rText.data();
        s32 length = 0;
        while (pText[length] != 0) {
            if (pText[length] == u'\n') {
                return false;
            }
            length++;
        }
        if (length < 2) {
            return false;
        }
        pText = rText.grow(static_cast< u32 >(length) + 1, static_cast< u32 >(length) + 2);
        s32 at = 0;
        static const char16_t cClosing[] = u"\u3001\u3002\uFF0C\uFF0E\uFF01\uFF1F\uFF09\u300D\u300F)!?.,";
        at = length / 2;
        const auto closing = [](char16_t c) {
            for (const char16_t* p = cClosing; *p != 0; p++) {
                if (*p == c) {
                    return true;
                }
            }
            return false;
        };
        while (at < length - 1 && closing(pText[at])) {
            at++;
        }
        for (s32 i = length; i >= at; i--) {
            pText[i + 1] = pText[i];
        }
        pText[at] = u'\n';
        return true;
    }

    // NebulaText's `pId` with {n} as `number`.
    void getNumbered(MR::Text16& rDst, const char* pId, s32 number) {
        const MR::FormattedName digits("%d", static_cast< int >(number));
        MR::Text16 wide;
        fromUtf8(wide, 0, digits.c_str());
        const char16_t* const args[] = {wide.c_str()};
        getExpanded(rDst, pId, args, 1);
    }

    // A host text's entry in the played language: its NebulaText id is "S." and the text with each space as _ and each
    // number as #, and the entry names the numbers {0} to {3} in their order ("S.#_FPS" = "{0} FPS"). False when the
    // table has none.
    __attribute__((noinline)) bool lookUp(MR::Text16& rDst, const char* pText) {
        // The id has a character for each of the text's and a number is one; the numbers, each with its terminator,
        // follow one another (a run of digits and its terminator take no more units than its digits and the byte
        // after them).
        const u32 textBytes = static_cast< u32 >(strlen(pText));
        MR::FormattedName idText;
        char* const id = idText.room(textBytes + 3);
        id[0] = 'S';
        id[1] = '.';
        s32 idLength = 2;
        MR::Text16 numberText;
        char16_t* const pNumbers = numberText.room(textBytes + 2);
        MR::InlineVector< u32, 4 > starts;
        u32 used = 0;
        for (const char* p = pText; *p != 0;) {
            if (*p < '0' || *p > '9') {
                id[idLength++] = *p == ' ' ? '_' : *p;
                p++;
                continue;
            }
            starts.push_back(used);
            while ((*p >= '0' && *p <= '9') || (*p == '.' && p[1] >= '0' && p[1] <= '9')) {
                pNumbers[used++] = static_cast< char16_t >(*p);
                p++;
            }
            pNumbers[used++] = 0;
            id[idLength++] = '#';
        }
        id[idLength] = 0;
        if (!NebulaText::has(id)) {
            return false;
        }
        MR::InlineVector< const char16_t*, 4 > args;
        for (s32 i = 0; i < starts.size(); i++) {
            args.push_back(pNumbers + starts[i]);
        }
        getExpanded(rDst, id, args.begin(), args.size());
        return true;
    }

    // `pText`'s first `length` letters (no space at their end) and an ellipsis.
    void shorten(MR::Text16& rDst, const char16_t* pText, s32 length) {
        char16_t* const pDst = rDst.room(static_cast< u32 >(length) + 2);
        s32 i = 0;
        for (; i < length && pText[i] != 0; i++) {
            pDst[i] = pText[i];
        }
        while (i > 0 && pDst[i - 1] == u' ') {
            i--;
        }
        pDst[i++] = 0x2026;
        pDst[i] = 0;
    }

    // A sentence's first letter as a capital (the Latin letters the game's font has; other scripts have none).
    void capitalize(char16_t* pText) {
        const char16_t c = pText[0];
        if ((c >= u'a' && c <= u'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) {
            pText[0] = static_cast< char16_t >(c - 0x20);
        }
    }

    // A host text in the played language: its entry, or else piece by piece, each piece the same way: the words before
    // a closing "(...)" and those inside it, then its sentences, the words before ": " and after it, a list's items
    // ("8X (OFF WITH DLSS / FSR 3)", "RIIVOLUTION, FILES. TEXT: ENGLISH"), joined as the language joins them. A name
    // only whole (the keyboard's "Keyboard and mouse"); words the table does not have as the host gives them.
    __attribute__((noinline)) void translate(MR::Text16& rDst, const char* pText, bool name) {
        rDst.room(1);
        if (pText == nullptr || pText[0] == 0 || lookUp(rDst, pText)) {
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
            const MR::FormattedName first("%.*s", static_cast< int >(firstLength), pText);
            const MR::FormattedName second("%.*s", static_cast< int >(secondEnd - secondStart), pText + secondStart);
            MR::Text16 firstText;
            MR::Text16 secondText;
            translate(firstText, first.c_str(), false);
            translate(secondText, second.c_str(), false);
            if (i == 1) {
                capitalize(firstText.data());
                capitalize(secondText.data());
            }
            const char16_t* const args[] = {firstText.c_str(), secondText.c_str()};
            getExpanded(rDst, cJoins[i], args, 2);
            return;
        }
        fromUtf8(rDst, 0, pText);
    }

    // A host text as a whole line or value: translated, its first letter a capital unless it is a name ("on" inside
    // "(off with MSAA)" stays small).
    void translateLine(MR::Text16& rDst, const char* pText, bool name) {
        translate(rDst, pText, name);
        if (!name) {
            capitalize(rDst.data());
        }
    }

    // The keyboard's key for `key`, in the played language; `controls`: the bindings page's own (Space applies).
    void getKeyName(MR::Text16& rDst, s32 key, bool controls) {
        if (key == KeyChange) {
            char16_t* const pDst = rDst.room(3);
            pDst[0] = 0x2190;
            pDst[1] = 0x2192;
            pDst[2] = 0;
            return;
        }
        static const char* const cKeys[KeyNum] = {"KeyEnter", "KeyBackspace", "KeyEscape", "KeyTab", nullptr, "KeyEnter", "KeySpace"};
        getText(rDst, key == KeyApply && controls ? "KeySpace" : cKeys[key]);
    }

    // `a` to `b` by `t` (0 to 1), each channel.
    u32 mix(u32 a, u32 b, f32 t) {
        u32 out = 0;
        for (s32 shift = 0; shift < 32; shift += 8) {
            const f32 from = static_cast< f32 >(a >> shift & 0xFF);
            const f32 to = static_cast< f32 >(b >> shift & 0xFF);
            out |= static_cast< u32 >(from + (to - from) * t + 0.5f) << shift;
        }
        return out;
    }

    u32 darker(u32 rgba) {
        const u32 r = (rgba >> 24 & 0xFF) * 3 / 4;
        const u32 g = (rgba >> 16 & 0xFF) * 3 / 4;
        const u32 b = (rgba >> 8 & 0xFF) * 3 / 4;
        return r << 24 | g << 16 | b << 8 | (rgba & 0xFF);
    }

    bool makeKeyCap(const Kits& rKits, Pane* pParent, KeyCap* pCap, const char* pName, f32 size) {
        MR::FormattedName name;
        pCap->mSize = size;
        name.format("%sK", pName);
        pCap->mWord = makeText(rKits, pParent, name.c_str(), 16);
        if (pCap->mWord == nullptr) {
            return false;
        }
        setTextStyle(pCap->mWord, size, cAccent, cAccent, 4);
        for (s32 i = 0; i < 2; i++) {
            name.format("%sB%d", pName, static_cast< int >(i));
            pCap->mBadges[i] = makeWindow(rKits, pParent, name.c_str());
            name.format("%sL%d", pName, static_cast< int >(i));
            pCap->mLabels[i] = makeText(rKits, pParent, name.c_str(), 4);
            if (pCap->mBadges[i] == nullptr || pCap->mLabels[i] == nullptr) {
                return false;
            }
            setTextStyle(pCap->mLabels[i], size * 0.85f, 0xFFFFFFFF, 0xFFFFFFFF, 4);
            pCap->mBadges[i]->SetVisible(false);
            pCap->mLabels[i]->SetVisible(false);
        }
        return true;
    }

    // The keyboard's key for `key` (NEBULA_KEY_*); `controls`: the bindings page's own (Space applies). Changing: the
    // left arrow (the right one beside it).
    s32 keyboardKey(s32 key, bool controls) {
        static const s32 cKeys[KeyNum] = {NEBULA_KEY_ENTER, NEBULA_KEY_BACKSPACE, NEBULA_KEY_ESCAPE, NEBULA_KEY_TAB,
                                          NEBULA_KEY_LEFT,  NEBULA_KEY_ENTER,     NEBULA_KEY_SPACE};
        return key == KeyApply && controls ? NEBULA_KEY_SPACE : cKeys[key];
    }

    // `key` (KeyApply...; -1 none) as `input` (NEBULA_SETTINGS_INPUT_*) shows it: the keyboard's as its keys' icons
    // (as the game's icons draw them; its name in words while the icons cannot be drawn), a controller's as its buttons.
    void fillKeyCap(KeyCap& rCap, s32 key, u32 input, bool controls) {
        f32 height = 0.0f;
        if (input == NEBULA_SETTINGS_INPUT_TOUCH) {
            key = -1;
        }
        const bool pad = input >= NEBULA_SETTINGS_INPUT_XBOX && input <= NEBULA_SETTINGS_INPUT_PLAYSTATION;
        rCap.mBadgeCount = 0;
        rCap.mWidth = 0.0f;
        rCap.mGlyph = false;
        rCap.mKeyIcon = false;
        rCap.mWord->SetVisible(key >= 0 && (!pad || key == KeyChange));
        const u16 keyIcon = key >= 0 && !pad ? NebulaIcons::keyCode(keyboardKey(key, controls)) : 0;
        if (keyIcon != 0) {
            const f32 glyphSize = rCap.mSize * 1.3f;
            setGlyph(rCap.mWord, keyIcon, glyphSize);
            s32 glyphs = 1;
            if (key == KeyChange) {
                // Left and right, side by side.
                char16_t text[16] = {0};
                MR::addPictureFontTag(MR::addPictureFontTag(text, keyIcon), NebulaIcons::keyCode(NEBULA_KEY_RIGHT));
                setText(rCap.mWord, text);
                glyphs = 2;
            }
            setTextColor(rCap.mWord, 0xFFFFFFFF, 0xFFFFFFFF);
            rCap.mGlyph = true;
            rCap.mKeyIcon = true;
            rCap.mWidth = glyphFontSize(rCap.mWord, glyphSize).width * glyphs;
            rCap.mWord->mSize = Ui::Size(rCap.mWidth + glyphSize, glyphSize * 2.0f);
        } else if (key >= 0 && !pad) {
            MR::Text16 text;
            getKeyName(text, key, controls);
            setTextStyle(rCap.mWord, rCap.mSize, cAccent, cAccent, 4);
            setText(rCap.mWord, text.c_str());
            measure(rCap.mWord, 4000.0f, &rCap.mWidth, &height);
        } else if (key == KeyChange) {
            rCap.mGlyph = true;
            setGlyph(rCap.mWord, cGlyphPad, rCap.mSize * 1.3f);
            setTextColor(rCap.mWord, 0xFFFFFFFF, 0xFFFFFFFF);
            rCap.mWidth = glyphFontSize(rCap.mWord, rCap.mSize * 1.3f).width;
        } else if (key >= 0) {
            const s32 layout = static_cast< s32 >(input) - 1;
            const f32 diameter = rCap.mSize * cBadgeScale;
            static const s32 cFace[KeyNum] = {3, 2, 1, -1, 0, 0, 0};
            rCap.mBadgeCount = key == KeyPage ? 2 : 1;
            for (s32 i = 0; i < rCap.mBadgeCount; i++) {
                const PadButton& rLook = key == KeyPage ? cShoulders[layout] : cPadFaces[layout][cFace[key]];
                const bool face = key != KeyPage;
                setText(rCap.mLabels[i], face ? rLook.mLabel : cPadShoulders[layout][i]);
                setTextColor(rCap.mLabels[i], rLook.mInk, rLook.mInk);
                paint(rCap.mBadges[i], rLook.mFill, rLook.mFill == 0xFFFFFFFF ? cBadgeRing : darker(rLook.mFill));
                f32 labelWidth = 0.0f;
                measure(rCap.mLabels[i], 4000.0f, &labelWidth, &height);
                // Centred on the badge by its ink, whatever the glyph's own margins; the font's square (taller than
                // wide) drawn square and smaller, as on the controller.
                f32 inkWidth = 0.0f;
                f32 inkHeight = 0.0f;
                rCap.mLabelScales[i] = Vec2f(1.0f, 1.0f);
                if (!inkOffset(rCap.mLabels[i], &rCap.mInkX[i], &rCap.mInkY[i], &inkWidth, &inkHeight)) {
                    rCap.mInkX[i] = 0.0f;
                    rCap.mInkY[i] = 0.0f;
                } else if (face && rLook.mLabel[0] == 0x25A1 && inkWidth > 0.0f) {
                    rCap.mLabelScales[i] = Vec2f(0.78f * inkHeight / inkWidth, 0.78f);
                }
                const PadButton* pFace = face ? &rLook : nullptr;
                // A face button round, a shoulder a pill as long as its name.
                rCap.mBadgeWidths[i] = pFace != nullptr ? diameter : labelWidth + diameter * 0.75f;
                rCap.mWidth += rCap.mBadgeWidths[i] + (i > 0 ? cBadgeGap : 0.0f);
            }
        }
        for (s32 i = 0; i < 2; i++) {
            rCap.mBadges[i]->SetVisible(i < rCap.mBadgeCount);
            rCap.mLabels[i]->SetVisible(i < rCap.mBadgeCount);
        }
    }

    // Its left end at `left`, its middle at `y`.
    void placeKeyCap(KeyCap& rCap, f32 left, f32 y) {
        if (rCap.mBadgeCount == 0) {
            rCap.mWord->mTranslate = Vec3f(left + rCap.mWidth * 0.5f,
                                                      rCap.mKeyIcon ? y - cKeyGlyphDrop * rCap.mSize * 1.3f
                                                      : rCap.mGlyph ? glyphCentre(y, rCap.mSize * 1.3f)
                                                                    : textCentre(y, rCap.mSize),
                                                      0.0f);
            return;
        }
        const f32 diameter = rCap.mSize * cBadgeScale;
        f32 x = left;
        for (s32 i = 0; i < rCap.mBadgeCount; i++) {
            const f32 width = rCap.mBadgeWidths[i];
            shape(rCap.mBadges[i], x + width * 0.5f, y, width, diameter, diameter * 0.5f);
            // Scaled about its box's middle: its ink's offset and the capitals' lift scale with it.
            const Vec2f& rScale = rCap.mLabelScales[i];
            rCap.mLabels[i]->mScale = rScale;
            rCap.mLabels[i]->mTranslate = Vec3f(x + width * 0.5f - rCap.mInkX[i] * rScale.x,
                                                           y - (cCapLift * rCap.mSize * 0.85f + rCap.mInkY[i]) * rScale.y, 0.0f);
            x += width + cBadgeGap;
        }
    }

    // The host's view kept by the game: its strings, page names and rows are copies in this view's own storage, valid
    // until the next copyFrom.
    struct ShownView : NebulaSettingsView {
        MR::GrowBuffer< char > mTextStore;
        MR::GrowBuffer< NebulaSettingsRow > mRowStore;
        MR::GrowBuffer< const char* > mPageStore;

        // Closed and empty, holding no storage yet: made with the screen.
        void reset() {
            memset(static_cast< void* >(static_cast< NebulaSettingsView* >(this)), 0, sizeof(NebulaSettingsView));
            mTextStore.mItems = nullptr;
            mTextStore.mRoom = 0;
            mRowStore.mItems = nullptr;
            mRowStore.mRoom = 0;
            mPageStore.mItems = nullptr;
            mPageStore.mRoom = 0;
        }

        void copyFrom(const NebulaSettingsView& rFrom);
    };

    // The characters a string takes with its terminator; a missing string is empty.
    u32 textUnits(const char* pText) {
        return pText != nullptr ? static_cast< u32 >(strlen(pText)) + 1 : 1;
    }

    // `pText` (empty when missing) at *ppAt, which moves past it; the copy.
    const char* putText(char** ppAt, const char* pText) {
        char* const pStart = *ppAt;
        const u32 units = textUnits(pText);
        if (pText != nullptr) {
            memcpy(pStart, pText, units);
        } else {
            pStart[0] = '\0';
        }
        *ppAt += units;
        return pStart;
    }

    void ShownView::copyFrom(const NebulaSettingsView& rFrom) {
        const u32 pageCount = rFrom.pages != nullptr ? rFrom.page_count : 0;
        const u32 rowCount = rFrom.rows != nullptr ? rFrom.total_rows : 0;
        u32 units = textUnits(rFrom.help) + textUnits(rFrom.status_text) + textUnits(rFrom.long_text);
        for (u32 i = 0; i < pageCount; i++) {
            units += textUnits(rFrom.pages[i]);
        }
        for (u32 i = 0; i < rowCount; i++) {
            units += textUnits(rFrom.rows[i].label) + textUnits(rFrom.rows[i].value);
        }
        char* pAt = mTextStore.room(units);
        NebulaSettingsRow* const pRows = mRowStore.room(rowCount);
        const char** const pPages = mPageStore.room(pageCount);
        static_cast< NebulaSettingsView& >(*this) = rFrom;
        page_count = pageCount;
        total_rows = rowCount;
        pages = pPages;
        rows = pRows;
        for (u32 i = 0; i < pageCount; i++) {
            pPages[i] = putText(&pAt, rFrom.pages[i]);
        }
        for (u32 i = 0; i < rowCount; i++) {
            pRows[i].label = putText(&pAt, rFrom.rows[i].label);
            pRows[i].value = putText(&pAt, rFrom.rows[i].value);
            pRows[i].flags = rFrom.rows[i].flags;
            pRows[i].slider_steps = rFrom.rows[i].slider_steps;
            pRows[i].slider_at = rFrom.rows[i].slider_at;
        }
        help = putText(&pAt, rFrom.help);
        status_text = putText(&pAt, rFrom.status_text);
        long_text = putText(&pAt, rFrom.long_text);
    }

    // Two rows of the host's view that look alike.
    bool isSameRow(const NebulaSettingsRow& rA, const NebulaSettingsRow& rB) {
        return rA.flags == rB.flags && rA.slider_steps == rB.slider_steps && rA.slider_at == rB.slider_at &&
               MR::isEqualString(rA.label, rB.label) && MR::isEqualString(rA.value, rB.value);
    }

    // The page's row `row` (counted from its first), or null past its last.
    const NebulaSettingsRow* rowAt(const NebulaSettingsView& rView, u32 row) {
        return row < rView.total_rows ? &rView.rows[row] : nullptr;
    }

    bool hasButtons(const NebulaSettingsRow* pRow) {
        return pRow != nullptr && (pRow->flags & NEBULA_SETTINGS_ROW_BUTTONS) != 0;
    }

    // A setting along a range: a slider.
    bool hasSlider(const NebulaSettingsRow& rRow) {
        return (rRow.flags & (NEBULA_SETTINGS_ROW_CHANGES | NEBULA_SETTINGS_ROW_SLIDER)) ==
                   (NEBULA_SETTINGS_ROW_CHANGES | NEBULA_SETTINGS_ROW_SLIDER) &&
               rRow.slider_steps > 0;
    }

    // A setting with a list of values: its arrows step through them (a binding waits for an input instead).
    bool hasArrows(const NebulaSettingsRow& rRow) {
        return (rRow.flags & (NEBULA_SETTINGS_ROW_HEADING | NEBULA_SETTINGS_ROW_CHANGES | NEBULA_SETTINGS_ROW_BUTTONS |
                              NEBULA_SETTINGS_ROW_BINDING | NEBULA_SETTINGS_ROW_MOD_OFF | NEBULA_SETTINGS_ROW_MOD_ON)) ==
                   NEBULA_SETTINGS_ROW_CHANGES &&
               !hasSlider(rRow);
    }

    // The MODS page's list a row is in (a mod, or the list's title): 0 the mods off, 1 the mods on; -1 none.
    s32 modList(const NebulaSettingsRow& rRow) {
        return (rRow.flags & NEBULA_SETTINGS_ROW_MOD_OFF) != 0 ? 0 : (rRow.flags & NEBULA_SETTINGS_ROW_MOD_ON) != 0 ? 1 : -1;
    }

    // The MODS page's two lists show.
    bool hasModLists(const NebulaSettingsView& rView) {
        for (u32 i = 0; i < rView.total_rows; i++) {
            if (modList(rView.rows[i]) >= 0) {
                return true;
            }
        }
        return false;
    }

    // What the first hint is for on the selected row: changing it, pressing its button, picking a mod up, putting it
    // down.
    enum { HintChange, HintPress, HintPickUp, HintPutDown };
    s32 firstHint(const NebulaSettingsView& rView) {
        const NebulaSettingsRow* pRow = rowAt(rView, rView.selected);
        if (pRow == nullptr) {
            return HintChange;
        }
        if (hasButtons(pRow)) {
            return HintPress;
        }
        if (modList(*pRow) >= 0) {
            return (pRow->flags & NEBULA_SETTINGS_ROW_HELD) != 0 ? HintPutDown : HintPickUp;
        }
        return HintChange;
    }

    bool isInside(f32 x, f32 y, f32 cx, f32 cy, f32 w, f32 h) {
        return x >= cx - w * 0.5f && x <= cx + w * 0.5f && y >= cy - h * 0.5f && y <= cy + h * 0.5f;
    }

    // A button `w` x `h` centred on (cx, cy) under the pointer: within `gap` / 2 of its sides (half the room to the
    // next button) and cReach above and below.
    bool isOnButton(f32 x, f32 y, f32 cx, f32 cy, f32 w, f32 h, f32 gap) {
        return isInside(x, y, cx, cy, w + gap, h + cReach * 2.0f);
    }
};  // namespace

// The menu's screen: the dimmed game behind the quit question's white window, the pages as the game's buttons, the
// rows with the selected one's highlight, the line under the page with the hints and the pointer's Apply and Back, and
// a question over it all when the host asks one. GameSystem draws it over the scene, which waits while it is shown.
// The HOME Menu is the same screen without the page: its window is the questions'.
class NebulaSettingsScreen : public LayoutActor {
public:
    NebulaSettingsScreen();

    virtual void init(const TableIter& rIter);

    void step(const NebulaSettingsView& rView, s32 homeView);
    void drawMenu() const;
    bool isBuilt() const { return mBuilt; }
    bool isShown() const { return mOpen > 0.0f; }
    bool isOpenFully() const { return mOpen >= 1.0f && !mClosing; }
    bool isHome() const { return mHome; }

private:
    bool build();
    bool makeButton(Pane* pParent, Button* pButton, const char* pName);
    bool addRowChip();
    bool addModButtons(s32 count);
    bool addTab();
    bool addRow();
    void ensureTabs(s32 count);
    void ensureRows(s32 count);
    void arrange();
    void fillTexts(const NebulaSettingsView& rView, const NebulaSettingsView* pShown);
    void fillButton(Button& rButton, const char* pWordId);
    void setQuestion(s32 question);
    void placeJoyCon(f32 top);
    void takeModPicture(const NebulaSettingsView& rView);
    void placeModPicture(bool shown, f32 width, f32 top, f32 bottom);
    void paintJoyCon(u32 budgetMicros, f32* pPixelsX, f32* pPixelsY) const;
    void fillHome(s32 homeView);
    void readPointer(const NebulaSettingsView& rView);
    void readHomePointer(f32 x, f32 y);
    void clickRow(const NebulaSettingsView& rView, s32 row);
    void dragSlider(const NebulaSettingsView& rView, f32 x);
    void edgeScroll(s32 list, f32 y);
    RowChip& useRowChip(s32 index, s32 row, s32 button, u32 version, const char16_t* pWord);
    void placeChip(Chip& rChip, f32 x, f32 y, f32 width, f32 height, bool focused);
    void placeRowBox(RowChip& rPooled, f32 x, f32 y, f32 width, f32 height, f32 radius, bool focused, bool off);
    void placeButton(Button& rButton, f32 x, f32 y, f32 width);
    void placeRowButtons(const NebulaSettingsView& rView);
    void placeRows(const NebulaSettingsView& rView);
    void placeMods(const NebulaSettingsView& rView);
    void showMods(bool shown);
    void readModsPointer(const NebulaSettingsView& rView, f32 x, f32 y);
    void fitStatus(f32 room);
    void place(const NebulaSettingsView& rView);
    void placeHome();

    NebulaPaneKit mModalKit;   // SysInfoWindow: the quit question's dimmer, window and shadow
    NebulaPaneKit mTextKit;    // InformationWindow: the game's message font
    NebulaPaneKit mWindowKit;  // SysInfoWindowMini: the window with a ring, repainted for the highlight
    NebulaPaneKit mButtonKit;  // BackButton: the game's button
    Kits mKits;
    bool mBuilt;

    Pane* mRoot;
    Pane* mDim;
    Pane* mMenu;
    Pane* mPage;  // all but the question: the HOME Menu has none of it
    Window* mShadow;
    Window* mPanel;
    TextBox* mTitle;
    MR::GrowBuffer< Chip > mTabs;  // the page buttons, made as the pages need them
    MR::GrowBuffer< f32 > mTabWordWidths;
    s32 mTabsMade;
    Pane* mList;
    Window* mHighlight;
    MR::GrowBuffer< Row > mRows;  // one per row of the page, made as the pages need them
    s32 mRowsMade;
    MR::GrowBuffer< RowChip > mRowChips;  // the rows' buttons, made as the rows in sight need them
    s32 mRowChipCount;
    // The MODS page: its two lists (the mods off, the mods on) with their titles and what an empty one says, a line
    // where a dragged mod would land, and the launcher's buttons under them.
    Window* mModPanels[2];
    TextBox* mModTitles[2];
    TextBox* mModEmpty[2];
    Window* mDropLine;
    MR::GrowBuffer< Chip > mModButtons;  // made as the launcher's row has buttons
    s32 mModButtonsMade;
    u32 mModButtonsVersion;  // the words they show (Row::mButtonsVersion)
    s32 mModButtonCount;
    s32 mModButtonsRow;  // the buttons' row; -1 none
    MR::GrowBuffer< s32 > mItemList;   // each row's list (-1 none) and place in it, room for every row
    MR::GrowBuffer< s32 > mItemPlace;
    s32 mModCounts[2];
    f32 mModScroll[2];
    f32 mModLeft[2];  // each list's left edge
    f32 mModWidth;
    f32 mModItemsTop;
    f32 mModItemsBottom;
    f32 mModButtonsY;
    s32 mHoverModButton;
    s32 mDragRow;    // the mod held down by the pointer; -1 none
    bool mDragging;  // moved far enough to be dragged
    f32 mDragFromX;
    f32 mDragFromY;
    u64 mEdgeScrollNs;    // the last edge scroll under a dragged mod (NebulaHost::clockNs; 0 none)
    // A mod let go into another place: drawn there until the view the host sends has it there (the frame of the drop
    // still has the order before it).
    MR::GrowBuffer< char > mSettleLabel;
    s32 mSettleList;  // -1 none
    s32 mSettlePlace;
    s32 mSettleFrames;
    s32 mModSwipeList;    // the list the pointer scrolls (-1 none) and its scroll when it was held down
    f32 mModSwipeFrom;
    bool mModFree[2];     // the list stays where a finger left it, until the keys move the selection
    f32 mModFreeScroll[2];
    s32 mDropList;  // where it would land: the list (-1 none) and the place in it
    s32 mDropPlace;
    f32 mClipTop;  // the list's rows are cut to these
    f32 mClipBottom;
    Window* mTrack;
    Window* mThumb;
    TextBox* mStatus;
    MR::Text16 mStatusText;     // the line under the page, whole
    f32 mStatusWidth;          // as shown
    f32 mStatusRoom;           // the room it was fitted to
    Hint mHints[cHintsMax];
    Button mFooter[FooterNum];
    Pane* mQuestion;
    Pane* mQuestionDim;
    Window* mQuestionShadow;
    Window* mQuestionPanel;
    TextBox* mQuestionText;
    Window* mProgressTrack;  // a download's bar
    Window* mProgressFill;
    Button mChoices[ChoiceNum];
    s32 mChoiceCount;
    Pane* mJoyCon;  // the Pair Joy-Con picture
    JoyConHalf mHalves[2];
    bool mJoyConPaired;  // the pair's chime played
    s32 mJoyConBits;     // the Pair Joy-Con screen's status_number this frame (NEBULA_JOYCON_*)
    f32 mQuestionInk;    // the question text's height from its first capitals' tops to its last baseline
    // The ABOUT window's picture (a mod's icon or one of its pictures): its pane, made when first shown, and the game's
    // copy of the host's pixels in a texture record of its own (cModPictureSide a side at most, taken once).
    Picture* mModPicture;
    Ui::TexMap mModPictureMap;
    u8* mModPictureRecord;
    const u8* mModPictureFrom;  // the host's pixels last copied (the same pointer: the same picture)
    u32 mModPictureWidth;
    u32 mModPictureHeight;
    bool mModPictureReady;  // copied: shown on an ABOUT page
    bool mModPictureNew;    // copied since the pane last took it

    NebulaSettingsView mRead;   // the host's view this step
    ShownView mShown;           // the host's view as drawn, copied
    f32 mOpen;                  // 0 away, 1 in place
    bool mClosing;
    s32 mQuestionShown;  // cQuestions' index; -1 none
    bool mHome;          // the HOME Menu, not the settings
    s32 mHomeView;       // the host's NEBULA_HOME_MENU_VIEW as drawn; -1 none
    Button* mFocusButton;  // the HOME Menu's selected answer
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
    // The list held down by the pointer: where, what was under it (an arrow, a slider), the scroll then, and whether it
    // drags the list (let go where it was pressed, what is under it is clicked).
    bool mPressing;
    s32 mPressRow;
    s32 mPressArrow;
    bool mPressSlider;
    f32 mPressX;
    f32 mPressY;
    f32 mPressScroll;
    bool mScrollDrag;
    s32 mSwipeFrames;  // a finger's swipe just moved the list: the row under it is not chosen yet
    s32 mDraggedAt;  // the selection when the list was last let go; it stays where it was dragged until another is chosen
    s32 mHoverArrow;    // the hovered row's arrow under the pointer: -1 back, 1 on; 0 none
    bool mHoverSlider;  // the hovered row's slider under the pointer
    s32 mHeldArrow;     // an arrow held down, stepping again while it is (-1, 1; 0 none)
    s32 mHeldFrames;
    s32 mSliderRow;  // the slider held down, following the pointer; -1 none
    s32 mHoverRowButton;  // the hovered row's button under the pointer; -1 none
    Button* mHoverButton;

    // The layout, again whenever the screen's shape changes.
    f32 mScaleX;
    f32 mZoom = 1.0f;  // the menu's size (arrange): every measure below is in its own units
    f32 mScreenWidth = 0.0f;
    f32 mScreenHeight = 0.0f;
    s32 mTextSizeChosen = 0;
    s32 mWindowDp = 0;  // the window's width in density-independent units at the last arrange: a move to a monitor of another density re-arranges
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
    : LayoutActor("NebulaSettingsMenu", true), mBuilt(false), mRoot(nullptr), mDim(nullptr), mMenu(nullptr), mPage(nullptr), mShadow(nullptr),
      mPanel(nullptr), mTitle(nullptr), mList(nullptr), mHighlight(nullptr), mTrack(nullptr), mThumb(nullptr), mStatus(nullptr),
      mStatusWidth(0.0f), mStatusRoom(-1.0f), mQuestion(nullptr), mQuestionDim(nullptr), mQuestionShadow(nullptr), mQuestionPanel(nullptr),
      mQuestionText(nullptr), mProgressTrack(nullptr), mProgressFill(nullptr), mChoiceCount(0), mJoyCon(nullptr), mJoyConPaired(false), mJoyConBits(0), mQuestionInk(0.0f), mModPicture(nullptr), mModPictureRecord(nullptr), mModPictureFrom(nullptr), mModPictureWidth(0), mModPictureHeight(0), mModPictureReady(false), mModPictureNew(false), mOpen(0.0f), mClosing(false), mQuestionShown(-1), mHome(false), mHomeView(-1),
      mFocusButton(nullptr), mFrame(0), mScroll(0.0f),
      mScrollTarget(0.0f), mHighlightY(0.0f), mPointerMode(false), mPointerX(0.0f), mPointerY(0.0f), mPointerRow(-1), mHoverRow(-1),
      mHoverTab(-1), mPressing(false), mPressRow(-1), mPressArrow(0), mPressSlider(false), mPressX(0.0f), mPressY(0.0f), mPressScroll(0.0f), mScrollDrag(false), mSwipeFrames(0), mDraggedAt(-1), mHoverArrow(0),
      mHoverSlider(false), mHeldArrow(0), mHeldFrames(0), mSliderRow(-1), mHoverRowButton(-1), mHoverButton(nullptr), mScaleX(1.0f), mWidth(0.0f), mHeight(0.0f), mPanelWidth(0.0f),
      mPanelHeight(0.0f), mLeft(0.0f), mRight(0.0f), mTitleY(0.0f), mTabY(0.0f), mListTop(0.0f), mListBottom(0.0f), mStatusY(0.0f),
      mHintsY(0.0f), mFooterY(0.0f), mValueRight(0.0f) {
    memset(&mTabs, 0, sizeof(mTabs));
    mTabsMade = 0;
    memset(mHalves, 0, sizeof(mHalves));
    memset(&mTabWordWidths, 0, sizeof(mTabWordWidths));
    memset(&mRows, 0, sizeof(mRows));
    mRowsMade = 0;
    memset(&mRowChips, 0, sizeof(mRowChips));
    mRowChipCount = 0;
    memset(mModPanels, 0, sizeof(mModPanels));
    memset(mModTitles, 0, sizeof(mModTitles));
    memset(mModEmpty, 0, sizeof(mModEmpty));
    memset(&mModButtons, 0, sizeof(mModButtons));
    mModButtonsMade = 0;
    memset(&mItemList, 0, sizeof(mItemList));
    memset(&mItemPlace, 0, sizeof(mItemPlace));
    memset(mModCounts, 0, sizeof(mModCounts));
    memset(mModScroll, 0, sizeof(mModScroll));
    memset(mModLeft, 0, sizeof(mModLeft));
    mDropLine = nullptr;
    mModButtonsVersion = ~0u;
    mModButtonCount = 0;
    mModButtonsRow = -1;
    mModWidth = 0.0f;
    mModItemsTop = 0.0f;
    mModItemsBottom = 0.0f;
    mModButtonsY = 0.0f;
    mHoverModButton = -1;
    mDragRow = -1;
    mDragging = false;
    mDragFromX = 0.0f;
    mDragFromY = 0.0f;
    mEdgeScrollNs = 0u;
    memset(&mSettleLabel, 0, sizeof(mSettleLabel));
    mSettleList = -1;
    mSettlePlace = 0;
    mSettleFrames = 0;
    mModSwipeList = -1;
    mModSwipeFrom = 0.0f;
    mModFree[0] = mModFree[1] = false;
    mModFreeScroll[0] = mModFreeScroll[1] = 0.0f;
    mDropList = -1;
    mDropPlace = 0;
    mClipTop = 0.0f;
    mClipBottom = 0.0f;
    memset(mHints, 0, sizeof(mHints));
    memset(mFooter, 0, sizeof(mFooter));
    memset(mChoices, 0, sizeof(mChoices));
    mShown.reset();
    mKits.mText = &mTextKit;
    mKits.mWindow = &mWindowKit;
    mKits.mButton = &mButtonKit;
}

void NebulaSettingsScreen::init(const TableIter& /*rIter*/) {
    initLayoutManager("SysInfoWindow", 0);
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaSettingsMenu";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));
    mBuilt = build();
    if (!mBuilt) {
        nebula_log("NebulaSettingsMenu: a layout or pane is missing\n");
    }
    kill();
}

bool NebulaSettingsScreen::makeButton(Pane* pParent, Button* pButton, const char* pName) {
    if (!makeChip(mKits, pParent, &pButton->mChip, pName, false, cButtonSize, cButtonText)) {
        return false;
    }
    return makeKeyCap(mKits, pButton->mChip.mRoot, &pButton->mKey, pName, cButtonSize);
}

// The next button of the pool the rows' own buttons are taken from, unused and hidden.
bool NebulaSettingsScreen::addRowChip() {
    const s32 i = mRowChipCount;
    RowChip& rPooled = mRowChips.room(static_cast< u32 >(i) + 1)[i];
    memset(static_cast< void* >(&rPooled), 0, sizeof(rPooled));
    MR::FormattedName name("NsRowBtn%d", static_cast< int >(i));
    if (!makeChip(mKits, mList, &rPooled.mChip, name.c_str(), false, cRowButtonSize, cButtonText)) {
        return false;
    }
    // The box behind the word.
    name.format("NsRowBtnE%d", static_cast< int >(i));
    rPooled.mEdge = makeWindow(mKits, rPooled.mChip.mRoot, name.c_str());
    name.format("NsRowBtnF%d", static_cast< int >(i));
    rPooled.mFace = makeWindow(mKits, rPooled.mChip.mRoot, name.c_str());
    if (rPooled.mEdge == nullptr || rPooled.mFace == nullptr) {
        return false;
    }
    paint(rPooled.mEdge, cRowButtonEdge, cRowButtonEdge);
    rPooled.mChip.mRoot->RemoveChild(rPooled.mChip.mText);
    rPooled.mChip.mRoot->AppendChild(rPooled.mChip.mText);
    rPooled.mChip.mArt->SetVisible(false);
    rPooled.mRow = -1;
    rPooled.mChip.mRoot->SetVisible(false);
    mRowChipCount++;
    return true;
}

// The launcher's buttons made up to `count`.
bool NebulaSettingsScreen::addModButtons(s32 count) {
    while (mModButtonsMade < count) {
        const s32 i = mModButtonsMade;
        Chip& rChip = mModButtons.room(static_cast< u32 >(i) + 1)[i];
        memset(static_cast< void* >(&rChip), 0, sizeof(rChip));
        const MR::FormattedName name("NsModBtn%d", static_cast< int >(i));
        if (!makeChip(mKits, mPage, &rChip, name.c_str(), false, cButtonSize, cButtonText)) {
            return false;
        }
        mModButtonsMade++;
    }
    return true;
}

// The next page button, hidden until a page needs it.
bool NebulaSettingsScreen::addTab() {
    const s32 i = mTabsMade;
    Chip& rTab = mTabs.room(static_cast< u32 >(i) + 1)[i];
    memset(static_cast< void* >(&rTab), 0, sizeof(rTab));
    mTabWordWidths.room(static_cast< u32 >(i) + 1)[i] = 0.0f;
    char name[16];
    nebula_snprintf(name, sizeof(name), "NsTab%d", static_cast< int >(i));
    if (!makeChip(mKits, mPage, &rTab, name, false, cTabSize, cButtonText)) {
        return false;
    }
    setInfluencedAlphaRecursive(rTab.mRoot, 0);
    mTabsMade++;
    return true;
}

// The next row's panes, hidden until a page shows a row there.
bool NebulaSettingsScreen::addRow() {
    const s32 i = mRowsMade;
    char name[16];
    Row& rRow = mRows.room(static_cast< u32 >(i) + 1)[i];
    memset(static_cast< void* >(&rRow), 0, sizeof(rRow));
    nebula_snprintf(name, sizeof(name), "NsRow%d", static_cast< int >(i));
    rRow.mRoot = makeGroup(mList, name);
    if (rRow.mRoot == nullptr) {
        return false;
    }
    nebula_snprintf(name, sizeof(name), "NsFrame%d", static_cast< int >(i));
    rRow.mFrame = makeWindow(mKits, rRow.mRoot, name);
    nebula_snprintf(name, sizeof(name), "NsRule%d", static_cast< int >(i));
    rRow.mRule = makeWindow(mKits, rRow.mRoot, name);
    nebula_snprintf(name, sizeof(name), "NsLabel%d", static_cast< int >(i));
    rRow.mLabel = makeText(mKits, rRow.mRoot, name, 96);
    nebula_snprintf(name, sizeof(name), "NsValue%d", static_cast< int >(i));
    rRow.mValue = makeText(mKits, rRow.mRoot, name, 128);
    nebula_snprintf(name, sizeof(name), "NsTrack%d", static_cast< int >(i));
    rRow.mSliderTrack = makeWindow(mKits, rRow.mRoot, name);
    nebula_snprintf(name, sizeof(name), "NsFill%d", static_cast< int >(i));
    rRow.mSliderFill = makeWindow(mKits, rRow.mRoot, name);
    if (rRow.mFrame == nullptr || rRow.mRule == nullptr || rRow.mLabel == nullptr || rRow.mValue == nullptr ||
        rRow.mSliderTrack == nullptr || rRow.mSliderFill == nullptr) {
        return false;
    }
    paint(rRow.mRule, cRule, cRule);
    paint(rRow.mSliderTrack, cSliderTrack, cSliderTrack);
    paint(rRow.mSliderFill, cSliderFill, cSliderFill);
    setInfluencedAlphaRecursive(rRow.mRoot, 0);
    mRowsMade++;
    return true;
}

// Page buttons for `count` pages. A button that cannot be made leaves the menu unbuilt.
void NebulaSettingsScreen::ensureTabs(s32 count) {
    while (mTabsMade < count) {
        if (!addTab()) {
            nebula_log("NebulaSettingsMenu: a page button could not be made\n");
            mBuilt = false;
            return;
        }
    }
}

// Room for `count` rows: their panes, and each row's list and place. Panes made after the first batch go in front of
// the row buttons and the drop line, as the first ones are. A row that cannot be made leaves the menu unbuilt.
void NebulaSettingsScreen::ensureRows(s32 count) {
    const u32 want = count > 0 ? static_cast< u32 >(count) : 0;
    const u32 hadList = mItemList.mRoom;
    s32* const pList = mItemList.room(want);
    for (u32 i = hadList; i < mItemList.mRoom; i++) {
        pList[i] = 0;
    }
    const u32 hadPlace = mItemPlace.mRoom;
    s32* const pPlace = mItemPlace.room(want);
    for (u32 i = hadPlace; i < mItemPlace.mRoom; i++) {
        pPlace[i] = 0;
    }
    if (mRowsMade >= count) {
        return;
    }
    while (mRowsMade < count) {
        if (!addRow()) {
            nebula_log("NebulaSettingsMenu: a row could not be made\n");
            mBuilt = false;
            return;
        }
    }
    if (mDropLine != nullptr) {
        for (s32 k = 0; k < mRowChipCount && k < cRowChipsFirst; k++) {
            mList->RemoveChild(mRowChips[k].mChip.mRoot);
            mList->AppendChild(mRowChips[k].mChip.mRoot);
        }
        mList->RemoveChild(mDropLine);
        mList->AppendChild(mDropLine);
        for (s32 k = cRowChipsFirst; k < mRowChipCount; k++) {
            mList->RemoveChild(mRowChips[k].mChip.mRoot);
            mList->AppendChild(mRowChips[k].mChip.mRoot);
        }
    }
}

bool NebulaSettingsScreen::build() {
    LayoutManager* pManager = getLayoutManager();
    Pane* pRoot = pManager->mLayout->mpRootPane;
    Ui::Layout* const pInto = pManager->mLayout;
    if (pRoot == nullptr || !mModalKit.init(pManager->mLayoutHolder, "SysInfoWindow", pInto) ||
        !mTextKit.init("InformationWindow", pInto) || !mWindowKit.init("SysInfoWindowMini", pInto) ||
        !mButtonKit.init("BackButton", pInto)) {
        return false;
    }
    // The question window's own panes stay hidden; the menu is built beside them.
    for (Ui::Pane* pChild : pRoot->GetChildList()) {
        pChild->SetVisible(false);
    }
    mRoot = makeGroup(pRoot, "NsRoot");
    mDim = mModalKit.make("BG", "NsDim");
    if (mRoot == nullptr || mDim == nullptr) {
        return false;
    }
    mDim->mBasePosition = 4;
    mRoot->AppendChild(mDim);
    mMenu = makeGroup(mRoot, "NsMenu");
    mPage = mMenu != nullptr ? makeGroup(mMenu, "NsPage") : nullptr;
    mShadow = mModalKit.makeWindow("ShaInfo", "NsShadow");
    mPanel = mModalKit.makeWindow("WinInfo", "NsPanel");
    if (mPage == nullptr || mShadow == nullptr || mPanel == nullptr) {
        return false;
    }
    mShadow->mBasePosition = 4;
    mPanel->mBasePosition = 4;
    mPage->AppendChild(mShadow);
    mPage->AppendChild(mPanel);
    mTitle = makeText(mKits, mPage, "NsTitle", 64);
    if (mTitle == nullptr) {
        return false;
    }
    setTextStyle(mTitle, cTitleSize, cText, cText, 3);
    while (mTabsMade < cTabsFirst) {
        if (!addTab()) {
            return false;
        }
    }

    for (s32 i = 0; i < 2; i++) {
        char name[16];
        nebula_snprintf(name, sizeof(name), "NsModPanel%d", static_cast< int >(i));
        mModPanels[i] = makeWindow(mKits, mPage, name);
        nebula_snprintf(name, sizeof(name), "NsModTitle%d", static_cast< int >(i));
        mModTitles[i] = makeText(mKits, mPage, name, 64);
        nebula_snprintf(name, sizeof(name), "NsModEmpty%d", static_cast< int >(i));
        mModEmpty[i] = makeText(mKits, mPage, name, 96);
        if (mModPanels[i] == nullptr || mModTitles[i] == nullptr || mModEmpty[i] == nullptr) {
            return false;
        }
        paint(mModPanels[i], 0xFFFFFF70, cRule);
        setTextStyle(mModTitles[i], cHeadingSize, cAccent, cAccent, 3);
        setTextStyle(mModEmpty[i], cFooterSize, cSubText, cSubText, 4);
    }
    mList = makeGroup(mPage, "NsList");
    mHighlight = mList != nullptr ? makeWindow(mKits, mList, "NsHighlight") : nullptr;
    if (mHighlight == nullptr) {
        return false;
    }
    paint(mHighlight, cHighlightFill, cHighlightRing);
    while (mRowsMade < cRowsFirst) {
        if (!addRow()) {
            return false;
        }
    }
    while (mRowChipCount < cRowChipsFirst) {
        if (!addRowChip()) {
            return false;
        }
    }
    mDropLine = makeWindow(mKits, mList, "NsDropLine");
    if (mDropLine == nullptr) {
        return false;
    }
    paint(mDropLine, cAccent, cAccent);
    if (!addModButtons(cRowButtonsFirst)) {
        return false;
    }
    showMods(false);
    mTrack = makeWindow(mKits, mPage, "NsTrack");
    mThumb = makeWindow(mKits, mPage, "NsThumb");
    mStatus = makeText(mKits, mPage, "NsStatus", 256);
    if (mTrack == nullptr || mThumb == nullptr || mStatus == nullptr) {
        return false;
    }
    paint(mTrack, cTrack, cTrack);
    paint(mThumb, cThumb, cThumb);
    setTextStyle(mStatus, cFooterSize, cSubText, cSubText, 3);
    for (s32 i = 0; i < cHintsMax; i++) {
        char name[16];
        nebula_snprintf(name, sizeof(name), "NsHint%d", static_cast< int >(i));
        if (!makeKeyCap(mKits, mPage, &mHints[i].mKey, name, cFooterSize)) {
            return false;
        }
        nebula_snprintf(name, sizeof(name), "NsHintW%d", static_cast< int >(i));
        mHints[i].mWord = makeText(mKits, mPage, name, 48);
        if (mHints[i].mWord == nullptr) {
            return false;
        }
        setTextStyle(mHints[i].mWord, cFooterSize, cSubText, cSubText, 3);
    }
    if (!makeButton(mPage, &mFooter[FooterApply], "NsApply") || !makeButton(mPage, &mFooter[FooterBack], "NsBack")) {
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
    mQuestionText = makeText(mKits, mQuestion, "NsQText", 1536);
    if (mQuestionText == nullptr) {
        return false;
    }
    setTextStyle(mQuestionText, cTextSize + 1.0f, cText, cText, 4);
    mProgressTrack = makeWindow(mKits, mQuestion, "NsQBarTrack");
    mProgressFill = makeWindow(mKits, mQuestion, "NsQBarFill");
    if (mProgressTrack == nullptr || mProgressFill == nullptr) {
        return false;
    }
    paint(mProgressTrack, cSliderTrack, cSliderTrack);
    paint(mProgressFill, cSliderFill, cSliderFill);
    for (s32 i = 0; i < ChoiceNum; i++) {
        char name[16];
        nebula_snprintf(name, sizeof(name), "NsChoice%d", static_cast< int >(i));
        if (!makeButton(mQuestion, &mChoices[i], name)) {
            return false;
        }
    }
    mJoyCon = makeGroup(mQuestion, "NsJoyCon");
    if (mJoyCon == nullptr) {
        return false;
    }
    for (s32 half = 0; half < 2; half++) {
        char name[16];
        nebula_snprintf(name, sizeof(name), "NsJc%dL", static_cast< int >(half));
        mHalves[half].mLetter = makeText(mKits, mJoyCon, name, 4);
        if (mHalves[half].mLetter == nullptr) {
            return false;
        }
        setTextStyle(mHalves[half].mLetter, cTextSize, cText, cText, 4);
        setText(mHalves[half].mLetter, half == 0 ? u"L" : u"R");
        mHalves[half].mMap = nullptr;
        mHalves[half].mPresses = -1;
    }
    mJoyCon->SetVisible(false);
    mQuestion->SetVisible(false);
    setInfluencedAlphaRecursive(mRoot, 0);
    return true;
}

// Where everything goes for the screen's shape: the window as large as the screen allows, at most cMaxWidth wide.
void NebulaSettingsScreen::arrange() {
    readScreen(&mScaleX, &mWidth, &mHeight);
    mTextSizeChosen = static_cast< s32 >(NebulaHost::option(NEBULA_OPTION_MENU_TEXT_SIZE));
    mWindowDp = static_cast< s32 >(NebulaHost::option(NEBULA_OPTION_WINDOW_WIDTH_DP));
    mScreenWidth = mWidth;
    mScreenHeight = mHeight;
    // The menu's size: the player's, or large enough that its text is readable on this screen.
    const s32 chosen = static_cast< s32 >(NebulaHost::option(NEBULA_OPTION_MENU_TEXT_SIZE));
    const s32 wide = static_cast< s32 >(NebulaHost::option(NEBULA_OPTION_WINDOW_WIDTH_DP));
    mZoom = 1.0f;
    if (chosen != 0) {
        mZoom = static_cast< f32 >(chosen) / 100.0f;
    } else if (wide > 0) {
        const f32 textDp = cTextSize * static_cast< f32 >(wide) / mWidth;
        mZoom = textDp < cReadableTextDp ? cReadableTextDp / textDp : 1.0f;
        mZoom = mZoom > cZoomMax ? cZoomMax : mZoom;
    }
    // What is not rows, in the menu's units: the margins, the window's inside, the tabs and the footer. The title goes
    // first on a short screen; whatever size is asked for, rows stay: three for the size the screen chose, two for the player's.
    const f32 titleRoom = cTitleSize + 14.0f;
    const f32 around = cMargin * 2.0f + cPad * 1.8f + cTabHeight + 8.0f + cButtonHeight * 0.5f + cFooterLine * 0.5f + 3.0f + cFooterSize * 0.5f + 8.0f;
    const f32 largest = mHeight / (around + cRowHeight * (chosen != 0 ? 2.0f : 3.0f) + 1.0f);
    const f32 widest = mWidth / (cMinWindowWidth + cMargin * 2.0f);
    mZoom = mZoom > largest ? largest : mZoom;
    mZoom = mZoom > widest ? widest : mZoom;
    const bool titled = mHeight / mZoom >= around + titleRoom + cRowHeight * 3.0f;
    mTitle->SetVisible(titled);
    mWidth /= mZoom;
    mHeight /= mZoom;
    mRoot->mScale = Vec2f(mScaleX * mZoom, mZoom);
    mPanelWidth = mWidth - cMargin * 2.0f;
    mPanelWidth = mPanelWidth > cMaxWidth ? cMaxWidth : mPanelWidth;
    mPanelHeight = mHeight - cMargin * 2.0f;
    const f32 top = mPanelHeight * 0.5f - cPad;
    mLeft = -mPanelWidth * 0.5f + cPad;
    mRight = mPanelWidth * 0.5f - cPad;
    mTitleY = top - cTitleSize * 0.5f;
    mTabY = top - (titled ? titleRoom : 0.0f) - cTabHeight * 0.5f;
    mListTop = mTabY - cTabHeight * 0.5f - 8.0f;
    mFooterY = -mPanelHeight * 0.5f + cPad * 0.8f + cButtonHeight * 0.5f;
    mHintsY = mFooterY - cFooterLine * 0.5f + 3.0f;
    mStatusY = mHintsY + cFooterLine;
    // The list holds whole rows: at rest none is cut at its edges.
    const f32 listRoom = mListTop - (mStatusY + cFooterSize * 0.5f + 8.0f);
    mListBottom = mListTop - cRowHeight * static_cast< s32 >(listRoom / cRowHeight);
    mValueRight = mRight - 16.0f;

    mDim->mSize = Ui::Size(mWidth * 1.5f, mHeight * 1.5f);
    mPanel->mSize = Ui::Size(mPanelWidth, mPanelHeight);
    mShadow->mSize = Ui::Size(mPanelWidth, mPanelHeight);
    mShadow->mTranslate = Vec3f(6.0f, -6.0f, 0.0f);
    mQuestionDim->mSize = Ui::Size(mPanelWidth, mPanelHeight);
    if (!mHome) {
        mQuestionPanel->mSize = Ui::Size(cQuestionWidth, cQuestionHeight);
        mQuestionShadow->mSize = Ui::Size(cQuestionWidth, cQuestionHeight);
    }
    mQuestionShadow->mTranslate = Vec3f(6.0f, -6.0f, 0.0f);
}

void NebulaSettingsScreen::fillButton(Button& rButton, const char* pWordId) {
    MR::Text16 text;
    f32 height = 0.0f;
    getText(text, pWordId);
    setText(rButton.mChip.mText, text.c_str());
    measure(rButton.mChip.mText, 4000.0f, &rButton.mWordWidth, &height);
}

// The texts in the played language, again for whatever changed from `pShown` (all of them when it is null): the page
// names, the rows, the line under the page, the hints and the question.
__attribute__((noinline)) void NebulaSettingsScreen::fillTexts(const NebulaSettingsView& rView, const NebulaSettingsView* pShown) {
    MR::Text16 text;
    f32 width = 0.0f;
    f32 height = 0.0f;
    if (pShown == nullptr) {
        getText(text, "SettingsTitle");
        setText(mTitle, text.c_str());
        measure(mTitle, 4000.0f, &width, &height);
    }
    ensureTabs(static_cast< s32 >(rView.page_count));
    ensureRows(static_cast< s32 >(rView.total_rows));
    if (!mBuilt) {
        return;
    }
    // The page names, when they are new.
    bool samePages = pShown != nullptr && pShown->page_count == rView.page_count;
    for (u32 i = 0; samePages && i < rView.page_count; i++) {
        samePages = MR::isEqualString(pShown->pages[i], rView.pages[i]);
    }
    if (!samePages) {
        for (s32 i = 0; i < mTabsMade; i++) {
            Chip& rTab = mTabs[i];
            rTab.mRoot->SetVisible(i < static_cast< s32 >(rView.page_count));
            if (i >= static_cast< s32 >(rView.page_count)) {
                continue;
            }
            translateLine(text, rView.pages[i], false);
            setText(rTab.mText, text.c_str());
            measure(rTab.mText, 4000.0f, &mTabWordWidths[i], &height);
        }
    }
    const bool samePage = pShown != nullptr && pShown->page == rView.page && pShown->input == rView.input;
    s32 onPlace = 0;  // the mods on, counted
    for (s32 i = 0; i < mRowsMade; i++) {
        Row& rRow = mRows[i];
        const bool modOn = i < static_cast< s32 >(rView.total_rows) && modList(rView.rows[i]) == 1 &&
                           (rView.rows[i].flags & NEBULA_SETTINGS_ROW_HEADING) == 0;
        onPlace += modOn ? 1 : 0;
        const s32 place = modOn ? onPlace : 0;
        const bool shown = i < static_cast< s32 >(rView.total_rows);
        rRow.mRoot->SetVisible(shown);
        // A row whose data is unchanged keeps its text unless its number moved (a mod above it left the list).
        if (!shown || (samePage && rRow.mOnPlace == place && i < static_cast< s32 >(pShown->total_rows) &&
                       isSameRow(rView.rows[i], pShown->rows[i]))) {
            continue;
        }
        const NebulaSettingsRow& rData = rView.rows[i];
        const bool heading = (rData.flags & NEBULA_SETTINGS_ROW_HEADING) != 0;
        rRow.mOnPlace = place;
        // A mod's own option ("  NAME") keeps its indent.
        rRow.mIndented = rData.label[0] == ' ' && rData.label[1] == ' ';
        translateLine(text, rRow.mIndented ? rData.label + 2 : rData.label, (rData.flags & NEBULA_SETTINGS_ROW_LABEL_NAME) != 0);
        if (heading && modList(rData) >= 0) {
            setText(mModTitles[modList(rData)], text.c_str());
        }
        if (modOn) {
            // Its place before its name, as the launcher's list numbers it: the name moves on, the digits take its place.
            const MR::FormattedName digits("%d   ", static_cast< int >(onPlace));
            const u32 digitsLength = static_cast< u32 >(strlen(digits.c_str()));
            const u32 nameLength = static_cast< u32 >(MR::textLength(text.c_str()));
            char16_t* const pText = text.grow(nameLength + 1, digitsLength + nameLength + 1);
            for (u32 k = nameLength + 1; k > 0; k--) {
                pText[digitsLength + k - 1] = pText[k - 1];
            }
            for (u32 k = 0; k < digitsLength; k++) {
                pText[k] = static_cast< char16_t >(digits.c_str()[k]);
            }
        }
        setText(rRow.mLabel, text.c_str());
        setTextStyle(rRow.mLabel, heading ? cHeadingSize : cTextSize, heading ? cAccent : cText, heading ? cAccent : cText, 3);
        measure(rRow.mLabel, 4000.0f, &rRow.mLabelWidth, &height);
        rRow.mRule->SetVisible(heading);
        rRow.mFrame->SetVisible(!heading);
        rRow.mValue->SetVisible(!heading && !hasButtons(&rData));
        rRow.mValueWidth = 0.0f;
        rRow.mButtonCount = 0;
        rRow.mButtonsVersion++;
        if (hasButtons(&rData)) {
            // Their words, '|' between them, each as a value in the played language ('-' before one: unavailable).
            const char* pWord = rData.value;
            while (*pWord != 0) {
                const char* pEnd = strchr(pWord, '|');
                const bool off = *pWord == '-';
                if (off) {
                    pWord++;
                }
                const s32 length = pEnd != nullptr ? static_cast< s32 >(pEnd - pWord) : static_cast< s32 >(strlen(pWord));
                const MR::FormattedName word("%.*s", static_cast< int >(length), pWord);
                MR::Text16 translated;
                translateLine(translated, word.c_str(), false);
                RowButton& rButton = addRowButton(rRow);
                rButton.mOff = off;
                const u32 units = static_cast< u32 >(MR::textLength(translated.c_str())) + 1;
                MR::copyText(rButton.mWord.room(units), translated.c_str(), units);
                pWord = pEnd != nullptr ? pEnd + 1 : pWord + length;
            }
            continue;
        }
        if (heading) {
            continue;
        }
        // A binding whose input another control shares is red: both act together.
        const u32 valueColor = (rData.flags & NEBULA_SETTINGS_ROW_SHARED) != 0    ? cWarning
                               : (rData.flags & NEBULA_SETTINGS_ROW_CHANGES) != 0 ? cText
                                                                                  : cDisabled;
        translateLine(text, rData.value, (rData.flags & NEBULA_SETTINGS_ROW_NAME) != 0);
        if ((rData.flags & NEBULA_SETTINGS_ROW_ACTIVE) != 0) {
            // Two spaces and the word for it after the value.
            MR::Text16 inUse;
            getText(inUse, "SettingsInUse");
            const u32 valueLength = static_cast< u32 >(MR::textLength(text.c_str()));
            const u32 inUseLength = static_cast< u32 >(MR::textLength(inUse.c_str()));
            char16_t* const pText = text.grow(valueLength + 1, valueLength + 2 + inUseLength + 1);
            pText[valueLength] = u' ';
            pText[valueLength + 1] = u' ';
            for (u32 k = 0; k <= inUseLength; k++) {
                pText[valueLength + 2 + k] = inUse.c_str()[k];
            }
        }
        setText(rRow.mValue, text.c_str());
        // Between its arrows, centred; else at the row's end.
        setTextStyle(rRow.mValue, cTextSize, valueColor, valueColor, hasArrows(rData) ? 4 : 5);
        measure(rRow.mValue, 4000.0f, &rRow.mValueWidth, &height);
    }

    const s32 hint = firstHint(rView);
    if (pShown != nullptr && pShown->status == rView.status && pShown->status_number == rView.status_number && pShown->help_as_is == rView.help_as_is &&
        MR::isEqualString(pShown->help, rView.help) && MR::isEqualString(pShown->status_text, rView.status_text) &&
        MR::isEqualString(pShown->long_text, rView.long_text) && pShown->page == rView.page && pShown->input == rView.input && firstHint(*pShown) == hint) {
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
    case NEBULA_SETTINGS_STATUS_DLSS_FAILED:
        pStatusId = "SettingsDlssFailed";
        statusColor = cWarning;
        break;
    case NEBULA_SETTINGS_STATUS_XESS_FAILED:
        pStatusId = "SettingsXessFailed";
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
    case NEBULA_SETTINGS_STATUS_PREPARING:
        pStatusId = "SettingsPreparing";
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
        getNumbered(mStatusText, pStatusId, rView.status_number);
    } else if (rView.status == NEBULA_SETTINGS_STATUS_HELP && rView.help_as_is != 0) {
        fromUtf8(mStatusText, 0, pStatusText);  // already in the played language (a mod's refusal)
    } else {
        translateLine(mStatusText, pStatusText, false);
    }
    setTextColor(mStatus, statusColor, statusColor);
    mStatusRoom = -1.0f;  // fitted again where it is placed

    // The hints: changing (pressing, on a row of buttons; moving a mod), the page's own key, the pages.
    const bool controls = rView.page == rView.controls_page;
    static const s32 cPageKeys[cHintsMax] = {KeyChange, KeyUndo, KeyPage};
    static const char* const cPageWords[cHintsMax] = {"SettingsHintChange", "SettingsHintUndo", "SettingsHintPage"};
    static const s32 cControlsKeys[cHintsMax] = {KeyBind, KeyUndo, KeyPage};
    static const char* const cControlsWords[cHintsMax] = {"SettingsHintBind", "SettingsHintClear", "SettingsHintPage"};
    static const char* const cFirstWords[] = {nullptr, "SettingsHintPress", "SettingsHintPickUp", "SettingsHintPutDown"};
    for (s32 i = 0; i < cHintsMax; i++) {
        Hint& rHint = mHints[i];
        s32 key = controls ? cControlsKeys[i] : cPageKeys[i];
        const char* pWord = controls ? cControlsWords[i] : cPageWords[i];
        if (i == 0 && hint != HintChange) {
            key = KeyPress;
            pWord = cFirstWords[hint];
        }
        fillKeyCap(rHint.mKey, key, rView.input, controls);
        text.room(1);
        if (pWord != nullptr && rView.input != NEBULA_SETTINGS_INPUT_TOUCH) {
            getText(text, pWord);
        }
        setText(rHint.mWord, text.c_str());
        measure(rHint.mWord, 4000.0f, &rHint.mWordWidth, &height);
    }
    fillButton(mFooter[FooterApply], "SettingsApply");
    fillKeyCap(mFooter[FooterApply].mKey, KeyApply, rView.input, controls);
    fillButton(mFooter[FooterBack], "SettingsBack");
    fillKeyCap(mFooter[FooterBack].mKey, KeyBack, rView.input, controls);

    // A question over the page.
    s32 question = -1;
    if (mHome) {
        return;
    }
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
    if (rView.status == NEBULA_SETTINGS_STATUS_LICENSE || rView.status == NEBULA_SETTINGS_STATUS_MOD_ABOUT) {
        static MR::GrowBuffer< char16_t > shown;
        MR::Text16 pageText;
        if (rView.status_number > 0) {
            // Its number, then the page.
            getNumbered(pageText, "SettingsLicensePage", rView.status_number);
            u32 used = static_cast< u32 >(MR::textLength(pageText.c_str()));
            char16_t* const pNumber = pageText.grow(used + 1, used + 3);
            pNumber[used++] = u'\n';
            pNumber[used++] = u'\n';
            pNumber[used] = 0;
            fromUtf8(pageText, used, rView.long_text);
        } else if (rView.status == NEBULA_SETTINGS_STATUS_MOD_ABOUT) {
            // A picture page without its picture: its number, then why (it loads, can't be read, is missing).
            const s32 code = -rView.status_number;
            const char* const pState = code % 4 == 3   ? "SettingsModPictureMissing"
                                       : code % 4 == 2 ? "SettingsModPictureFailed"
                                                       : "SettingsModPictureLoading";
            if (code / 4 > 0) {
                MR::Text16 state;
                getText(state, pState);
                getNumbered(pageText, "SettingsLicensePage", code / 4);
                u32 used = static_cast< u32 >(MR::textLength(pageText.c_str()));
                const u32 stateLength = static_cast< u32 >(MR::textLength(state.c_str()));
                char16_t* const pText = pageText.grow(used + 1, used + stateLength + 3);
                pText[used++] = u'\n';
                pText[used++] = u'\n';
                for (u32 i = 0; i < stateLength; i++) {
                    pText[used++] = state.c_str()[i];
                }
                pText[used] = 0;
            } else {
                getText(pageText, pState);
            }
        } else {
            getText(pageText, rView.status_number == -2 ? "SettingsLicenseFailed" : "SettingsLicenseLoading");
        }
        const char16_t* const page = pageText.c_str();
        setAnchor(mQuestionText, 3);
        setTextSize(mQuestionText, cLicenseTextSize);
        u32 pageLength = 0;
        while (page[pageLength] != 0) {
            pageLength++;
        }
        if (pageLength > mQuestionText->GetStringBufferLength()) {
            mQuestionText->AllocStringBuffer(pageLength);
        }
        const f32 pageRoom = mPanelWidth - cLicenseInset * 2.0f - cQuestionMargin * 2.0f;
        // Lines broken at the last space that fits, measured once for a page: the box alone would break inside a word.
        static u32 shownLength = 0;
        char16_t* const pShown = shown.room(static_cast< u32 >(pageLength) + 1);
        bool samePage = shownLength == pageLength;
        for (u32 i = 0; samePage && i < pageLength; i++) {
            samePage = page[i] == pShown[i] || (page[i] == u' ' && pShown[i] == u'\n');
        }
        if (!samePage) {
            memcpy(pShown, page, pageLength * sizeof(char16_t));
            pShown[pageLength] = 0;
            shownLength = pageLength;
            u32 lineStart = 0;
            u32 lastSpace = 0;
            for (u32 i = 0; i <= pageLength; i++) {
                if (i != pageLength && pShown[i] != u' ' && pShown[i] != u'\n') {
                    continue;
                }
                mQuestionText->SetString(pShown + lineStart, 0, i - lineStart);
                measure(mQuestionText, 4000.0f, &width, &height);
                if (width > pageRoom && lastSpace > lineStart) {
                    pShown[lastSpace] = u'\n';
                    lineStart = lastSpace + 1;
                }
                if (i != pageLength) {
                    if (pShown[i] == u'\n') {
                        lineStart = i + 1;
                    } else {
                        lastSpace = i;
                    }
                }
            }
        }
        mQuestionText->SetString(pShown, 0, shownLength);
        measure(mQuestionText, pageRoom, &width, &height);
        mQuestionInk = height;
        for (s32 i = 0; i < mChoiceCount; i++) {
            fillKeyCap(mChoices[i].mKey, rQuestion.mKeys[i], rView.input, false);
        }
        return;
    }
    setAnchor(mQuestionText, 4);
    const char* pText = rView.status_number == 1 ? rQuestion.mTextOne : rQuestion.mText;
    if (rView.status == NEBULA_SETTINGS_STATUS_JOYCON) {
        // Paired, both halves here (hold L and R), or one missing (connect them).
        const s32 halves = NEBULA_JOYCON_LEFT | NEBULA_JOYCON_RIGHT;
        pText = (rView.status_number & NEBULA_JOYCON_PAIRED) != 0 ? "SettingsJoyConPaired"
                : (rView.status_number & halves) == halves     ? "SettingsJoyConPress"
                                                               : "SettingsJoyConConnect";
    }
    getNumbered(text, pText, rView.status_number);
    if (rView.status == NEBULA_SETTINGS_STATUS_MODS_CODE_ASK && rView.status_text != nullptr && rView.status_text[0] != 0) {
        // The mod's name above the question: more than one can wait for an answer.
        MR::Text16 asked;
        getNumbered(asked, pText, rView.status_number);
        fromUtf8(text, 0, rView.status_text);
        u32 used = static_cast< u32 >(MR::textLength(text.c_str()));
        const u32 askedLength = static_cast< u32 >(MR::textLength(asked.c_str()));
        char16_t* const pNamed = text.grow(used, used + askedLength + 3);
        pNamed[used++] = u'\n';
        pNamed[used++] = u'\n';
        for (u32 i = 0; i < askedLength; i++) {
            pNamed[used++] = asked.c_str()[i];
        }
        pNamed[used] = 0;
    }
    setTextSize(mQuestionText, cTextSize + 1.0f);
    setText(mQuestionText, text.c_str());
    // Too long for its window: broken at its spaces (or, written without spaces, in two), then smaller if need be.
    const f32 room = cQuestionWidth - 48.0f;
    measure(mQuestionText, 4000.0f, &width, &height);
    if (width > room) {
        bool spaced = false;
        for (const char16_t* p = text.c_str(); *p != 0; p++) {
            spaced = spaced || *p == u' ';
        }
        if (spaced) {
            wrapWords(mQuestionText, text.c_str(), room, &width, &height);
        } else if (breakUnspaced(text)) {
            setText(mQuestionText, text.c_str());
            measure(mQuestionText, 4000.0f, &width, &height);
        }
    }
    f32 size = cTextSize + 1.0f;
    if (width > room) {
        size *= clamp(room / width, 0.75f, 1.0f);
        setTextSize(mQuestionText, size);
        measure(mQuestionText, 4000.0f, &width, &height);
    }
    mQuestionInk = height - (cCapTop + 1.0f - cBaseline) * size;
    // The answers' keys follow what the player last used.
    for (s32 i = 0; i < mChoiceCount; i++) {
        Button& rChoice = mChoices[i];
        fillKeyCap(rChoice.mKey, rQuestion.mKeys[i], rView.input, false);
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
            fillButton(rChoice, cQuestions[question].mWords[i]);
            rChoice.mAction = cQuestions[question].mActions[i];
        }
    }
    mQuestion->SetVisible(question >= 0);
    const bool bar = question >= 0 && (cQuestions[question].mStatus == NEBULA_SETTINGS_STATUS_DLSS_INSTALLING ||
                                       cQuestions[question].mStatus == NEBULA_SETTINGS_STATUS_XESS_INSTALLING);
    mProgressTrack->SetVisible(bar);
    mProgressFill->SetVisible(bar);
    mJoyCon->SetVisible(question >= 0 && cQuestions[question].mStatus == NEBULA_SETTINGS_STATUS_JOYCON);
    mJoyConPaired = false;
    for (s32 half = 0; half < 2; half++) {
        mHalves[half].mPresses = -1;
        mHalves[half].mPop = 0.0f;
    }
}

// The HOME Menu as the host has it: its title and rows, or "are you sure?" and its answers; the selected one focused.
void NebulaSettingsScreen::fillHome(s32 homeView) {
    // HOME reuses the modal, but never its progress bar or controller-pairing illustration.
    mProgressTrack->SetVisible(false);
    mProgressFill->SetVisible(false);
    mJoyCon->SetVisible(false);
    mQuestionText->SetVisible(true);
    setAnchor(mQuestionText, 4);
    const s32 asking = homeView >> 8 & 0xFF;
    if (mHomeView < 0 || asking != (mHomeView >> 8 & 0xFF)) {
        MR::Text16 text;
        f32 width = 0.0f;
        f32 height = 0.0f;
        if (asking == 0) {
            getText(text, "HomeMenuTitle");
            setTextSize(mQuestionText, cTitleSize);
        } else {
            // The question, then what it costs.
            const u32 length = static_cast< u32 >(getText(text, asking == 1 ? "QuitQuestion" : "ResetQuestion"));
            MR::Text16 cost;
            const u32 costLength = static_cast< u32 >(getText(cost, "UnsavedLost"));
            char16_t* const pText = text.grow(length + 1, length + 1 + costLength + 1);
            pText[length] = u'\n';
            for (u32 k = 0; k <= costLength; k++) {
                pText[length + 1 + k] = cost.c_str()[k];
            }
            setTextSize(mQuestionText, cTextSize + 1.0f);
        }
        setText(mQuestionText, text.c_str());
        measure(mQuestionText, (asking == 0 ? cHomeWidth : cQuestionWidth) - 48.0f, &width, &height);
        mChoiceCount = asking == 0 ? cHomeRowNum : cHomeAnswerNum;
        for (s32 i = 0; i < ChoiceNum; i++) {
            Button& rChoice = mChoices[i];
            rChoice.mChip.mRoot->SetVisible(i < mChoiceCount);
            rChoice.mChip.mFocusLevel = 0.0f;
            if (i < mChoiceCount) {
                fillButton(rChoice, asking == 0 ? cHomeRows[i] : cHomeAnswers[i]);
                fillKeyCap(rChoice.mKey, -1, 0, false);
            }
        }
        const f32 panelWidth = asking == 0 ? cHomeWidth : cQuestionWidth;
        const f32 panelHeight = asking == 0 ? cPad * 2.0f + cTitleSize + 18.0f + cButtonHeight * cHomeRowNum + cHomeGap * (cHomeRowNum - 1)
                                            : cQuestionHeight;
        mQuestionPanel->mSize = Ui::Size(panelWidth, panelHeight);
        mQuestionShadow->mSize = Ui::Size(panelWidth, panelHeight);
        mQuestion->SetVisible(true);
    }
    mHomeView = homeView;
    const s32 selected = homeView & 0xFF;
    mFocusButton = selected < mChoiceCount ? &mChoices[selected] : nullptr;
}

// The pointer, in the menu's units: over a page's button, a row (its buttons, its arrows, its slider), the footer's
// buttons or the question's. Moving it picks the row under it; A chooses what it is over.
void NebulaSettingsScreen::readPointer(const NebulaSettingsView& rView) {
    mHoverRow = -1;
    mHoverTab = -1;
    mHoverRowButton = -1;
    mHoverArrow = 0;
    mHoverSlider = false;
    mHoverButton = nullptr;
    mHoverModButton = -1;
    // A finger's swipe (host_window_sdl: a tenth of the window's height to a notch): the list follows the finger.
    const s32 swipe = NebulaHost::pointerWheel();
    if (mSwipeFrames > 0) {
        mSwipeFrames--;
    }
    if (swipe != 0 && mFrame > 1 && !mHome) {
        Vec2f edge, top, bottom;
        edge.x = 0.0f;
        edge.y = 0.0f;
        MR::convertScreenPosToLayoutPos(&top, edge);
        edge.y = static_cast< s32 >(NebulaFrameClock::cScreenLines);
        MR::convertScreenPosToLayoutPos(&bottom, edge);
        mScrollTarget -= static_cast< f32 >(swipe) * __builtin_fabsf(bottom.y - top.y) / 1200.0f;
        mDraggedAt = static_cast< s32 >(rView.selected);
        mSwipeFrames = cSwipeHoverFrames;
    }
    // A binding that waits for an input takes the mouse's buttons itself, wherever the pointer is.
    if (!MR::isPointerInScreen(PLAYER_1) || rView.status == NEBULA_SETTINGS_STATUS_CAPTURE_KEY ||
        rView.status == NEBULA_SETTINGS_STATUS_CAPTURE_PAD) {
        mDragRow = -1;
        mDragging = false;
        mModSwipeList = -1;
        mPressing = false;
        mScrollDrag = false;
        mHeldArrow = 0;
        mSliderRow = -1;
        return;
    }
    // The player's pointer as StarPointerController maps it to the screen.
    Vec2f screen;
    MR::getPointerPastPos(&screen, 0, PLAYER_1);
    screen.x = (screen.x + 1.0f) * MR::getScreenWidth() * 0.5f;
    screen.y = (screen.y + 1.0f) * static_cast< s32 >(NebulaFrameClock::cScreenLines) * 0.5f;
    Vec2f layout;
    MR::convertScreenPosToLayoutPos(&layout, screen);
    const f32 x = layout.x / (mScaleX * mZoom);
    const f32 y = layout.y / mZoom - mMenu->mTranslate.y;
    if (__builtin_fabsf(x - mPointerX) > 0.5f || __builtin_fabsf(y - mPointerY) > 0.5f) {
        mPointerMode = true;
    }
    mPointerX = x;
    mPointerY = y;
    if (mHome) {
        readHomePointer(x, y);
        return;
    }

    if (mQuestionShown >= 0) {
        for (s32 i = 0; i < mChoiceCount; i++) {
            const Chip& rChip = mChoices[i].mChip;
            if (isOnButton(x, y, rChip.mX, rChip.mRoot->mTranslate.y, rChip.mWidth, cButtonHeight, cButtonGap)) {
                mHoverButton = &mChoices[i];
            }
        }
    } else {
        for (u32 i = 0; i < rView.page_count; i++) {
            if (i != rView.page && isOnButton(x, y, mTabs[i].mX, mTabY, mTabs[i].mWidth, cTabHeight, cTabGap)) {
                mHoverTab = static_cast< s32 >(i);
            }
        }
        for (s32 i = 0; i < FooterNum; i++) {
            if (isOnButton(x, y, mFooter[i].mChip.mX, mFooterY, mFooter[i].mChip.mWidth, cButtonHeight, cButtonGap)) {
                mHoverButton = &mFooter[i];
            }
        }
        if (hasModLists(rView)) {
            readModsPointer(rView, x, y);
        } else if (x >= mLeft && x <= mRight && y <= mListTop && y >= mListBottom && !mScrollDrag && mSwipeFrames == 0) {
            const s32 row = static_cast< s32 >((mListTop - y + mScroll) / cRowHeight);
            const NebulaSettingsRow* pData = row >= 0 ? rowAt(rView, static_cast< u32 >(row)) : nullptr;
            if (pData != nullptr && (pData->flags & NEBULA_SETTINGS_ROW_HEADING) == 0) {
                mHoverRow = row;
                const Row& rRow = mRows[row];
                // A row's buttons and arrows reach the whole row's height, and half the room to what is beside them.
                for (s32 i = 0; i < rRow.mButtonCount; i++) {
                    if (__builtin_fabsf(x - rRow.mButtons[i].mX) <= (rRow.mButtons[i].mWidth + cRowButtonGap) * 0.5f) {
                        mHoverRowButton = i;
                    }
                }
                for (s32 i = 0; i < 2 && rRow.mArrows; i++) {
                    if (__builtin_fabsf(x - rRow.mArrowX[i]) <= (cArrowWidth + cRowButtonGap) * 0.5f) {
                        mHoverArrow = i == 0 ? -1 : 1;
                    }
                }
                mHoverSlider = rRow.mSliderRight > rRow.mSliderLeft && x >= rRow.mSliderLeft - cKnobSize &&
                               x <= rRow.mSliderRight + cKnobSize;
            }
        }
    }

    // A slider held down follows the pointer along it; an arrow held down steps again and again, stopping at the end.
    if (mSliderRow >= 0) {
        if (MR::isHeldJump(PLAYER_1)) {
            dragSlider(rView, x);
            return;
        }
        mSliderRow = -1;
    }
    if (mHeldArrow != 0) {
        if (MR::isHeldJump(PLAYER_1)) {
            mHeldFrames++;
            if (mHoverArrow == mHeldArrow && mHoverRow == static_cast< s32 >(rView.selected) && mHeldFrames >= cRepeatDelay &&
                (mHeldFrames - cRepeatDelay) % cRepeatEvery == 0) {
                NebulaHost::settingsInput(NEBULA_SETTINGS_CHANGE, mHeldArrow);
            }
            return;
        }
        mHeldArrow = 0;
    }
    if (mPointerMode && mHoverRow >= 0 && mHoverRow != static_cast< s32 >(rView.selected)) {
        mPointerRow = mHoverRow;
        NebulaHost::settingsInput(NEBULA_SETTINGS_SELECT_ROW, mHoverRow);
    }
    // The list held down: what the press does waits for the pointer's first move, so a finger scrolling the list over
    // an arrow or a slider changes nothing. Up or down past cDragStart drags the list; along a slider, the slider
    // follows; held still on an arrow, it steps and goes on stepping. Let go where it was pressed, what is there is
    // clicked: an arrow steps (going round at its ends), a slider takes the step under the pointer, a row its own.
    if (mPressing) {
        if (MR::isHeldJump(PLAYER_1)) {
            const f32 across = __builtin_fabsf(x - mPressX);
            const f32 along = __builtin_fabsf(y - mPressY);
            if (!mScrollDrag && mPressSlider && across > cDragStart && across >= along) {
                mPressing = false;
                mSliderRow = mPressRow;
                dragSlider(rView, x);
                return;
            }
            if (!mScrollDrag && along > cDragStart) {
                mScrollDrag = true;
            }
            if (mScrollDrag) {
                mScrollTarget = mPressScroll + (y - mPressY);
                return;
            }
            if (mPressArrow != 0 && ++mHeldFrames >= cRepeatDelay) {
                mPressing = false;
                NebulaHost::settingsInput(NEBULA_SETTINGS_CHANGE, mPressArrow * 2);
                mHeldArrow = mPressArrow;
                mHeldFrames = cRepeatDelay;
            }
            return;
        }
        mPressing = false;
        if (!mScrollDrag && mPressRow >= 0 && mPressRow == mHoverRow) {
            if (mPressArrow != 0) {
                if (mHoverArrow == mPressArrow) {
                    NebulaHost::settingsInput(NEBULA_SETTINGS_CHANGE, mPressArrow * 2);
                }
            } else if (mPressSlider) {
                mSliderRow = mPressRow;
                dragSlider(rView, mPressX);
                mSliderRow = -1;
            } else {
                clickRow(rView, mPressRow);
            }
        } else if (mScrollDrag) {
            mDraggedAt = static_cast< s32 >(rView.selected);
        }
        mScrollDrag = false;
        return;
    }
    if (!MR::isPressedJump(PLAYER_1)) {
        return;
    }
    if (mHoverButton != nullptr) {
        NebulaHost::settingsInput(mHoverButton->mAction, 0);
    } else if (mHoverTab >= 0) {
        NebulaHost::settingsInput(NEBULA_SETTINGS_SELECT_PAGE, mHoverTab);
    } else if (!hasModLists(rView) && x >= mLeft && x <= mRight && y <= mListTop && y >= mListBottom) {
        mPressing = true;
        mPressRow = mHoverRow;
        mPressArrow = mHoverRow >= 0 ? mHoverArrow : 0;
        mPressSlider = mHoverRow >= 0 && mHoverSlider;
        mPressX = x;
        mPressY = y;
        mPressScroll = mScroll;
        mHeldFrames = 0;
        if (mPressArrow != 0 || mPressSlider) {
            mPointerMode = true;
            mPointerRow = mHoverRow;
            NebulaHost::settingsInput(NEBULA_SETTINGS_SELECT_ROW, mHoverRow);
        }
    }
}

// A row clicked: its button (a row with one anywhere along it), or a setting stepped on as its right arrow does (a
// binding waits for an input). A slider's row takes the slider itself.
void NebulaSettingsScreen::clickRow(const NebulaSettingsView& rView, s32 row) {
    const NebulaSettingsRow* pData = rowAt(rView, static_cast< u32 >(row));
    if (pData == nullptr) {
        return;
    }
    const Row& rRow = mRows[row];
    mPointerMode = true;
    mPointerRow = row;
    if (hasButtons(pData) && (mHoverRowButton >= 0 || rRow.mButtonCount == 1)) {
        NebulaHost::settingsInput(NEBULA_SETTINGS_SELECT_ROW, row);
        NebulaHost::settingsInput(NEBULA_SETTINGS_PRESS, mHoverRowButton >= 0 ? mHoverRowButton : 0);
    } else if ((pData->flags & NEBULA_SETTINGS_ROW_CHANGES) != 0 && !hasSlider(*pData)) {
        NebulaHost::settingsInput(NEBULA_SETTINGS_SELECT_ROW, row);
        NebulaHost::settingsInput(NEBULA_SETTINGS_CHANGE, hasArrows(*pData) ? 2 : 1);
    }
}

// The slider held down set to the step under the pointer.
void NebulaSettingsScreen::dragSlider(const NebulaSettingsView& rView, f32 x) {
    const NebulaSettingsRow* pData = rowAt(rView, static_cast< u32 >(mSliderRow));
    if (pData == nullptr || !hasSlider(*pData)) {
        mSliderRow = -1;
        return;
    }
    const Row& rRow = mRows[mSliderRow];
    const f32 length = rRow.mSliderRight - rRow.mSliderLeft;
    if (length <= 0.0f) {
        return;
    }
    const s32 step = static_cast< s32 >(clamp((x - rRow.mSliderLeft) / length, 0.0f, 1.0f) * pData->slider_steps + 0.5f);
    const bool selected = mSliderRow == static_cast< s32 >(rView.selected);
    if (!selected) {
        NebulaHost::settingsInput(NEBULA_SETTINGS_SELECT_ROW, mSliderRow);
    }
    if (!selected || step != static_cast< s32 >(pData->slider_at)) {
        NebulaHost::settingsInput(NEBULA_SETTINGS_SET_VALUE, step);
    }
}

// The pointer over the MODS page's lists and buttons. A mod under it is selected; held down and moved (a finger, the
// mouse or A), it is dragged at once, and let go over a list it lands there (the line shows where). Held at a list's top
// or bottom edge, the list scrolls under it. Held down on a list's free space and moved, the list scrolls; a click on a
// button presses it.
void NebulaSettingsScreen::readModsPointer(const NebulaSettingsView& rView, f32 x, f32 y) {
    const Row* const pButtonsRow = mModButtonsRow >= 0 ? &mRows[mModButtonsRow] : nullptr;
    for (s32 i = 0; i < mModButtonCount; i++) {
        if (!(pButtonsRow != nullptr && pButtonsRow->mButtons[i].mOff) && isOnButton(x, y, mModButtons[i].mX, mModButtonsY, mModButtons[i].mWidth, cButtonHeight, 8.0f)) {
            mHoverModButton = i;
        }
    }
    // The list and the place under the pointer.
    s32 overList = -1;
    for (s32 i = 0; i < 2; i++) {
        if (x >= mModLeft[i] - cModsGap * 0.5f && x <= mModLeft[i] + mModWidth + cModsGap * 0.5f && y <= mModItemsTop + cReach &&
            y >= mModItemsBottom - cReach) {
            overList = i;
        }
    }
    s32 hovered = -1;
    if (overList >= 0 && y <= mModItemsTop && y >= mModItemsBottom) {
        const s32 place = static_cast< s32 >((mModItemsTop - y + mModScroll[overList]) / cModHeight);
        for (s32 i = 0; i < static_cast< s32 >(rView.total_rows); i++) {
            if (mItemList[i] == overList && mItemPlace[i] == place) {
                hovered = i;
            }
        }
    }
    if (!mDragging) {
        mHoverRow = hovered;
    }
    // The pointer moving on a list's free space scrolls that list; let go, it stays there.
    if (mModSwipeList >= 0) {
        if (MR::isHeldJump(PLAYER_1)) {
            mModFree[mModSwipeList] = true;
            mModFreeScroll[mModSwipeList] = mModSwipeFrom + (y - mDragFromY);
            return;
        }
        mModSwipeList = -1;
        return;
    }
    if (mDragRow >= 0) {
        const s32 index = mDragRow;
        const bool moved = __builtin_fabsf(x - mDragFromX) > cDragStart || __builtin_fabsf(y - mDragFromY) > cDragStart;
        if (!mDragging && moved) {
            mDragging = true;
            mEdgeScrollNs = 0u;
            MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");
        }
        mDropList = mDragging ? overList : -1;
        if (mDropList >= 0) {
            edgeScroll(mDropList, y);
        } else {
            mEdgeScrollNs = 0u;
        }
        if (mDropList >= 0) {
            const s32 place = static_cast< s32 >((mModItemsTop - y + mModScroll[mDropList]) / cModHeight + 0.5f);
            mDropPlace = place < 0 ? 0 : place > mModCounts[mDropList] ? mModCounts[mDropList] : place;
        }
        if (!MR::isHeldJump(PLAYER_1)) {
            // Let go: into the list under the pointer at the line's place (counted without the mod itself).
            if (mDragging && mDropList >= 0 && index >= 0 && index < static_cast< s32 >(rView.total_rows)) {
                s32 place = mDropPlace;
                if (mDropList == mItemList[index] && place > mItemPlace[index]) {
                    place--;
                }
                if (mDropList != mItemList[index] || place != mItemPlace[index]) {
                    NebulaHost::settingsInput(NEBULA_SETTINGS_SELECT_ROW, mDragRow);
                    NebulaHost::settingsInput(NEBULA_SETTINGS_MOVE_MOD, (mDropList + 1) << 16 | place);
                    MR::startSystemSE("SE_SY_SELECT_PAUSE_ITEM");
                    const char* pLabel = rView.rows[index].label;
                    u32 length = 0;
                    while (pLabel[length] != '\0') {
                        length++;
                    }
                    char* const pSettle = mSettleLabel.room(length + 1);
                    for (u32 i = 0; i <= length; i++) {
                        pSettle[i] = pLabel[i];
                    }
                    mSettleList = mDropList;
                    mSettlePlace = place;
                    mSettleFrames = 4;
                }
            }
            if (mDragging) {
                // The dropped mod stays selected: the row under the pointer (the drop line rounds to the nearest gap,
                // so it can be a neighbour) is picked again only once the pointer moves.
                mPointerMode = false;
            }
            mDragRow = -1;
            mDragging = false;
            mDropList = -1;
            mEdgeScrollNs = 0u;
        }
        return;
    }
    if (!MR::isPressedJump(PLAYER_1)) {
        return;
    }
    if (mHoverModButton >= 0 && mModButtonsRow >= 0) {
        NebulaHost::settingsInput(NEBULA_SETTINGS_SELECT_ROW, mModButtonsRow);
        NebulaHost::settingsInput(NEBULA_SETTINGS_PRESS, mHoverModButton);
    } else if (hovered >= 0) {
        // Held down: dragged once it moves.
        mPointerMode = true;
        mPointerRow = hovered;
        NebulaHost::settingsInput(NEBULA_SETTINGS_SELECT_ROW, hovered);
        mDragRow = hovered;
        mDragFromX = x;
        mDragFromY = y;
    } else if (overList >= 0) {
        // Held down on the list's free space: moved, the list follows.
        mPointerMode = true;
        mModSwipeList = overList;
        mModSwipeFrom = mModScroll[overList];
        mDragFromY = y;
    }
}

// A dragged mod at the top or bottom edge of the list under it (a row's height in from it, or past it): the list scrolls
// toward that edge, faster the further in, by the time since the last frame so at any frame rate alike.
void NebulaSettingsScreen::edgeScroll(s32 list, f32 y) {
    const u64 now = NebulaHost::clockNs();
    const f32 seconds = mEdgeScrollNs != 0u && now > mEdgeScrollNs ? static_cast< f32 >(now - mEdgeScrollNs) * 1.0e-9f : 0.0f;
    mEdgeScrollNs = now;
    // > 0 toward the top, < 0 toward the bottom: from the middle of the first or last row in sight (0) to the edge (1).
    const f32 band = cModHeight * 0.5f;
    f32 depth = 0.0f;
    if (y > mModItemsTop - band) {
        depth = (y - (mModItemsTop - band)) / band;
    } else if (y < mModItemsBottom + band) {
        depth = (y - (mModItemsBottom + band)) / band;
    }
    depth = clamp(depth, -1.0f, 1.0f);
    if (depth == 0.0f || seconds <= 0.0f) {
        return;
    }
    const f32 step = depth * cEdgeScrollRows * cModHeight * (seconds < 0.1f ? seconds : 0.1f);
    if (!mModFree[list]) {
        mModFree[list] = true;
        mModFreeScroll[list] = mModScroll[list];
    }
    // The top edge shows earlier mods (less scroll), the bottom later ones; the layout keeps it in the list's range.
    mModFreeScroll[list] -= step;
    mModScroll[list] = mModFreeScroll[list];
}

// The pointer over the HOME Menu's answers: moving onto one selects it (the host's selection, as the keys move it), A
// chooses it. The host's menu plays its sounds.
void NebulaSettingsScreen::readHomePointer(f32 x, f32 y) {
    s32 hovered = -1;
    for (s32 i = 0; i < mChoiceCount; i++) {
        const Chip& rChip = mChoices[i].mChip;
        if (isOnButton(x, y, rChip.mX, rChip.mRoot->mTranslate.y, rChip.mWidth, cButtonHeight, cHomeGap)) {
            hovered = i;
            mHoverButton = &mChoices[i];
        }
    }
    if (hovered < 0) {
        return;
    }
    if (mPointerMode && hovered != (mHomeView & 0xFF)) {
        NebulaHomeMenu::point(hovered);
    }
    if (MR::isPressedJump(PLAYER_1)) {
        NebulaHomeMenu::choose(hovered);
    }
}

// The line under the page in the room beside Apply and Back: narrowed down to cMinSqueeze, then cut short with an
// ellipsis (a mod's long list of conflicts).
void NebulaSettingsScreen::fitStatus(f32 room) {
    mStatusRoom = room;
    f32 height = 0.0f;
    setText(mStatus, mStatusText.c_str());
    measure(mStatus, 4000.0f, &mStatusWidth, &height);
    if (mStatusWidth * cMinSqueeze <= room) {
        return;
    }
    // The longest start that fits with its ellipsis.
    MR::Text16 cut;
    s32 fits = 0;
    s32 longest = static_cast< s32 >(MR::textLength(mStatusText.c_str())) - 1;
    while (fits < longest) {
        const s32 length = (fits + longest + 1) / 2;
        shorten(cut, mStatusText.c_str(), length);
        setText(mStatus, cut.c_str());
        measure(mStatus, 4000.0f, &mStatusWidth, &height);
        if (mStatusWidth * cMinSqueeze <= room) {
            fits = length;
        } else {
            longest = length - 1;
        }
    }
    shorten(cut, mStatusText.c_str(), fits);
    setText(mStatus, cut.c_str());
    measure(mStatus, 4000.0f, &mStatusWidth, &height);
}

// A button's art `width` x `height` centred on (x, y); it grows and shines while focused (under the pointer, or the
// selected one).
void NebulaSettingsScreen::placeChip(Chip& rChip, f32 x, f32 y, f32 width, f32 height, bool focused) {
    rChip.mFocusLevel += ((focused ? 1.0f : 0.0f) - rChip.mFocusLevel) * 0.35f;
    rChip.mX = x;
    rChip.mWidth = width;
    rChip.mHeight = height;
    shapeChip(rChip, width, height);
    const f32 grow = 1.0f + 0.05f * rChip.mFocusLevel;
    rChip.mRoot->mTranslate = Vec3f(x, y, 0.0f);
    rChip.mRoot->mScale = Vec2f(grow, grow);
    rChip.mLayers[LayerShine]->SetVisible(rChip.mFocusLevel > 0.02f);
    rChip.mLayers[LayerShine]->mAlpha = toAlpha(rChip.mFocusLevel * 0.7f);
}

// A row's own button `width` x `height` centred on (x, y): a box with the rows' corners over its darker edge (a knob:
// round). Focused, it grows a little, its face whitens and its ring darkens; unavailable, it is grey.
void NebulaSettingsScreen::placeRowBox(RowChip& rPooled, f32 x, f32 y, f32 width, f32 height, f32 radius, bool focused, bool off) {
    Chip& rChip = rPooled.mChip;
    rChip.mFocusLevel += ((focused ? 1.0f : 0.0f) - rChip.mFocusLevel) * 0.35f;
    rChip.mX = x;
    rChip.mWidth = width;
    rChip.mHeight = height;
    const f32 grow = 1.0f + 0.05f * rChip.mFocusLevel;
    rChip.mRoot->mTranslate = Vec3f(x, y + cEdgeDrop * 0.5f, 0.0f);
    rChip.mRoot->mScale = Vec2f(grow, grow);
    shape(rPooled.mEdge, 0.0f, -cEdgeDrop, width, height, radius);
    shape(rPooled.mFace, 0.0f, 0.0f, width, height, radius);
    rPooled.mEdge->SetVisible(!off);
    paint(rPooled.mFace, off ? cRowButtonOff : mix(cRowButtonFill, cRowButtonLit, rChip.mFocusLevel),
          off ? cFrameRing : mix(cRowButtonRing, cRowButtonLitRing, rChip.mFocusLevel));
}

// A button's art for `width`, its key and word centred together on it; it grows and shines under the pointer.
void NebulaSettingsScreen::placeButton(Button& rButton, f32 x, f32 y, f32 width) {
    Chip& rChip = rButton.mChip;
    placeChip(rChip, x, y, width, cButtonHeight, mHoverButton == &rButton || mFocusButton == &rButton);
    // Narrowed together when a language runs longer than the button's middle (its rounded ends stay clear).
    const f32 key = rButton.mKey.mWidth;
    const f32 gap = key > 0.0f ? cKeyGap : 0.0f;
    const f32 wordRoom = width - cButtonHeight - key - gap;
    const f32 word = rButton.mWordWidth;
    const f32 squeeze = word > wordRoom && word > 0.0f ? clamp(wordRoom / word, cMinSqueeze, 1.0f) : 1.0f;
    const f32 total = key + gap + word * squeeze;
    placeKeyCap(rButton.mKey, -total * 0.5f, 0.0f);
    rChip.mText->mScale = Vec2f(squeeze, 1.0f);
    rChip.mText->mTranslate = Vec3f(total * 0.5f - word * squeeze * 0.5f, textCentre(0.0f, cButtonSize), 0.0f);
}

void NebulaSettingsScreen::showMods(bool shown) {
    for (s32 i = 0; i < 2; i++) {
        mModPanels[i]->SetVisible(shown);
        mModTitles[i]->SetVisible(shown);
        mModEmpty[i]->SetVisible(shown);
    }
    for (s32 i = 0; i < mModButtonsMade; i++) {
        mModButtons[i].mRoot->SetVisible(shown && i < mModButtonCount);
    }
    mDropLine->SetVisible(false);
}

// The MODS page as the launcher's Mods tab: the mods off on the left and on on the right in their frames under their
// titles, each list scrolled to the selection, the mod picked up or dragged lifted from its place (a line where it
// would land), and the buttons under the lists sharing the window's width.
void NebulaSettingsScreen::placeMods(const NebulaSettingsView& rView) {
    mModWidth = (mRight - mLeft - cModsGap) * 0.5f;
    mModLeft[0] = mLeft;
    mModLeft[1] = mRight - mModWidth;
    const f32 titleY = mListTop - cHeadingSize * 0.5f;
    const f32 frameTop = mListTop - cHeadingSize - 8.0f;
    mModButtonsY = mListBottom + cButtonHeight * 0.5f;
    const f32 frameBottom = mModButtonsY + cButtonHeight * 0.5f + 10.0f;
    mModItemsTop = frameTop - 5.0f;
    mModItemsBottom = frameBottom + 5.0f;
    mClipTop = mModItemsTop;
    mClipBottom = mModItemsBottom;
    const f32 inner = mModItemsTop - mModItemsBottom;

    // Each row's list and place (in the whole list); the selection's.
    mModCounts[0] = static_cast< s32 >(rView.mod_list_count[0]);
    mModCounts[1] = static_cast< s32 >(rView.mod_list_count[1]);
    s32 seen[2] = {0, 0};
    mModButtonsRow = -1;
    s32 selectedList = -1;
    s32 selectedPlace = 0;
    bool held = false;
    for (s32 i = 0; i < static_cast< s32 >(rView.total_rows); i++) {
        const NebulaSettingsRow& rData = rView.rows[i];
        const s32 row = i;
        mItemList[i] = (rData.flags & NEBULA_SETTINGS_ROW_HEADING) != 0 ? -1 : modList(rData);
        if (hasButtons(&rData)) {
            mModButtonsRow = row;
        }
        if (mItemList[i] < 0) {
            mRows[i].mRoot->SetVisible(false);
            continue;
        }
        mItemPlace[i] = seen[mItemList[i]]++;
        if (row == static_cast< s32 >(rView.selected)) {
            selectedList = mItemList[i];
            selectedPlace = mItemPlace[i];
            held = (rData.flags & NEBULA_SETTINGS_ROW_HELD) != 0;
        }
    }

    // Each list scrolled to keep the selection in sight (a row of room past it when the keys move it).
    for (s32 l = 0; l < 2; l++) {
        const f32 content = mModCounts[l] * cModHeight;
        const f32 maxScroll = content > inner ? content - inner : 0.0f;
        f32 target = mModScroll[l];
        if (mModFree[l] && !mPointerMode) {
            mModFree[l] = false;  // the keys moved the selection: it is kept in sight again
        }
        if (mModFree[l]) {
            mModFreeScroll[l] = clamp(mModFreeScroll[l], 0.0f, maxScroll);
            target = mModFreeScroll[l];
        } else if (l == selectedList && !mDragging) {
            const f32 top = selectedPlace * cModHeight;
            const f32 room = mPointerMode ? 0.0f : cModHeight;
            target = top - room < target ? top - room : target;
            target = top + cModHeight + room > target + inner ? top + cModHeight + room - inner : target;
        }
        target = clamp(target, 0.0f, maxScroll);
        mModScroll[l] = mFrame <= 1 ? target : mModScroll[l] + (target - mModScroll[l]) * 0.35f;
    }

    // The mods in their lists; the dragged one under the pointer.
    f32 highlightX = 0.0f;
    f32 highlightY = 0.0f;
    for (s32 i = 0; i < static_cast< s32 >(rView.total_rows); i++) {
        if (mItemList[i] < 0) {
            continue;
        }
        Row& rRow = mRows[i];
        const s32 row = i;
        const s32 list = mItemList[i];
        f32 x = mModLeft[list];
        f32 y = mModItemsTop - mItemPlace[i] * cModHeight - cModHeight * 0.5f + mModScroll[list];
        const bool dragged = mDragging && row == mDragRow;
        bool settling = false;
        if (mSettleFrames > 0) {
            const char* pLabel = rView.rows[i].label;
            s32 c = 0;
            while (pLabel[c] != '\0' && pLabel[c] == mSettleLabel[c]) {
                c++;
            }
            if (pLabel[c] == mSettleLabel[c]) {
                settling = list != mSettleList || mItemPlace[i] != mSettlePlace;
                if (!settling) {
                    mSettleFrames = 0;  // the view has it where it landed
                }
            }
        }
        if (settling) {
            x = mModLeft[mSettleList];
            y = mModItemsTop - mSettlePlace * cModHeight - cModHeight * 0.5f + mModScroll[mSettleList];
        } else if (dragged) {
            x += mPointerX - mDragFromX;
            y = mPointerY;
        } else if (held && row == static_cast< s32 >(rView.selected)) {
            y += 2.0f;  // picked up
        }
        rRow.mRoot->mTranslate = Vec3f(x, y, 0.0f);
        const bool inView = dragged || (y < mModItemsTop + cModHeight && y > mModItemsBottom - cModHeight);
        rRow.mRoot->SetVisible(inView);
        if (row == static_cast< s32 >(rView.selected)) {
            highlightX = x + mModWidth * 0.5f;
            highlightY = y;
        }
        if (!inView) {
            continue;  // out of sight: nothing of it is laid out until it scrolls in
        }
        const f32 room = mModWidth - 28.0f;
        rRow.mLabel->mScale = Vec2f(rRow.mLabelWidth > room && rRow.mLabelWidth > 0.0f ? clamp(room / rRow.mLabelWidth, cMinSqueeze, 1.0f) : 1.0f, 1.0f);
        rRow.mLabel->mTranslate = Vec3f(14.0f, textCentre(0.0f, cTextSize), 0.0f);
        rRow.mValue->SetVisible(false);
        rRow.mFrame->SetVisible(false);
        rRow.mRule->SetVisible(false);
        rRow.mSliderTrack->SetVisible(false);
        rRow.mSliderFill->SetVisible(false);
    }

    // A few frames on from the drop, the dropped mod is drawn as the view has it whatever it holds.
    if (mSettleFrames > 0) {
        mSettleFrames--;
    }

    // The selection's highlight; a mod picked up or dragged rides a little higher, ringed in the game's blue.
    const bool lifted = held || mDragging;
    mHighlightY = mFrame <= 1 || mDragging ? highlightY : mHighlightY + (highlightY - mHighlightY) * 0.45f;
    paint(mHighlight, cHighlightFill, lifted ? cAccent : cHighlightRing);
    shape(mHighlight, highlightX, mHighlightY, mModWidth - 8.0f, cModHeight - 2.0f, 9.0f);
    mHighlight->SetVisible(selectedList >= 0 && mQuestionShown < 0);
    for (s32 i = 0; i < static_cast< s32 >(rView.total_rows); i++) {
        if (mItemList[i] >= 0 && mRows[i].mRoot->IsVisible()) {
            setTextColor(mRows[i].mLabel, lifted && i == static_cast< s32 >(rView.selected) ? cAccent : cText,
                         lifted && i == static_cast< s32 >(rView.selected) ? cAccent : cText);
        }
    }

    // The lists' frames and titles, and what an empty one says.
    for (s32 l = 0; l < 2; l++) {
        shape(mModPanels[l], mModLeft[l] + mModWidth * 0.5f, (frameTop + frameBottom) * 0.5f, mModWidth, frameTop - frameBottom, 12.0f);
        mModTitles[l]->mTranslate = Vec3f(mModLeft[l] + 4.0f, textCentre(titleY, cHeadingSize), 0.0f);
        const bool none = l == 1 && mModCounts[0] == 0 && mModCounts[1] == 0;
        mModEmpty[l]->SetVisible(none);
        if (none) {
            MR::Text16 text;
            f32 width = 0.0f;
            f32 height = 0.0f;
            getText(text, "SettingsModsNone");
            setText(mModEmpty[l], text.c_str());
            measure(mModEmpty[l], mModWidth - 24.0f, &width, &height);
            mModEmpty[l]->mTranslate =
                Vec3f(mModLeft[l] + mModWidth * 0.5f, textCentre((frameTop + frameBottom) * 0.5f, cFooterSize), 0.0f);
        }
    }

    // Where a dragged mod would land.
    mDropLine->SetVisible(mDragging && mDropList >= 0);
    if (mDragging && mDropList >= 0) {
        const f32 lineY = mModItemsTop - mDropPlace * cModHeight + mModScroll[mDropList];
        shape(mDropLine, mModLeft[mDropList] + mModWidth * 0.5f, lineY, mModWidth - 16.0f, 3.0f, 1.5f);
    }

    // The buttons, the window's width shared, words narrowed to fit.
    const Row* pButtons = mModButtonsRow >= 0 ? &mRows[mModButtonsRow] : nullptr;
    mModButtonCount = pButtons != nullptr ? pButtons->mButtonCount : 0;
    if (!addModButtons(mModButtonCount) && mModButtonCount > mModButtonsMade) {
        mModButtonCount = mModButtonsMade;
    }
    if (pButtons != nullptr && pButtons->mButtonsVersion != mModButtonsVersion) {
        mModButtonsVersion = pButtons->mButtonsVersion;
        for (s32 i = 0; i < mModButtonCount; i++) {
            f32 height = 0.0f;
            setText(mModButtons[i].mText, pButtons->mButtons[i].mWord.mItems);
            measure(mModButtons[i].mText, 4000.0f, &mModButtons[i].mText->mSize.width, &height);
        }
    }
    const f32 gap = 8.0f;
    const f32 width = mModButtonCount > 0 ? (mRight - mLeft - gap * (mModButtonCount - 1)) / mModButtonCount : 0.0f;
    for (s32 i = 0; i < mModButtonsMade; i++) {
        Chip& rChip = mModButtons[i];
        rChip.mRoot->SetVisible(i < mModButtonCount);
        if (i >= mModButtonCount) {
            continue;
        }
        const bool focused = (mModButtonsRow == static_cast< s32 >(rView.selected) && i == static_cast< s32 >(rView.selected_button) &&
                              !mPointerMode) ||
                             i == mHoverModButton;
        const bool off = pButtons->mButtons[i].mOff;
        placeChip(rChip, mLeft + width * 0.5f + i * (width + gap), mModButtonsY, width, cButtonHeight, focused);
        setTextColor(rChip.mText, off ? cDisabled : cButtonText, off ? cDisabled : cButtonText);
        const f32 word = rChip.mText->mSize.width;
        const f32 room = width - 20.0f;
        rChip.mText->mScale = Vec2f(word > room && word > 0.0f ? clamp(room / word, cMinSqueeze, 1.0f) : 1.0f, 1.0f);
        rChip.mText->mTranslate = Vec3f(0.0f, textCentre(0.0f, cButtonSize), 0.0f);
    }

    // No row's own buttons and no scroll bar here.
    for (s32 i = 0; i < mRowChipCount; i++) {
        mRowChips[i].mRow = -1;
        mRowChips[i].mChip.mRoot->SetVisible(false);
    }
    mTrack->SetVisible(false);
    mThumb->SetVisible(false);
}

// The pool's button `index` as row `row`'s `button` (ChipLess... for a setting's own), its word set when it was
// another's.
RowChip& NebulaSettingsScreen::useRowChip(s32 index, s32 row, s32 button, u32 version, const char16_t* pWord) {
    while (mRowChipCount <= index && addRowChip()) {
    }
    RowChip& rPooled = mRowChips[index < mRowChipCount ? index : mRowChipCount - 1];
    Chip& rChip = rPooled.mChip;
    if (rPooled.mRow != row || rPooled.mButton != button || rPooled.mVersion != version) {
        rPooled.mRow = row;
        rPooled.mButton = button;
        rPooled.mVersion = version;
        rChip.mFocusLevel = 0.0f;
        const bool arrow = button == ChipLess || button == ChipMore;
        setTextStyle(rChip.mText, arrow ? cArrowSize : cRowButtonSize, arrow ? cAccent : cButtonText, arrow ? cAccent : cButtonText, 4);
        setText(rChip.mText, pWord);
        rChip.mText->mRotate.z = button == ChipLess ? 90.0f : button == ChipMore ? -90.0f : 0.0f;
        f32 height = 0.0f;
        measure(rChip.mText, 4000.0f, &rChip.mText->mSize.width, &height);
        if (!arrow || !inkOffset(rChip.mText, &rPooled.mInkX, &rPooled.mInkY)) {
            rPooled.mInkX = 0.0f;
            rPooled.mInkY = 0.0f;
        }
    }
    rChip.mRoot->SetVisible(true);
    return rPooled;
}

// The rows' own buttons on the rows in sight, the pool's going to them in order: a row of buttons right to left from
// the row's end, a setting's arrows, a slider's knob. Focused: the selected row's selected button, what is under the
// pointer, what it holds.
void NebulaSettingsScreen::placeRowButtons(const NebulaSettingsView& rView) {
    const bool asking = mQuestionShown >= 0;
    s32 next = 0;
    for (s32 i = 0; i < static_cast< s32 >(rView.total_rows); i++) {
        Row& rRow = mRows[i];
        const f32 y = rRow.mRoot->mTranslate.y;
        if (!rRow.mRoot->IsVisible()) {
            continue;
        }
        const s32 row = i;
        const NebulaSettingsRow& rData = rView.rows[i];
        for (s32 a = 0; a < 2 && rRow.mArrows; a++) {
            // The ▲ turned to point back or on, its ink centred on the button whatever the glyph's own margins: turned
            // a quarter (90 back, -90 on), its ink's offset turns with it.
            const s32 button = a == 0 ? ChipLess : ChipMore;
            RowChip& rPooled = useRowChip(next++, i, button, 0, u"\u25B2");
            const s32 direction = a == 0 ? -1 : 1;
            const bool focused = !asking && ((row == mHoverRow && mHoverArrow == direction) ||
                                             (row == static_cast< s32 >(rView.selected) && mHeldArrow == direction));
            placeRowBox(rPooled, rRow.mArrowX[a], y, cArrowWidth, cRowButtonHeight, cRowButtonRadius, focused, false);
            const f32 dx = rPooled.mInkX;
            const f32 dy = cCapLift * cArrowSize + rPooled.mInkY;
            const f32 turn = a == 0 ? 1.0f : -1.0f;
            rPooled.mChip.mText->mTranslate = Vec3f(turn * dy, -turn * dx, 0.0f);
        }
        if (rRow.mSliderRight > rRow.mSliderLeft) {
            RowChip& rPooled = useRowChip(next++, i, ChipKnob, 0, u"");
            const f32 at = static_cast< f32 >(rData.slider_at) / static_cast< f32 >(rData.slider_steps > 0 ? rData.slider_steps : 1);
            const bool focused = !asking && (row == mSliderRow || (row == mHoverRow && mHoverSlider));
            placeRowBox(rPooled, rRow.mSliderLeft + (rRow.mSliderRight - rRow.mSliderLeft) * at, y, cKnobSize, cKnobSize, cKnobSize * 0.5f,
                        focused, false);
        }
        f32 x = mRight - 8.0f;
        for (s32 b = rRow.mButtonCount - 1; b >= 0; b--) {
            RowChip& rPooled = useRowChip(next++, i, b, rRow.mButtonsVersion, rRow.mButtons[b].mWord.mItems);
            Chip& rChip = rPooled.mChip;
            const f32 word = rChip.mText->mSize.width;
            const f32 width = word + 40.0f > cRowButtonMin ? word + 40.0f : cRowButtonMin;
            x -= width * 0.5f;
            rRow.mButtons[b].mX = x;
            rRow.mButtons[b].mWidth = width;
            const bool focused = (row == static_cast< s32 >(rView.selected) && b == static_cast< s32 >(rView.selected_button) &&
                                  !mPointerMode) ||
                                 (row == mHoverRow && b == mHoverRowButton);
            const bool off = rRow.mButtons[b].mOff;
            placeRowBox(rPooled, x, y, width, cRowButtonHeight, cRowButtonRadius, focused && !off, off);
            setTextColor(rChip.mText, off ? cDisabled : cButtonText, off ? cDisabled : cButtonText);
            rChip.mText->mTranslate = Vec3f(0.0f, textCentre(0.0f, cRowButtonSize), 0.0f);
            x -= width * 0.5f + cRowButtonGap;
        }
    }
    for (; next < mRowChipCount; next++) {
        mRowChips[next].mRow = -1;
        mRowChips[next].mChip.mRoot->SetVisible(false);
    }
}

// The page's rows, scrolled so that the selection keeps a row of room above and below it (a row the pointer picked is
// under it already, and the list holds still under the pointer), the highlight on the selection, the scroll bar.
void NebulaSettingsScreen::placeRows(const NebulaSettingsView& rView) {
    const bool asking = mQuestionShown >= 0;
    mClipTop = mListTop + 2.0f;
    mClipBottom = mListBottom - 2.0f;
    const f32 listHeight = mListTop - mListBottom;
    const f32 content = rView.total_rows * cRowHeight;
    const f32 maxScroll = content > listHeight ? content - listHeight : 0.0f;
    const f32 selectedTop = rView.selected * cRowHeight;
    // A row kept on each side of the selection, when the list has room for all three.
    const f32 room = mPointerMode || listHeight < cRowHeight * 3.0f ? 0.0f : cRowHeight;
    if (!mScrollDrag && static_cast< s32 >(rView.selected) != mDraggedAt) {
        mDraggedAt = -1;
        if (selectedTop - room < mScrollTarget) {
            mScrollTarget = selectedTop - room;
        }
        if (selectedTop + cRowHeight + room > mScrollTarget + listHeight) {
            mScrollTarget = selectedTop + cRowHeight + room - listHeight;
        }
    }
    mScrollTarget = clamp(mScrollTarget, 0.0f, maxScroll);
    mScroll = mFrame <= 1 || mScrollDrag ? mScrollTarget : mScroll + (mScrollTarget - mScroll) * 0.35f;
    for (s32 i = 0; i < static_cast< s32 >(rView.total_rows); i++) {
        Row& rRow = mRows[i];
        const NebulaSettingsRow& rData = rView.rows[i];
        const bool heading = (rData.flags & NEBULA_SETTINGS_ROW_HEADING) != 0;
        const s32 row = i;
        const f32 y = mListTop - row * cRowHeight - cRowHeight * 0.5f + mScroll;
        rRow.mRoot->mTranslate = Vec3f(0.0f, y, 0.0f);
        const bool inView = y < mListTop + cRowHeight && y > mListBottom - cRowHeight;
        rRow.mRoot->SetVisible(inView);
        if (!inView) {
            continue;  // out of sight: nothing of it is laid out until it scrolls in
        }
        const f32 labelX = mLeft + (rRow.mIndented ? 30.0f : heading ? 2.0f : 16.0f);
        rRow.mLabel->mScale = Vec2f(1.0f, 1.0f);
        rRow.mLabel->mTranslate = Vec3f(labelX, textCentre(heading ? -2.0f : 0.0f, heading ? cHeadingSize : cTextSize), 0.0f);
        rRow.mArrows = false;
        rRow.mSliderLeft = 0.0f;
        rRow.mSliderRight = 0.0f;
        rRow.mSliderTrack->SetVisible(false);
        rRow.mSliderFill->SetVisible(false);
        if (heading) {
            const f32 start = labelX + rRow.mLabelWidth + 10.0f;
            shape(rRow.mRule, (start + mRight) * 0.5f, -2.0f, mRight > start ? mRight - start : 0.0f, 2.0f, 1.0f);
            continue;
        }
        // The row as a button: the selected one (the keys' or under the pointer) white and ringed in the game's blue; a
        // row that only shows something fainter.
        const bool selected = row == static_cast< s32 >(rView.selected) && !asking;
        const bool fixed = (rData.flags & (NEBULA_SETTINGS_ROW_CHANGES | NEBULA_SETTINGS_ROW_BUTTONS)) == 0;
        // The others' ring in their fill: their whole box shows (the ring's colour is near the window's).
        const u32 fill = selected ? cHighlightFill : fixed ? cFrameFixed : cFrameFill;
        paint(rRow.mFrame, fill, selected ? cHighlightRing : fill);
        shape(rRow.mFrame, (mLeft + mRight) * 0.5f, 0.0f, mRight - mLeft, cRowHeight - cRowGap, 12.0f);
        // A setting's arrows around its value, or its slider with its value after it, at the row's end in the same
        // place on every row; the label narrowed to the room left.
        const f32 end = mRight - 6.0f;
        const f32 box = clamp((mRight - mLeft) * 0.3f, cValueMin, cValueMax);
        const f32 start = end - cArrowWidth * 2.0f - cRowButtonGap * 2.0f - box;
        f32 labelRoom = 4000.0f;  // a row's own value or buttons leave it whole
        if (hasArrows(rData)) {
            rRow.mArrows = true;
            rRow.mArrowX[0] = start + cArrowWidth * 0.5f;
            rRow.mArrowX[1] = end - cArrowWidth * 0.5f;
            const f32 room = box - 12.0f;
            const f32 squeeze = rRow.mValueWidth > room && rRow.mValueWidth > 0.0f ? clamp(room / rRow.mValueWidth, 0.55f, 1.0f) : 1.0f;
            rRow.mValue->mScale = Vec2f(squeeze, 1.0f);
            rRow.mValue->mTranslate =
                Vec3f(end - cArrowWidth - cRowButtonGap - box * 0.5f, textCentre(cEdgeDrop * 0.5f, cTextSize), 0.0f);
            labelRoom = start - labelX - 12.0f;
        } else if (hasSlider(rData)) {
            rRow.mSliderLeft = start + cKnobSize * 0.5f;
            rRow.mSliderRight = mValueRight - cSliderValue - 12.0f;
            const f32 at = static_cast< f32 >(rData.slider_at) / static_cast< f32 >(rData.slider_steps);
            const f32 knob = rRow.mSliderLeft + (rRow.mSliderRight - rRow.mSliderLeft) * at;
            const f32 trackLeft = rRow.mSliderLeft - cSliderHeight * 0.5f;
            // On the knob's face, as the arrows' values are.
            shape(rRow.mSliderTrack, (trackLeft + rRow.mSliderRight + cSliderHeight * 0.5f) * 0.5f, cEdgeDrop * 0.5f,
                  rRow.mSliderRight - rRow.mSliderLeft + cSliderHeight, cSliderHeight, cSliderHeight * 0.5f);
            shape(rRow.mSliderFill, (trackLeft + knob) * 0.5f, cEdgeDrop * 0.5f, knob - trackLeft, cSliderHeight, cSliderHeight * 0.5f);
            rRow.mSliderTrack->SetVisible(true);
            rRow.mSliderFill->SetVisible(true);
            const f32 squeeze = rRow.mValueWidth > cSliderValue && rRow.mValueWidth > 0.0f ? clamp(cSliderValue / rRow.mValueWidth, 0.55f, 1.0f) : 1.0f;
            rRow.mValue->mScale = Vec2f(squeeze, 1.0f);
            rRow.mValue->mTranslate = Vec3f(mValueRight, textCentre(cEdgeDrop * 0.5f, cTextSize), 0.0f);
            labelRoom = start - labelX - 12.0f;
        } else {
            // A value too long for the room its label leaves is narrowed to fit.
            const f32 room = mValueRight - labelX - rRow.mLabelWidth - 24.0f;
            const f32 squeeze = rRow.mValueWidth > room && rRow.mValueWidth > 0.0f ? clamp(room / rRow.mValueWidth, 0.55f, 1.0f) : 1.0f;
            rRow.mValue->mScale = Vec2f(squeeze, 1.0f);
            rRow.mValue->mTranslate = Vec3f(mValueRight, textCentre(0.0f, cTextSize), 0.0f);
        }
        const f32 label = rRow.mLabelWidth;
        rRow.mLabel->mScale = Vec2f(label > labelRoom && label > 0.0f ? clamp(labelRoom / label, 0.5f, 1.0f) : 1.0f, 1.0f);
    }

    placeRowButtons(rView);
    mHighlight->SetVisible(false);

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
}

// Where each piece is this frame: the drop, the pages' buttons, the rows or the MODS page's lists, the footer, the
// question.
void NebulaSettingsScreen::place(const NebulaSettingsView& rView) {
    mDim->mAlpha = toAlpha(clamp(mOpen * 1.5f, 0.0f, 1.0f));
    const f32 dropHeight = mHome ? mQuestionPanel->mSize.height : mPanelHeight;
    mMenu->mTranslate = Vec3f(0.0f, (1.0f - easeOut(mOpen)) * (mHeight * 0.5f + dropHeight * 0.5f + 16.0f), 0.0f);
    if (mHome) {
        placeHome();
        return;
    }
    // The on-screen controls are arranged over the picture: their window alone, the page hidden.
    const bool arranging = rView.status == NEBULA_SETTINGS_STATUS_ARRANGE;
    mPage->SetVisible(!arranging);
    mTitle->mTranslate = Vec3f(mLeft + 2.0f, textCentre(mTitleY, cTitleSize), 0.0f);

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
        rTab.mRoot->mTranslate = Vec3f(rTab.mX, mTabY, 0.0f);
        rTab.mRoot->mScale = Vec2f(grow, grow);
        rTab.mLayers[LayerShine]->SetVisible(rTab.mFocusLevel > 0.02f);
        rTab.mLayers[LayerShine]->mAlpha = toAlpha(rTab.mFocusLevel * 0.7f);
        const f32 room = tabWidth - 22.0f;
        const f32 word = mTabWordWidths[i];
        rTab.mText->mScale = Vec2f(word > room && word > 0.0f ? room / word : 1.0f, 1.0f);
        rTab.mText->mTranslate = Vec3f(0.0f, textCentre(0.0f, cTabSize), 0.0f);
        setTextColor(rTab.mText, active ? cAccent : cButtonText, active ? cAccent : cButtonText);
    }

    const bool asking = mQuestionShown >= 0;
    const bool mods = hasModLists(rView);
    showMods(mods);
    if (mods) {
        placeMods(rView);
    } else {
        placeRows(rView);
    }

    // The footer: Apply and Back for the pointer on the right; the line under the page and the hints in the room left,
    // narrowed when a language runs long.
    f32 buttonWidths[FooterNum];
    f32 buttonsWidth = 0.0f;
    for (s32 i = 0; i < FooterNum; i++) {
        const Button& rButton = mFooter[i];
        buttonWidths[i] = rButton.mKey.mWidth + cKeyGap + rButton.mWordWidth + 44.0f;
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
    mStatus->mScale = Vec2f(statusScale, 1.0f);
    mStatus->mTranslate = Vec3f(mLeft + 2.0f, textCentre(mStatusY, cFooterSize), 0.0f);
    // The hints' keys keep their size; their words and the room between them narrow when a language runs long.
    f32 keysWidth = 0.0f;
    f32 wordsWidth = 0.0f;
    for (s32 i = 0; i < cHintsMax; i++) {
        keysWidth += mHints[i].mKey.mWidth;
        wordsWidth += cKeyGap + mHints[i].mWordWidth + (i > 0 ? cHintGap : 0.0f);
    }
    const f32 wordsRoom = textRoom - keysWidth;
    const f32 hintScale = wordsWidth > wordsRoom && wordsWidth > 0.0f ? clamp(wordsRoom / wordsWidth, 0.6f, 1.0f) : 1.0f;
    x = mLeft + 2.0f;
    for (s32 i = 0; i < cHintsMax; i++) {
        Hint& rHint = mHints[i];
        rHint.mWord->mScale = Vec2f(hintScale, 1.0f);
        placeKeyCap(rHint.mKey, x, mHintsY);
        x += rHint.mKey.mWidth + cKeyGap * hintScale;
        rHint.mWord->mTranslate = Vec3f(x, textCentre(mHintsY, cFooterSize), 0.0f);
        x += (rHint.mWordWidth + cHintGap) * hintScale;
    }

    // The question, centred on the window, its answers along its foot; Pair Joy-Con's picture over its text.
    if (asking) {
        mQuestionDim->mAlpha = arranging ? 0 : 96;
        // Pair Joy-Con: from the top, the letters, the halves, the text and the answer, each as far from the next as
        // the answer is from the window's ring (the text by its capitals' tops and its baseline).
        const bool joycon = rView.status == NEBULA_SETTINGS_STATUS_JOYCON;
        const f32 picture = cCapHeight * cTextSize + cJoyConLetterGap +
                            (NebulaIcons::cJoyConShoulder + NebulaIcons::cJoyConBody[1]) * cJoyConUnit;
        const f32 gap = cQuestionMargin - cQuestionRing;
        const bool about = rView.status == NEBULA_SETTINGS_STATUS_MOD_ABOUT;
        const bool license = rView.status == NEBULA_SETTINGS_STATUS_LICENSE || about;
        const bool bar = rView.status == NEBULA_SETTINGS_STATUS_DLSS_INSTALLING || rView.status == NEBULA_SETTINGS_STATUS_XESS_INSTALLING;
        const f32 panelWidth = license ? mPanelWidth - cLicenseInset * 2.0f : cQuestionWidth;
        const f32 height = joycon    ? cQuestionMargin * 2.0f + gap * 2.0f + picture + mQuestionInk + cButtonHeight
                           : license ? mPanelHeight - cLicenseInset * 2.0f
                                     : cQuestionHeight;
        mQuestionPanel->mSize = Ui::Size(panelWidth, height);
        mQuestionShadow->mSize = Ui::Size(panelWidth, height);
        const f32 answersY = -height * 0.5f + cQuestionMargin + cButtonHeight * 0.5f;
        const f32 top = height * 0.5f - cQuestionMargin;
        const f32 textY = joycon ? top - picture - gap - mQuestionInk * 0.5f
                                 : (height * 0.5f + answersY + cButtonHeight * 0.5f) * 0.5f + (bar ? cSliderHeight * 1.5f : 0.0f);
        if (license) {
            mQuestionText->mTranslate = Vec3f(-panelWidth * 0.5f + cQuestionMargin, top - mQuestionInk * 0.5f, 0.0f);
        } else {
            mQuestionText->mTranslate = Vec3f(0.0f, textCentre(textY, cTextSize + 1.0f), 0.0f);
        }
        if (joycon) {
            placeJoyCon(top);
        }
        // Between the page's text and the answers.
        placeModPicture(about, panelWidth - cQuestionMargin * 2.0f, top - mQuestionInk - gap,
                        answersY + cButtonHeight * 0.5f + gap);
        if (bar) {
            const f32 barWidth = cQuestionWidth - cQuestionMargin * 2.0f;
            const f32 barY = textY - (cTextSize + 1.0f) * 0.5f - cSliderHeight * 2.0f;
            const f32 percent = clamp(static_cast< f32 >(rView.status_number), 0.0f, 100.0f) / 100.0f;
            const f32 fill = barWidth * percent > cSliderHeight ? barWidth * percent : cSliderHeight;
            shape(mProgressTrack, 0.0f, barY, barWidth, cSliderHeight, cSliderHeight * 0.5f);
            shape(mProgressFill, -barWidth * 0.5f + fill * 0.5f, barY, fill, cSliderHeight, cSliderHeight * 0.5f);
        }
        const f32 width = (panelWidth - cQuestionMargin * 2.0f - cButtonGap * (mChoiceCount - 1)) / (mChoiceCount > 0 ? mChoiceCount : 1);
        for (s32 i = 0; i < mChoiceCount; i++) {
            placeButton(mChoices[i], -panelWidth * 0.5f + cQuestionMargin + width * 0.5f + i * (width + cButtonGap), answersY, width);
        }
    }
}

// The ABOUT window's picture from the host's view, copied into the game's own texture when it is another one; none on
// another screen. Read while the host's pointer is good (the view of this step).
void NebulaSettingsScreen::takeModPicture(const NebulaSettingsView& rView) {
    const bool about = rView.open && rView.status == NEBULA_SETTINGS_STATUS_MOD_ABOUT;
    const u8* const pFrom = about ? rView.picture : nullptr;
    const u32 width = pFrom != nullptr ? rView.picture_width : 0;
    const u32 height = pFrom != nullptr ? rView.picture_height : 0;
    if (pFrom == mModPictureFrom && width == mModPictureWidth && height == mModPictureHeight) {
        return;
    }
    mModPictureFrom = pFrom;
    mModPictureWidth = width;
    mModPictureHeight = height;
    mModPictureReady = false;
    if (pFrom == nullptr || width == 0 || height == 0 || width > cModPictureSide || height > cModPictureSide) {
        return;
    }
    const u32 room = NEBULA_TEXTURE_HEADER + cModPictureSide * cModPictureSide * 4;
    if (mModPictureRecord == nullptr) {
        mModPictureRecord = static_cast< u8* >(NebulaArena::alloc(room, 16, NebulaArena::root()));
        if (mModPictureRecord == nullptr) {
            return;
        }
    }
    nebula_texture_init(mModPictureRecord, width, height, 1, NEBULA_TEXTURE_RGBA8, 0);
    memcpy(mModPictureRecord + NEBULA_TEXTURE_HEADER, pFrom, static_cast< size_t >(width) * height * 4);
    RenderTexture::rewritten(mModPictureRecord, room);
    mModPictureMap.SetImage(mModPictureRecord);
    mModPictureMap.SetSize(static_cast< u16 >(width), static_cast< u16 >(height));
    mModPictureMap.SetTexelFormat(Ui::TexMap::TEXEL_RGBA8);
    mModPictureMap.SetWrapMode(NEBULA_WRAP_CLAMP, NEBULA_WRAP_CLAMP);
    mModPictureMap.SetMipMap(false);
    mModPictureMap.SetFilter(NEBULA_FILTER_LINEAR, NEBULA_FILTER_LINEAR);
    mModPictureMap.SetLOD(0.0f, 0.0f);
    mModPictureReady = true;
    mModPictureNew = true;
}

// The ABOUT window's picture centred in the room from `top` down to `bottom`, `width` wide, its shape kept; a small one
// grows to three of the screen's pixels a texel at most.
void NebulaSettingsScreen::placeModPicture(bool shown, f32 width, f32 top, f32 bottom) {
    shown = shown && mModPictureReady && top - bottom > 1.0f && width > 1.0f;
    if (shown && mModPicture == nullptr) {
        mModPicture = makePicture(mQuestion, "NsModPic", &mModPictureMap);
        if (mModPicture == nullptr) {
            return;
        }
        mModPicture->mBasePosition = 4;
        mModPictureNew = false;
    }
    if (mModPicture == nullptr) {
        return;
    }
    mModPicture->SetVisible(shown);
    if (!shown) {
        return;
    }
    if (mModPictureNew) {
        mModPicture->GetMaterial()->SetTexture(0, mModPictureMap);
        mModPictureNew = false;
    }
    const u32 lines = NebulaHost::layer2DLines();
    const f32 pixels = (lines != 0u ? static_cast< f32 >(lines) : 1368.0f) / mHeight;  // the screen's pixels a unit
    const f32 w = static_cast< f32 >(mModPictureWidth);
    const f32 h = static_cast< f32 >(mModPictureHeight);
    f32 scale = width / w < (top - bottom) / h ? width / w : (top - bottom) / h;
    if (scale > 3.0f / pixels) {
        scale = 3.0f / pixels;
    }
    mModPicture->mSize = Ui::Size(w * scale, h * scale);
    mModPicture->mTranslate = Vec3f(0.0f, (top + bottom) * 0.5f, 0.0f);
}

// The Pair Joy-Con picture under `top` (the letters' cap tops): the halves side by side, their rails cJoyConGap apart,
// the letters L and R over their shoulders. Each half's picture is painted at the 2D layer's pixels per unit
// (NebulaIcons) and placed on its pixels. While the shoulders are to be held the letters bob; held, a shoulder clicks
// down and its letter takes the half's colour. A button pressed on a half pops it a little; paired, a chime.
void NebulaSettingsScreen::placeJoyCon(f32 top) {
    const s32 bits = mJoyConBits;
    const f32 k = cJoyConUnit;
    const bool paired = (bits & NEBULA_JOYCON_PAIRED) != 0;
    const bool both = (bits & (NEBULA_JOYCON_LEFT | NEBULA_JOYCON_RIGHT)) == (NEBULA_JOYCON_LEFT | NEBULA_JOYCON_RIGHT);
    if (paired && !mJoyConPaired) {
        MR::startSystemSE("SE_SY_TALK_OK");
    }
    mJoyConPaired = paired;
    const f32 phase = static_cast< f32 >(mFrame % 72) / 72.0f;
    const f32 wave = 1.0f - __builtin_fabsf(phase * 2.0f - 1.0f);  // 0..1..0 over 1.2 s
    f32 pixelsX;
    f32 pixelsY;
    paintJoyCon(cJoyConPaintShown, &pixelsX, &pixelsY);
    f32 box[2];
    NebulaIcons::joyConPictureSize(&box[0], &box[1]);
    // On the layer's pixels: the pictures' sides are whole texels from their centres.
    const auto snap = [](f32 value, f32 pixels) { return __builtin_floorf(value * pixels + 0.5f) / pixels; };
    const f32 letterY = top - cCapHeight * cTextSize * 0.5f;
    const f32 bodyY = top - cCapHeight * cTextSize - cJoyConLetterGap -
                      (NebulaIcons::cJoyConShoulder + NebulaIcons::cJoyConBody[1] * 0.5f) * k;
    for (s32 half = 0; half < 2; half++) {
        JoyConHalf& rHalf = mHalves[half];
        const bool left = half == 0;
        const bool connected = (bits & (left ? NEBULA_JOYCON_LEFT : NEBULA_JOYCON_RIGHT)) != 0;
        const bool held = (bits & (left ? NEBULA_JOYCON_LEFT_HELD : NEBULA_JOYCON_RIGHT_HELD)) != 0;
        const s32 look = !connected ? NebulaIcons::JoyConAway : held ? NebulaIcons::JoyConHeld : NebulaIcons::JoyConShown;
        // A press on the half: a pop (none for the presses counted before the screen saw them).
        const s32 presses = bits >> (left ? NEBULA_JOYCON_LEFT_PRESSES_SHIFT : NEBULA_JOYCON_RIGHT_PRESSES_SHIFT) & 15;
        if (rHalf.mPresses >= 0 && presses != rHalf.mPresses && connected) {
            rHalf.mPop = 1.0f;
        }
        rHalf.mPresses = presses;
        rHalf.mPop = clamp(rHalf.mPop - 1.0f / 7.0f, 0.0f, 1.0f);
        // Nothing of the half until its pictures are painted (the Controls page paints them ahead).
        Ui::TexMap* pMap = NebulaIcons::joyConPicture(half, look);
        rHalf.mLetter->SetVisible(pMap != nullptr);
        if (pMap == nullptr) {
            if (rHalf.mPicture != nullptr) {
                rHalf.mPicture->SetVisible(false);
            }
            continue;
        }
        if (rHalf.mPicture == nullptr) {
            char name[16];
            nebula_snprintf(name, sizeof(name), "NsJc%dP", static_cast< int >(half));
            rHalf.mPicture = makePicture(mJoyCon, name, pMap);
            if (rHalf.mPicture == nullptr) {
                continue;
            }
            rHalf.mPicture->mBasePosition = 4;
        } else if (pMap != rHalf.mMap) {
            rHalf.mPicture->GetMaterial()->SetTexture(0, *pMap);
        }
        rHalf.mMap = pMap;
        rHalf.mPicture->SetVisible(true);
        // A pop swells it a touch and lifts it.
        const f32 pop = rHalf.mPop * rHalf.mPop;
        const f32 lift = pop * 0.3f * k;
        const f32 x = (left ? -cJoyConCentre : cJoyConCentre) * k;
        rHalf.mPicture->mSize = Ui::Size(box[0] * k, box[1] * k);
        rHalf.mPicture->mScale = Vec2f(1.0f + pop * 0.02f, 1.0f + pop * 0.02f);
        rHalf.mPicture->mTranslate = Vec3f(snap(x, pixelsX), snap(bodyY, pixelsY) + lift, 0.0f);
        // The letters bob down toward their shoulders (never into the window's margin).
        const f32 bob = both && !paired && !held ? wave * 0.7f * k : 0.0f;
        rHalf.mLetter->mTranslate = Vec3f(x + (left ? -cJoyConLetter : cJoyConLetter) * k,
                                                     textCentre(letterY - bob + lift, cTextSize), 0.0f);
        const u32 letter = !connected ? cDisabled : held || paired ? NebulaIcons::cJoyConColours[half] : cText;
        setTextColor(rHalf.mLetter, letter, letter);
    }
}

// The Pair Joy-Con pictures at the 2D layer's density, painted for about `budgetMicros` this frame; that density in
// pixels per menu unit.
void NebulaSettingsScreen::paintJoyCon(u32 budgetMicros, f32* pPixelsX, f32* pPixelsY) const {
    const u32 lines = NebulaHost::layer2DLines();
    const f32 height = lines != 0u ? static_cast< f32 >(lines) : 1368.0f;
    *pPixelsX = height * NebulaFrameClock::display().content_aspect / mWidth;
    *pPixelsY = height / mHeight;
    NebulaIcons::setJoyConDensity(*pPixelsX * cJoyConUnit, *pPixelsY * cJoyConUnit);
    (void)NebulaIcons::paintJoyConAhead(budgetMicros);
}

// The HOME Menu's window centred on the screen: the title over the rows, or the question over its answers.
void NebulaSettingsScreen::placeHome() {
    mQuestionDim->mAlpha = 0;
    const f32 top = mQuestionPanel->mSize.height * 0.5f;
    if ((mHomeView >> 8 & 0xFF) == 0) {
        const f32 titleY = top - cPad - cTitleSize * 0.5f;
        mQuestionText->mTranslate = Vec3f(0.0f, textCentre(titleY, cTitleSize), 0.0f);
        const f32 firstY = titleY - cTitleSize * 0.5f - 18.0f - cButtonHeight * 0.5f;
        for (s32 i = 0; i < mChoiceCount; i++) {
            placeButton(mChoices[i], 0.0f, firstY - i * (cButtonHeight + cHomeGap), cHomeButtonWidth);
        }
        return;
    }
    const f32 answersY = -top + 22.0f + cButtonHeight * 0.5f;
    const f32 textY = (top + answersY + cButtonHeight * 0.5f) * 0.5f;
    mQuestionText->mTranslate = Vec3f(0.0f, textCentre(textY, cTextSize + 1.0f), 0.0f);
    const f32 width = (cQuestionWidth - 44.0f - cButtonGap * (mChoiceCount - 1)) / mChoiceCount;
    for (s32 i = 0; i < mChoiceCount; i++) {
        placeButton(mChoices[i], -cQuestionWidth * 0.5f + 22.0f + width * 0.5f + i * (width + cButtonGap), answersY, width);
    }
}

// A frame: open or close as the host's menus do (dropping in, rising away), the pointer, the sounds of what changed.
// The settings menu shows when both would (the host never opens both).
void NebulaSettingsScreen::step(const NebulaSettingsView& rView, s32 homeView) {
    if (!mBuilt) {
        return;
    }
    const bool wanted = rView.open || homeView >= 0;
    const bool home = !rView.open;
    if (wanted && (!isShown() || mClosing || home != mHome)) {
        if (!isShown() || home != mHome) {
            appear();
            mHome = home;
            mHomeView = -1;
            mFocusButton = nullptr;
            mPage->SetVisible(!home);
            mFrame = 0;
            mScroll = 0.0f;
            mScrollTarget = 0.0f;
            mPointerMode = false;
            mPointerRow = -1;
            mPressing = false;
            mScrollDrag = false;
            mDraggedAt = -1;
            mHeldArrow = 0;
            mSliderRow = -1;
            mHoverButton = nullptr;
            mHoverTab = -1;
            mQuestionShown = -1;
            mQuestion->SetVisible(false);
            for (s32 i = 0; i < mTabsMade; i++) {
                mTabs[i].mFocusLevel = 0.0f;
            }
            mFooter[FooterApply].mChip.mFocusLevel = 0.0f;
            mFooter[FooterBack].mChip.mFocusLevel = 0.0f;
            arrange();
            mShown.copyFrom(rView);
            if (home) {
                fillHome(homeView);
            } else {
                fillTexts(mShown, nullptr);
                if (!mBuilt) {
                    return;
                }
            }
        }
        mClosing = false;
        if (!home) {
            MR::startSystemSE("SE_SY_PAUSE_ON");
        }
    } else if (!wanted && isShown() && !mClosing) {
        mClosing = true;
        if (!mHome) {
            MR::startSystemSE("SE_SY_PAUSE_OFF");
        }
    }
    if (mClosing) {
        mOpen = clamp(mOpen - 1.0f / cDropFrames, 0.0f, 1.0f);
        if (mOpen <= 0.0f) {
            mClosing = false;
            kill();
            return;
        }
    } else if (wanted) {
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
    if (scaleX != mScaleX || __builtin_fabsf(width - mScreenWidth) > 0.5f || __builtin_fabsf(height - mScreenHeight) > 0.5f ||
        static_cast< s32 >(NebulaHost::option(NEBULA_OPTION_MENU_TEXT_SIZE)) != mTextSizeChosen ||
        static_cast< s32 >(NebulaHost::option(NEBULA_OPTION_WINDOW_WIDTH_DP)) != mWindowDp) {
        arrange();
    }
    if (!mHome && rView.page == rView.controls_page && rView.status != NEBULA_SETTINGS_STATUS_JOYCON) {
        f32 pixelsX;
        f32 pixelsY;
        paintJoyCon(cJoyConPaintAhead, &pixelsX, &pixelsY);  // the Pair Joy-Con screen's pictures, ready before it opens
    }
    // The Pair Joy-Con screen's halves change with every press: its picture follows them each frame, its text only what
    // it says (which halves are here, paired).
    NebulaSettingsView& view = mRead;
    memcpy(&view, &rView, sizeof(view));
    takeModPicture(rView);
    mJoyConBits = view.status == NEBULA_SETTINGS_STATUS_JOYCON ? view.status_number : 0;
    if (view.status == NEBULA_SETTINGS_STATUS_JOYCON) {
        view.status_number &= NEBULA_JOYCON_LEFT | NEBULA_JOYCON_RIGHT | NEBULA_JOYCON_PAIRED;
    }
    if (mHome) {
        if (homeView >= 0 && homeView != mHomeView) {
            fillHome(homeView);
        }
    } else if (view.open && (view.open != mShown.open || view.revision != mShown.revision)) {
        // What changed, as the game's menus sound it.
        if (rView.page != mShown.page) {
            MR::startSystemSE("SE_SY_GALAMAP_PAGE_CHANGE");
            mScrollTarget = 0.0f;
            mFrame = 1;  // the new page's rows and highlight start in place
            // What the pointer held belongs to the old page.
            mHeldArrow = 0;
            mSliderRow = -1;
            mPressing = false;
            mScrollDrag = false;
            mDraggedAt = -1;
            mDragRow = -1;
            mDragging = false;
        } else if (rView.selected != mShown.selected) {
            // Moving through the rows is silent; a change or a press sounds.
            if (static_cast< s32 >(rView.selected) != mPointerRow) {
                mPointerMode = false;  // the keys or a controller moved it: the pointer waits until it moves
            }
        } else {
            const NebulaSettingsRow* pNow = rowAt(rView, rView.selected);
            const NebulaSettingsRow* pBefore = rowAt(mShown, rView.selected);
            if (pNow != nullptr && pBefore != nullptr && !MR::isEqualString(pNow->value, pBefore->value)) {
                MR::startSystemSE("SE_SY_SELECT_PAUSE_ITEM");
            }
        }
        if (rView.status == NEBULA_SETTINGS_STATUS_APPLIED && mShown.status != NEBULA_SETTINGS_STATUS_APPLIED) {
            MR::startSystemSE("SE_SY_TALK_OK");
        }
        fillTexts(view, &mShown);
        if (!mBuilt) {
            return;
        }
        mShown.copyFrom(view);
    }
    if (wanted && !mClosing) {
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
    if (!mHome && mPage->IsVisible()) {
        drawClipped(mList, getLayoutManager()->mDrawInfo, mLeft - 4.0f, mRight + 18.0f, mClipTop, mClipBottom);
    }
    if (asking) {
        mQuestion->SetVisible(true);
        mQuestion->Draw(getLayoutManager()->mDrawInfo);
    }
}

namespace NebulaSettingsMenu {
    void init() {
        // The screen lasts as long as the process: made in the permanent arena, whatever is current.
        NebulaArena::CurrentScope home(Arenas::permanent());
        sScreen = new NebulaSettingsScreen();
        sScreen->initWithoutIter();
    }

    void update() {
        // Unbuilt: the game's own layouts lack a pane (a damaged store; init logged it). The game runs on.
        if (sScreen == nullptr || !sScreen->isBuilt()) {
            return;
        }
        NebulaHost::settingsView(&sView);
        // Not during the boot (the logos): asked for there, it closes at once. The title and the file select open it
        // (F1, the file select's Settings button), over everything they draw.
        if (sView.open && !sScreen->isShown() && !GameSystemFunction::isOccurredSystemWarning() &&
            MR::isCurrentScene(SceneKind_Boot)) {
            NebulaHost::settingsInput(NEBULA_SETTINGS_CLOSE, 0);
            sView.open = 0;
        }
        sScreen->step(sView, NebulaHomeMenu::view());
        // The whole game waits from the first frame of the drop to the last of the rise. Its sound fades and the music
        // stops as under the pause menu, and both come back as they were. The HOME Menu holds the game itself
        // (HomeButtonLayout).
        const bool freezing = sView.open || (sScreen->isShown() && !sScreen->isHome());
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
        NebulaHost::settingsInput(NEBULA_SETTINGS_OPEN, 0);
    }
};  // namespace NebulaSettingsMenu
