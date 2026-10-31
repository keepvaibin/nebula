#include "Game/Screen/NebulaAchievementBoard.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>

#ifdef TARGET_PC
#include "Game/Screen/NebulaMenuFocus.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/GalaxyMapBackground.hpp"
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/Screen/LayoutManager.hpp"
#include "Game/Screen/NebulaPaneKit.hpp"
#include "Game/Screen/NebulaUi.hpp"
#include "Game/System/LayoutHolder.hpp"
#include "Game/System/NebulaAchievements.hpp"
#include "Game/System/NebulaText.hpp"
#include "Game/Util/GamePadUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nw4r/lyt/layout.h>
#include <nw4r/lyt/material.h>
#include <nw4r/lyt/pane.h>
#include <nw4r/lyt/picture.h>
#include <nw4r/lyt/textBox.h>
#include <nw4r/lyt/window.h>
#include <nw4r/ut/Font.h>
#include <revolution/gx.h>
#include <revolution/os.h>
#include "Game/System/NebulaInput.hpp"
#include "Game/System/NebulaHost.hpp"

namespace NrvNebulaAchievementBoard {
    NEW_NERVE(NebulaAchievementBoardNrvAppear, NebulaAchievementBoard, Appear);
    NEW_NERVE(NebulaAchievementBoardNrvWait, NebulaAchievementBoard, Wait);
    NEW_NERVE(NebulaAchievementBoardNrvDisappear, NebulaAchievementBoard, Disappear);
};  // namespace NrvNebulaAchievementBoard

using namespace NebulaUi;

namespace {

    enum { TabAll, TabStory, TabChallenges, TabExtras, TabStats, TabNum };
    enum { FilterAll, FilterUnlocked, FilterLocked, FilterNum };
    enum { ZoneTabs, ZoneFilter, ZoneList };

    // Board units: the layout's units made square. The game draws its 2D for a 16:9 picture on a canvas three quarters
    // as wide as it is shown (MR::isScreen16Per9), so the board's root pane narrows by 0.75 there and every size here
    // is what it looks like. The screen is 456 high at 16:9 and wider, taller below 16:9.
    const f32 cMargin = 16.0f;   // the panel from the screen's sides and top
    const f32 cFooter = 50.0f;   // below the panel: Back and the pad's guide
    const f32 cPad = 18.0f;      // inside the panel
    const f32 cHeaderHeight = 38.0f;
    const f32 cTabHeight = 30.0f;
    const f32 cTabGap = 8.0f;
    const f32 cTabPadding = 18.0f;
    const f32 cBackHeight = 32.0f;
    const f32 cFilterHeight = 24.0f;
    const f32 cFilterSize = 13.0f;
    const f32 cFilterPadding = 14.0f;
    const f32 cGap = 12.0f;  // between cards
    const f32 cCardMinWidth = 330.0f;
    const f32 cCardMinHeight = 78.0f;
    const f32 cCardPad = 14.0f;
    const f32 cBadge = 56.0f;
    const f32 cIcon = 42.0f;
    const f32 cBarHeight = 7.0f;
    const f32 cTileMinWidth = 230.0f;
    const f32 cTileHeight = 66.0f;
    const f32 cSectionHeight = 34.0f;
    const s32 cMaxColumns = 4;

    // Text heights.
    const f32 cTitleSize = 25.0f;
    const f32 cTabSize = 15.0f;
    const f32 cNameSize = 17.5f;
    const f32 cHintSize = 13.0f;
    const f32 cMetaSize = 12.0f;
    const f32 cSummarySize = 16.0f;
    const f32 cValueSize = 21.0f;
    const f32 cLabelSize = 12.5f;
    const f32 cSectionSize = 15.0f;
    const f32 cFooterSize = 14.0f;
    const f32 cFooterGlyph = 21.0f;
    const f32 cBackGlyph = 18.0f;
    const f32 cTitleGlyph = 26.0f;

    const f32 cCrownHeight = 0.71f;  // the crown's picture, of its size (measured)
    const f32 cCrownLift = 0.02f;    // its middle above its box's middle

    // Colours, 0xRRGGBBAA: the pop-up's blue (InformationWindow) for what is earned, gold to mark it; deep night blue
    // for what is not.
    const u32 cPanelFill = 0x0A1442C8;
    const u32 cPanelRing = 0x6C8CE0FF;
    const u32 cCardFill = 0x3C40D8FF;
    const u32 cCardRing = 0xFFD860FF;
    const u32 cLockedFill = 0x1A1E52FF;
    const u32 cLockedRing = 0x3E4888FF;
    const u32 cBadgeFill = 0x22248CFF;
    const u32 cLockedBadgeFill = 0x10123AFF;
    const u32 cShadow = 0x00000060;
    const u32 cFocus = 0xFFFFFFFF;
    const u32 cWhite = 0xFFFFFFFF;
    const u32 cTextShadow = 0x00000070;
    const u32 cSubText = 0xD2DCFFFF;
    const u32 cLockedText = 0xA6AED6FF;
    const u32 cLockedSubText = 0x7E88B8FF;
    const u32 cGold = 0xFFD860FF;
    const u32 cGoldTop = 0xFFF4B0FF;
    const u32 cGoldBottom = 0xFFC038FF;
    const u32 cIconLocked = 0x56628CFF;
    const u32 cButtonText = 0x505050FF;  // the game's buttons' grey (BackButton's Txt material)
    const u32 cTrack = 0x0000004C;
    const u32 cTileFill = 0x2C2FA8F0;
    const u32 cTileRing = 0x6C74E8FF;

    const s32 cFadeFrames = 20;  // to black, and back from it
    const s32 cSwitchFrames = 12;


    // The picture font's buttons (MR::addPictureFontTag).
    const s32 cGlyphB = '1';
    const s32 cGlyphPlus = 'H';
    const s32 cGlyphMinus = 'G';
    const s32 cGlyphPad = 'C';
    const s32 cGlyphCrown = 'P';


    // A whole number, at least `digits` long (the game's wide characters are 16-bit everywhere: no swprintf).
    void formatInt(wchar_t* pDst, s32 capacity, s32 value, s32 digits) {
        char text[16];
        snprintf(text, sizeof(text), "%0*d", digits, value);
        s32 i = 0;
        for (; text[i] != 0 && i + 1 < capacity; i++) {
            pDst[i] = static_cast< wchar_t >(text[i]);
        }
        pDst[i] = 0;
    }

    // A number with its digits grouped by threes, as the language writes them (NebulaText's DigitGroup).
    void formatNumber(wchar_t* pDst, s32 capacity, u64 value) {
        char digits[24];
        snprintf(digits, sizeof(digits), "%llu", static_cast< unsigned long long >(value));
        wchar_t group[4];
        NebulaText::get(group, 4, "DigitGroup");
        const s32 count = static_cast< s32 >(strlen(digits));
        wchar_t* p = pDst;
        wchar_t* pEnd = pDst + capacity;
        for (s32 i = 0; i < count && p + 1 < pEnd; i++) {
            if (i > 0 && (count - i) % 3 == 0) {
                p = append(p, pEnd, group);
            }
            *p++ = static_cast< wchar_t >(digits[i]);
        }
        *p = 0;
    }

    // NebulaText's `pId` with its {name} tokens replaced.
    void fill(wchar_t* pDst, s32 capacity, const char* pId, const char* const* pNames, const wchar_t* const* pValues, s32 count) {
        wchar_t pattern[128];
        NebulaText::get(pattern, 128, pId);
        wchar_t* p = pDst;
        wchar_t* pEnd = pDst + capacity;
        *p = 0;
        for (const wchar_t* s = pattern; *s != 0;) {
            bool replaced = false;
            if (*s == L'{') {
                for (s32 i = 0; i < count && !replaced; i++) {
                    const char* pName = pNames[i];
                    s32 n = 0;
                    while (pName[n] != 0 && s[1 + n] == static_cast< wchar_t >(pName[n])) {
                        n++;
                    }
                    if (pName[n] == 0 && s[1 + n] == L'}') {
                        p = append(p, pEnd, pValues[i]);
                        s += n + 2;
                        replaced = true;
                    }
                }
            }
            if (!replaced) {
                if (p + 1 < pEnd) {
                    *p++ = *s;
                    *p = 0;
                }
                s++;
            }
        }
    }

    void formatDate(wchar_t* pDst, s32 capacity, s32 date) {
        wchar_t y[8], m[4], d[4];
        formatInt(y, 8, date / 10000, 1);
        formatInt(m, 4, date / 100 % 100, 2);
        formatInt(d, 4, date % 100, 2);
        const char* const names[] = {"y", "m", "d"};
        const wchar_t* const values[] = {y, m, d};
        fill(pDst, capacity, "DateFormat", names, values, 3);
    }

    void formatCount(wchar_t* pDst, s32 capacity, s32 current, s32 goal) {
        wchar_t n[16], total[16];
        formatNumber(n, 16, static_cast< u64 >(current));
        formatNumber(total, 16, static_cast< u64 >(goal));
        const char* const names[] = {"n", "total"};
        const wchar_t* const values[] = {n, total};
        fill(pDst, capacity, "BoardCount", names, values, 2);
    }

    struct Card {
        Pane* mRoot;
        Window* mFocus;
        Window* mShadow;
        Window* mBody;
        Window* mBadge;
        TextBox* mGlyph;
        Picture* mPicture;
        TextBox* mNameShadow;
        TextBox* mName;
        TextBox* mHint;
        TextBox* mMeta;
        Window* mTrack;
        Window* mBar;
        wchar_t mHintText[192];  // what the player did (shown unlocked), or the locked line
        bool mUnlocked;
        s32 mDate;
        s32 mCurrent;
        s32 mGoal;
        bool mShown;  // in the current tab and filter
        s32 mOrder;   // its place among those shown
        f32 mHintSize;
        f32 mHintHeight;
        f32 mMetaWidth;
        f32 mFocusLevel;
    };

    struct Tile {
        Pane* mRoot;
        Window* mBody;
        TextBox* mGlyph;
        Picture* mPicture;
        TextBox* mValueShadow;
        TextBox* mValue;
        TextBox* mLabel;
    };


    // The stats page, in its order: the save file's numbers, then Nebula's all-time counts.
    enum {
        StatPowerStars,
        StatStarBits,
        StatLives,
        StatChapters,
        StatComets,
        StatPurpleCoins,
        StatHidden,
        StatGreenStars,
        StatLumas,
        StatJumps,
        StatSpins,
        StatCoins,
        StatStarBitsCollected,
        StatLivesLost,
        StatGameOvers,
        StatOneUps,
        StatPlayTime,
        StatNum,
        StatFirstAllTime = StatJumps,
    };

    const char* const cStatLabels[StatNum] = {
        "StatPowerStars", "StatStarBits", "StatLives", "StatChapters", "StatComets", "StatPurpleCoins",
        "StatHidden", "StatGreenStars", "StatLumas", "StatJumps", "StatSpins", "StatCoins",
        "StatStarBitsCollected", "StatLivesLost", "StatGameOvers", "StatOneUps", "StatPlayTime",
    };

    // The footer's guide: a glyph (or two) and its word.
    enum { HintTabs, HintMove, HintNum };
};  // namespace

struct NebulaBoardParts {
    NebulaPaneKit mText;    // InformationWindow: the game's message font
    NebulaPaneKit mWindow;  // SysInfoWindowMini: the window with a ring
    NebulaPaneKit mButton;  // BackButton: the game's button
    nw4r::lyt::TexMap** mTextures;
    Pane* mBoard;
    Window* mPanel;
    TextBox* mTitleGlyph;
    TextBox* mTitleShadow;
    TextBox* mTitle;
    TextBox* mSummary;
    Window* mSummaryTrack;
    Window* mSummaryBar;
    TextBox* mPercent;
    Chip mTabs[TabNum];
    Chip mFilters[FilterNum];
    TextBox* mTabCount;  // the tab's own count, right of the filters
    Chip mBack;
    TextBox* mHintGlyphs[HintNum];
    TextBox* mHintLabels[HintNum];
    Pane* mList;
    Card* mCards;
    s32 mCardNum;
    Tile mTiles[StatNum];
    TextBox* mSections[2];
    TextBox* mEmpty;
    Window* mScrollTrack;
    Window* mScrollThumb;

    // State.
    s32 mTab;
    s32 mFilterMode;
    s32 mZone;
    s32 mTabFocus;
    s32 mCardFocus;  // among the cards shown; -1 none (the pointer over nothing)
    s32 mShownNum;
    s32 mUnlockedNum;
    f32 mScroll;
    f32 mScrollTarget;
    f32 mScrollVelocity;
    f32 mContentHeight;
    bool mDragging;
    bool mDragMoved;
    f32 mDragStartY;
    f32 mDragStartScroll;
    f32 mDragLastY;
    bool mPointerMode;
    bool mPointerIn;
    bool mOverBack;
    f32 mPointerX;
    f32 mPointerY;
    NebulaMenuFocus::Repeat mRepeat;  // the D-pad and stick, read as every Nebula menu reads them
    s32 mFrame;
    s32 mSwitchFrame;
    s32 mSwitchDirection;
    f32 mOpenLevel;  // 0 closed, 1 open
    bool mShown;     // drawn: from the black after the world until the black before it

    // Layout, again whenever the screen's shape or the list changes.
    f32 mScaleX;
    f32 mWidth;
    f32 mHeight;
    s32 mColumns;
    f32 mCardWidth;
    f32 mCardHeight;
    s32 mTileColumns;
    f32 mTileWidth;
    f32 mListLeft;
    f32 mListRight;
    f32 mListTop;
    f32 mListBottom;
    f32 mTabY;
    f32 mFilterY;
    f32 mHeaderY;
    f32 mFooterY;
    bool mArranged;
};

namespace {
    // The board's kits for NebulaUi's builders.
    Kits boardKits(NebulaBoardParts* pParts) {
        const Kits kits = {&pParts->mText, &pParts->mWindow, &pParts->mButton};
        return kits;
    }

    Window* makeWindow(NebulaBoardParts* pParts, Pane* pParent, const char* pName) {
        return NebulaUi::makeWindow(boardKits(pParts), pParent, pName);
    }

    TextBox* makeText(NebulaBoardParts* pParts, Pane* pParent, const char* pName, u32 length) {
        return NebulaUi::makeText(boardKits(pParts), pParent, pName, length);
    }

    bool makeChip(NebulaBoardParts* pParts, Pane* pParent, Chip* pChip, const char* pName, bool arrow) {
        return NebulaUi::makeChip(boardKits(pParts), pParent, pChip, pName, arrow, cTabSize, cButtonText);
    }

    // An icon: its texture's part on the picture, else its glyph (the Power Star when the texture is missing).
    void setIcon(NebulaBoardParts* pParts, TextBox* pGlyph, Picture* pPicture, const NebulaAchievementIcon* pIcon, f32 size, u32 tint) {
        const s32 texture = pIcon != nullptr && pIcon->mGlyph == 0 ? NebulaAchievements::findIconTexture(pIcon) : -1;
        if (pPicture != nullptr && texture >= 0 && pParts->mTextures[texture] != nullptr) {
            pPicture->GetMaterial()->SetTexture(0, *pParts->mTextures[texture]);
            nw4r::math::VEC2(*pCoords)[4] = pPicture->mTexCoordAry.mData;
            if (pCoords != nullptr && pPicture->mTexCoordAry.mNum > 0) {
                pCoords[0][0] = nw4r::math::VEC2(pIcon->mU0, pIcon->mV0);
                pCoords[0][1] = nw4r::math::VEC2(pIcon->mU1, pIcon->mV0);
                pCoords[0][2] = nw4r::math::VEC2(pIcon->mU0, pIcon->mV1);
                pCoords[0][3] = nw4r::math::VEC2(pIcon->mU1, pIcon->mV1);
            }
            const f32 fill = pIcon->mFill > 0.0f ? pIcon->mFill : 1.0f;
            pPicture->mSize = nw4r::lyt::Size(size * fill * pIcon->mWidth / pIcon->mHeight, size * fill);
            for (u32 i = 0; i < 4; i++) {
                pPicture->SetVtxColor(i, color(tint));
            }
            pPicture->SetVisible(true);
            if (pGlyph != nullptr) {
                pGlyph->SetVisible(false);
            }
            return;
        }
        if (pPicture != nullptr) {
            pPicture->SetVisible(false);
        }
        if (pGlyph != nullptr) {
            setGlyph(pGlyph, pIcon != nullptr && pIcon->mGlyph != 0 ? pIcon->mGlyph : '7', size);
            setTextColor(pGlyph, tint, tint);
            pGlyph->SetVisible(true);
        }
    }

    // An icon centred on (x, y): a picture by its middle, a glyph by where its picture sits.
    void placeIcon(TextBox* pGlyph, Picture* pPicture, f32 x, f32 y, f32 size) {
        if (pGlyph != nullptr) {
            pGlyph->mTranslate = nw4r::math::VEC3(x, glyphCentre(y, size), 0.0f);
        }
        if (pPicture != nullptr) {
            pPicture->mTranslate = nw4r::math::VEC3(x, y, 0.0f);
        }
    }

    bool isInGroup(s32 tab, s32 group) {
        return tab == TabAll || (tab == TabStory && group == 0) || (tab == TabChallenges && group == 1) || (tab == TabExtras && group == 2);
    }


    f32 getMaxScroll(const NebulaBoardParts* p) {
        const f32 over = p->mContentHeight - (p->mListTop - p->mListBottom);
        return over > 0.0f ? over : 0.0f;
    }

    // A card's text block (the name's capitals to the last line's baseline, and the progress bar), measured from the
    // name box's top; the box's top that centres the block on the card's middle.
    f32 cardTextTop(const Card& rCard, f32* pBlock) {
        const bool bar = !rCard.mUnlocked && rCard.mGoal > 0;
        const f32 top = -cCapTop * cNameSize;
        f32 bottom = -cBaseline * cNameSize;
        if (rCard.mHintText[0] != 0) {
            const s32 lines = countLines(rCard.mHintHeight, rCard.mHintSize);
            bottom = -(cNameSize + 6.0f) - cBaseline * rCard.mHintSize - (lines - 1) * rCard.mHintSize * (1.0f + cLineSpace);
        }
        if (bar) {
            bottom -= 11.0f + cBarHeight;
        }
        *pBlock = top - bottom;
        return -(top + bottom) * 0.5f;
    }

    // Where everything goes for the screen's shape, and what the tab and filter show.
    void arrange(NebulaBoardParts* p) {
        readScreen(&p->mScaleX, &p->mWidth, &p->mHeight);
        const f32 left = -p->mWidth * 0.5f + cMargin + cPad;
        const f32 right = p->mWidth * 0.5f - cMargin - cPad;
        const f32 top = p->mHeight * 0.5f - cMargin - cPad;
        p->mHeaderY = top - cHeaderHeight * 0.5f;
        p->mTabY = top - cHeaderHeight - 12.0f - cTabHeight * 0.5f;
        p->mListLeft = left;
        p->mListRight = right - 12.0f;  // the scroll bar's room
        p->mFilterY = p->mTabY - cTabHeight * 0.5f - 10.0f - cFilterHeight * 0.5f;
        p->mListTop = p->mTab == TabStats ? p->mTabY - cTabHeight * 0.5f - 16.0f : p->mFilterY - cFilterHeight * 0.5f - 14.0f;
        p->mListBottom = -p->mHeight * 0.5f + cFooter + 10.0f;
        p->mFooterY = -p->mHeight * 0.5f + cFooter * 0.5f;

        // Columns: as many cards at least cCardMinWidth wide as fit, at most four.
        const f32 listWidth = p->mListRight - p->mListLeft;
        s32 columns = static_cast< s32 >((listWidth + cGap) / (cCardMinWidth + cGap));
        columns = columns < 1 ? 1 : columns > cMaxColumns ? cMaxColumns : columns;
        p->mColumns = columns;
        p->mCardWidth = (listWidth - cGap * (columns - 1)) / columns;
        s32 tileColumns = static_cast< s32 >((listWidth + cGap) / (cTileMinWidth + cGap));
        tileColumns = tileColumns < 2 ? 2 : tileColumns > 6 ? 6 : tileColumns;
        p->mTileColumns = tileColumns;
        p->mTileWidth = (listWidth - cGap * (tileColumns - 1)) / tileColumns;

        // The cards shown, in the catalogue's order; their text fitted to the card, which is as tall as the
        // tallest needs (every card the same height).
        p->mShownNum = 0;
        const f32 textWidth = p->mCardWidth - cCardPad * 2.0f - cBadge - 14.0f;
        f32 cardHeight = cCardMinHeight;
        for (s32 i = 0; i < p->mCardNum; i++) {
            Card& rCard = p->mCards[i];
            const bool inTab = p->mTab != TabStats && isInGroup(p->mTab, NebulaAchievements::getGroup(i));
            const bool inFilter = p->mFilterMode == FilterAll || (p->mFilterMode == FilterUnlocked) == rCard.mUnlocked;
            // A secret shows only once unlocked; the 120-star goals once Halfway There is.
            const bool listed = rCard.mUnlocked || NebulaAchievements::isShownLocked(i);
            rCard.mShown = inTab && inFilter && listed;
            rCard.mOrder = rCard.mShown ? p->mShownNum++ : -1;
            rCard.mRoot->SetVisible(false);

            f32 width, height;
            measure(rCard.mMeta, 4000.0f, &rCard.mMetaWidth, &height);
            // A long name (some languages) narrows into its room beside the date.
            const f32 nameRoom = textWidth - (rCard.mUnlocked ? rCard.mMetaWidth + 12.0f : 0.0f);
            measure(rCard.mName, 4000.0f, &width, &height);
            measure(rCard.mNameShadow, 4000.0f, &width, &height);
            const f32 squeeze = width > nameRoom && width > 0.0f ? nameRoom / width : 1.0f;
            rCard.mName->mScale = nw4r::math::VEC2(squeeze, 1.0f);
            rCard.mNameShadow->mScale = nw4r::math::VEC2(squeeze, 1.0f);
            // The sentence below it: two lines at most, smaller if a language needs a third.
            rCard.mHintSize = cHintSize;
            setTextSize(rCard.mHint, rCard.mHintSize);
            wrapWords(rCard.mHint, rCard.mHintText, textWidth, &width, &rCard.mHintHeight);
            if (countLines(rCard.mHintHeight, rCard.mHintSize) > 2) {
                rCard.mHintSize = cHintSize * 0.86f;
                setTextSize(rCard.mHint, rCard.mHintSize);
                wrapWords(rCard.mHint, rCard.mHintText, textWidth, &width, &rCard.mHintHeight);
            }
            f32 block;
            cardTextTop(rCard, &block);
            if (rCard.mShown && block + cCardPad * 2.0f > cardHeight) {
                cardHeight = block + cCardPad * 2.0f;
            }
        }
        p->mCardHeight = cardHeight;
        if (p->mCardFocus >= p->mShownNum) {
            p->mCardFocus = p->mShownNum > 0 ? p->mShownNum - 1 : 0;
        }

        // The content's height: rows of cards, or the stats page's sections.
        f32 width, height;
        if (p->mTab == TabStats) {
            const s32 fileRows = (StatFirstAllTime + p->mTileColumns - 1) / p->mTileColumns;
            const s32 allRows = (StatNum - StatFirstAllTime + p->mTileColumns - 1) / p->mTileColumns;
            p->mContentHeight = 2.0f * cSectionHeight + (fileRows + allRows) * (cTileHeight + cGap) + cGap;
            for (s32 i = 0; i < 2; i++) {
                measure(p->mSections[i], 4000.0f, &width, &height);
            }
            const f32 room = p->mTileWidth - cCardPad * 2.0f - 48.0f;
            for (s32 i = 0; i < StatNum; i++) {
                Tile& rTile = p->mTiles[i];
                measure(rTile.mValue, 4000.0f, &width, &height);
                measure(rTile.mValueShadow, 4000.0f, &width, &height);
                const f32 valueScale = width > room && width > 0.0f ? room / width : 1.0f;
                rTile.mValue->mScale = nw4r::math::VEC2(valueScale, 1.0f);
                rTile.mValueShadow->mScale = nw4r::math::VEC2(valueScale, 1.0f);
                measure(rTile.mLabel, 4000.0f, &width, &height);
                rTile.mLabel->mScale = nw4r::math::VEC2(width > room && width > 0.0f ? room / width : 1.0f, 1.0f);
            }
        } else {
            const s32 rows = (p->mShownNum + p->mColumns - 1) / p->mColumns;
            p->mContentHeight = rows * p->mCardHeight + (rows > 0 ? (rows - 1) * cGap : 0.0f) + 10.0f;
        }
        if (p->mScrollTarget > getMaxScroll(p)) {
            p->mScrollTarget = getMaxScroll(p);
        }

        // The empty list's line.
        wchar_t empty[96] = {0};
        if (p->mTab != TabStats && p->mShownNum == 0) {
            NebulaText::get(empty, 96, p->mFilterMode == FilterLocked ? "BoardEmptyLocked" : "BoardEmptyUnlocked");
        }
        wrapWords(p->mEmpty, empty, p->mListRight - p->mListLeft - 40.0f, &width, &height);

        // The tabs across the panel's whole width, each the same; a label too long for its tab narrows.
        const f32 tabWidth = (right - left - cTabGap * (TabNum - 1)) / TabNum;
        for (s32 i = 0; i < TabNum; i++) {
            Chip& rChip = p->mTabs[i];
            rChip.mText->mScale = nw4r::math::VEC2(1.0f, 1.0f);
            measure(rChip.mText, 4000.0f, &width, &height);
            const f32 room = tabWidth - cTabPadding * 2.0f;
            rChip.mText->mScale = nw4r::math::VEC2(width > room && width > 0.0f ? room / width : 1.0f, 1.0f);
            rChip.mWidth = tabWidth;
            rChip.mHeight = cTabHeight;
            rChip.mX = left + tabWidth * 0.5f + i * (tabWidth + cTabGap);
        }
        // The filters: three chips from the list's left edge, each as wide as its word.
        f32 x = left;
        for (s32 i = 0; i < FilterNum; i++) {
            Chip& rChip = p->mFilters[i];
            measure(rChip.mText, 4000.0f, &width, &height);
            rChip.mWidth = width + cFilterPadding * 2.0f;
            rChip.mHeight = cFilterHeight;
            rChip.mX = x + rChip.mWidth * 0.5f;
            x += rChip.mWidth + cTabGap;
        }
        // The tab's count at the row's right: how many of its achievements are unlocked (the header has All's).
        wchar_t count[48] = {0};
        if (p->mTab != TabAll && p->mTab != TabStats) {
            s32 unlocked = 0;
            s32 total = 0;
            for (s32 i = 0; i < p->mCardNum; i++) {
                if (isInGroup(p->mTab, NebulaAchievements::getGroup(i))) {
                    total++;
                    unlocked += p->mCards[i].mUnlocked ? 1 : 0;
                }
            }
            formatCount(count, 48, unlocked, total);
        }
        setText(p->mTabCount, count);
        measure(p->mTabCount, 4000.0f, &width, &height);

        // The footer: Back on the left (its arrow, then its label), the pad's guide on the right.
        Chip& rBack = p->mBack;
        measure(rBack.mText, 4000.0f, &width, &height);
        rBack.mHeight = cBackHeight;
        rBack.mWidth = cBackHeight * 0.56f + backArrowSize(cBackHeight) * 0.5f + 6.0f + cBackGlyph + 5.0f + width + cBackHeight * 0.5f;
        rBack.mX = -p->mWidth * 0.5f + cMargin + rBack.mWidth * 0.5f;
        for (s32 i = 0; i < HintNum; i++) {
            measure(p->mHintLabels[i], 4000.0f, &width, &height);
            measure(p->mHintGlyphs[i], 4000.0f, &width, &height);
        }
        p->mArranged = true;
    }

    // Keeps the focused card's row in view.
    void followFocus(NebulaBoardParts* p) {
        if (p->mTab == TabStats || p->mShownNum == 0 || p->mCardFocus < 0) {
            return;
        }
        const s32 row = p->mCardFocus / p->mColumns;
        const f32 rowTop = row * (p->mCardHeight + cGap);
        const f32 rowBottom = rowTop + p->mCardHeight;
        const f32 view = p->mListTop - p->mListBottom;
        if (rowTop - 8.0f < p->mScrollTarget) {
            p->mScrollTarget = rowTop - 8.0f;
        } else if (rowBottom + 8.0f > p->mScrollTarget + view) {
            p->mScrollTarget = rowBottom + 8.0f - view;
        }
        p->mScrollTarget = clamp(p->mScrollTarget, 0.0f, getMaxScroll(p));
    }

    // Whether a box between `top` and `bottom` reaches into the list: 1 or 0. One cut by the list's edges stays as
    // bright as the rest (the scissor cuts it): dimmed, an unlocked card would read as a locked one.
    f32 listAlpha(const NebulaBoardParts* p, f32 top, f32 bottom) {
        return bottom < p->mListTop + 2.0f && top > p->mListBottom - 6.0f ? 1.0f : 0.0f;
    }

    // A button for this frame: its art at its size (a little larger while highlighted, as the game's buttons grow
    // when pointed at), the shine for the highlight, the label centred by its letters.
    void placeChip(Chip& rChip, f32 x, f32 y, bool active, bool focused, f32 pulse) {
        rChip.mFocusLevel += ((focused ? 1.0f : 0.0f) - rChip.mFocusLevel) * 0.35f;
        const f32 grow = 1.0f + 0.06f * rChip.mFocusLevel;
        rChip.mRoot->mTranslate = nw4r::math::VEC3(x, y, 0.0f);
        rChip.mRoot->mScale = nw4r::math::VEC2(grow, grow);
        shapeChip(rChip, rChip.mWidth, rChip.mHeight);
        rChip.mArt->mAlpha = toAlpha(active ? 1.0f : 0.62f);
        rChip.mLayers[LayerShine]->mAlpha = toAlpha(rChip.mFocusLevel * pulse);
    }

    // One frame of placement: the open and tab animations, the scroll, the highlight.
    void place(NebulaBoardParts* p, f32 open, s32 frame) {
        const f32 pulse = 0.7f + 0.3f * __builtin_sinf(frame * 0.12f);
        p->mBoard->mScale = nw4r::math::VEC2(p->mScaleX, 1.0f);
        p->mBoard->mAlpha = toAlpha(open);
        p->mBoard->mTranslate = nw4r::math::VEC3(0.0f, (1.0f - easeOut(open)) * -16.0f, 0.0f);

        // The panel and the header: the crown and title on the header's middle, the count, bar and percentage too.
        const f32 panelTop = p->mHeight * 0.5f - cMargin;
        const f32 panelBottom = -p->mHeight * 0.5f + cFooter;
        shape(p->mPanel, 0.0f, (panelTop + panelBottom) * 0.5f, p->mWidth - cMargin * 2.0f, panelTop - panelBottom, 16.0f);
        const f32 left = -p->mWidth * 0.5f + cMargin + cPad;
        const f32 right = p->mWidth * 0.5f - cMargin - cPad;
        const f32 headerY = panelTop - cPad - cHeaderHeight * 0.5f;
        f32 width, height;
        // The crown stands on the title's baseline.
        const f32 baseline = headerY - cCapHeight * 0.5f * cTitleSize;
        const f32 crownMiddle = baseline + cCrownHeight * 0.5f * cTitleGlyph;
        p->mTitleGlyph->mTranslate = nw4r::math::VEC3(left + cTitleGlyph * 0.5f + 2.0f, crownMiddle + cCrownLift * cTitleGlyph, 0.0f);
        measure(p->mTitle, 4000.0f, &width, &height);
        measure(p->mTitleShadow, 4000.0f, &width, &height);
        const f32 titleX = left + cTitleGlyph + 12.0f;
        p->mTitle->mTranslate = nw4r::math::VEC3(titleX, textCentre(headerY, cTitleSize), 0.0f);
        p->mTitleShadow->mTranslate = nw4r::math::VEC3(titleX + 1.5f, textCentre(headerY, cTitleSize) - 1.5f, 0.0f);
        f32 percentWidth;
        measure(p->mPercent, 4000.0f, &percentWidth, &height);
        measure(p->mSummary, 4000.0f, &width, &height);
        const f32 barWidth = clamp((right - left) * 0.22f, 90.0f, 220.0f);
        p->mPercent->mTranslate = nw4r::math::VEC3(right, textCentre(headerY, cSummarySize), 0.0f);
        const f32 barRight = right - percentWidth - 12.0f;
        shape(p->mSummaryTrack, barRight - barWidth * 0.5f, headerY, barWidth, 10.0f, 5.0f);
        const f32 done = p->mCardNum > 0 ? static_cast< f32 >(p->mUnlockedNum) / p->mCardNum : 0.0f;
        const f32 fillWidth = barWidth * done * easeOut(open);
        shape(p->mSummaryBar, barRight - barWidth + fillWidth * 0.5f, headerY, fillWidth < 10.0f ? 10.0f : fillWidth, 10.0f, 5.0f);
        p->mSummaryBar->SetVisible(p->mUnlockedNum > 0);
        p->mSummary->mTranslate = nw4r::math::VEC3(barRight - barWidth - 12.0f, textCentre(headerY, cSummarySize), 0.0f);

        // The tabs and, under them, the filters: the chosen one bright, the others dimmed; the highlight grows and
        // shines as the game's buttons do.
        const bool filters = p->mTab != TabStats;
        for (s32 i = 0; i < TabNum + FilterNum; i++) {
            const bool tab = i < TabNum;
            Chip& rChip = tab ? p->mTabs[i] : p->mFilters[i - TabNum];
            const s32 k = tab ? i : i - TabNum;
            const bool chosen = tab ? k == p->mTab : k == p->mFilterMode;
            const bool focused = (tab ? p->mZone == ZoneTabs && p->mTabFocus == k : p->mZone == ZoneFilter && p->mFilterMode == k) &&
                                 (!p->mPointerMode || p->mPointerIn);
            rChip.mRoot->SetVisible(tab || filters);
            placeChip(rChip, rChip.mX, tab ? p->mTabY : p->mFilterY, chosen, focused, pulse);
            rChip.mText->mTranslate = nw4r::math::VEC3(0.0f, textCentre(0.0f, tab ? cTabSize : cFilterSize), 0.0f);
            rChip.mText->mAlpha = toAlpha(chosen ? 1.0f : 0.8f);
        }
        p->mTabCount->mTranslate = nw4r::math::VEC3(right, textCentre(p->mFilterY, cFilterSize + 1.0f), 0.0f);
        p->mTabCount->SetVisible(filters);

        // The list: a tab's change slides it in; the scissor cuts what reaches past the edges.
        const f32 switched = easeOut(static_cast< f32 >(p->mSwitchFrame) / cSwitchFrames);
        p->mList->mTranslate = nw4r::math::VEC3((1.0f - switched) * 26.0f * p->mSwitchDirection, 0.0f, 0.0f);
        p->mList->mAlpha = toAlpha(static_cast< f32 >(p->mSwitchFrame) / cSwitchFrames * 1.4f);
        const f32 cw = p->mCardWidth;
        const f32 ch = p->mCardHeight;
        for (s32 i = 0; i < p->mCardNum; i++) {
            Card& rCard = p->mCards[i];
            if (!rCard.mShown) {
                rCard.mRoot->SetVisible(false);
                continue;
            }
            const s32 column = rCard.mOrder % p->mColumns;
            const s32 row = rCard.mOrder / p->mColumns;
            const f32 x = p->mListLeft + column * (cw + cGap) + cw * 0.5f;
            const f32 y = p->mListTop - 5.0f - row * (ch + cGap) - ch * 0.5f + p->mScroll;
            f32 alpha = listAlpha(p, y + ch * 0.5f, y - ch * 0.5f);
            // A cascade as the list appears: each card a moment after the one before.
            const f32 delay = (rCard.mOrder < 12 ? rCard.mOrder : 12) * 1.5f;
            const f32 cascade = clamp((p->mSwitchFrame - delay) / 8.0f, 0.0f, 1.0f);
            alpha *= cascade;
            if (alpha <= 0.0f) {
                rCard.mRoot->SetVisible(false);
                continue;
            }
            rCard.mRoot->SetVisible(true);
            rCard.mRoot->mAlpha = toAlpha(alpha);

            const bool focused = p->mZone == ZoneList && p->mCardFocus == rCard.mOrder && (!p->mPointerMode || p->mPointerIn);
            rCard.mFocusLevel += ((focused ? 1.0f : 0.0f) - rCard.mFocusLevel) * 0.3f;
            const f32 grow = 1.0f + 0.02f * rCard.mFocusLevel;
            rCard.mRoot->mTranslate = nw4r::math::VEC3(x, y + (1.0f - cascade) * -10.0f, 0.0f);
            rCard.mRoot->mScale = nw4r::math::VEC2(grow, grow);

            shape(rCard.mFocus, 0.0f, 0.0f, cw + 7.0f, ch + 7.0f, 15.5f);
            rCard.mFocus->mAlpha = toAlpha(rCard.mFocusLevel * pulse);
            shape(rCard.mShadow, 0.0f, -3.5f, cw, ch, 12.0f);
            shape(rCard.mBody, 0.0f, 0.0f, cw, ch, 12.0f);
            const f32 badgeX = -cw * 0.5f + cCardPad + cBadge * 0.5f;
            shape(rCard.mBadge, badgeX, 0.0f, cBadge, cBadge, cBadge * 0.5f);
            placeIcon(rCard.mGlyph, rCard.mPicture, badgeX, 0.0f, cIcon);

            // The name, the sentence and the progress bar as one block, centred on the card by their letters.
            const bool bar = !rCard.mUnlocked && rCard.mGoal > 0;
            f32 block;
            const f32 nameTop = cardTextTop(rCard, &block);
            const f32 textX = -cw * 0.5f + cCardPad + cBadge + 14.0f;
            const f32 textRight = cw * 0.5f - cCardPad;
            rCard.mName->mTranslate = nw4r::math::VEC3(textX, nameTop, 0.0f);
            rCard.mNameShadow->mTranslate = nw4r::math::VEC3(textX + 1.0f, nameTop - 1.0f, 0.0f);
            const f32 hintTop = nameTop - cNameSize - 6.0f;
            rCard.mHint->mTranslate = nw4r::math::VEC3(textX, hintTop, 0.0f);
            rCard.mHint->SetVisible(rCard.mHintText[0] != 0);
            rCard.mMeta->SetVisible(rCard.mUnlocked || bar);
            rCard.mTrack->SetVisible(bar);
            rCard.mBar->SetVisible(bar && rCard.mCurrent > 0);
            if (rCard.mUnlocked) {
                // The day, its letters level with the name's.
                const f32 nameMiddle = nameTop - (cCapTop + cCapHeight * 0.5f) * cNameSize;
                setAnchor(rCard.mMeta, 5);
                rCard.mMeta->mTranslate = nw4r::math::VEC3(textRight, textCentre(nameMiddle, cMetaSize), 0.0f);
            } else if (bar) {
                // The count right of its bar, both under the text.
                f32 baseline = nameTop - cBaseline * cNameSize;
                if (rCard.mHintText[0] != 0) {
                    const s32 lines = countLines(rCard.mHintHeight, rCard.mHintSize);
                    baseline = hintTop - cBaseline * rCard.mHintSize - (lines - 1) * rCard.mHintSize * (1.0f + cLineSpace);
                }
                const f32 barY = baseline - 11.0f - cBarHeight * 0.5f;
                setAnchor(rCard.mMeta, 5);
                rCard.mMeta->mTranslate = nw4r::math::VEC3(textRight, textCentre(barY, cMetaSize), 0.0f);
                const f32 barWidth = textRight - rCard.mMetaWidth - 10.0f - textX;
                shape(rCard.mTrack, textX + barWidth * 0.5f, barY, barWidth, cBarHeight, cBarHeight * 0.5f);
                const f32 part = barWidth * rCard.mCurrent / rCard.mGoal;
                const f32 shown = part < cBarHeight ? cBarHeight : part;
                shape(rCard.mBar, textX + shown * 0.5f, barY, shown, cBarHeight, cBarHeight * 0.5f);
            }
        }

        // The stats page: the file's tiles, then the all-time ones, each under its title.
        const bool stats = p->mTab == TabStats;
        f32 y = p->mListTop + p->mScroll;
        for (s32 section = 0; section < 2; section++) {
            TextBox* pTitle = p->mSections[section];
            const f32 titleY = y - 6.0f;
            pTitle->mTranslate = nw4r::math::VEC3(p->mListLeft + 2.0f, titleY, 0.0f);
            const f32 titleAlpha = listAlpha(p, titleY, titleY - cSectionSize);
            pTitle->SetVisible(stats && titleAlpha > 0.0f);
            pTitle->mAlpha = toAlpha(titleAlpha);
            y -= cSectionHeight;
            const s32 first = section == 0 ? 0 : StatFirstAllTime;
            const s32 last = section == 0 ? StatFirstAllTime : StatNum;
            for (s32 i = first; i < last; i++) {
                Tile& rTile = p->mTiles[i];
                const s32 k = i - first;
                const f32 tw = p->mTileWidth;
                const f32 tx = p->mListLeft + (k % p->mTileColumns) * (tw + cGap) + tw * 0.5f;
                const f32 ty = y - (k / p->mTileColumns) * (cTileHeight + cGap) - cTileHeight * 0.5f;
                const f32 cascade = clamp((p->mSwitchFrame - (i < 12 ? i : 12) * 1.2f) / 8.0f, 0.0f, 1.0f);
                const f32 alpha = listAlpha(p, ty + cTileHeight * 0.5f, ty - cTileHeight * 0.5f) * cascade;
                rTile.mRoot->SetVisible(stats && alpha > 0.0f);
                if (!stats || alpha <= 0.0f) {
                    continue;
                }
                rTile.mRoot->mAlpha = toAlpha(alpha);
                rTile.mRoot->mTranslate = nw4r::math::VEC3(tx, ty + (1.0f - cascade) * -10.0f, 0.0f);
                shape(rTile.mBody, 0.0f, 0.0f, tw, cTileHeight, 12.0f);
                placeIcon(rTile.mGlyph, rTile.mPicture, -tw * 0.5f + cCardPad + 19.0f, 0.0f, 36.0f);
                // The number over its label, the pair centred on the tile by their letters.
                const f32 textX = -tw * 0.5f + cCardPad + 48.0f;
                const f32 blockTop = -cCapTop * cValueSize;
                const f32 labelTop = -(cValueSize + 3.0f);
                const f32 blockBottom = labelTop - cBaseline * cLabelSize;
                const f32 valueTop = -(blockTop + blockBottom) * 0.5f;
                rTile.mValue->mTranslate = nw4r::math::VEC3(textX, valueTop, 0.0f);
                rTile.mValueShadow->mTranslate = nw4r::math::VEC3(textX + 1.0f, valueTop - 1.5f, 0.0f);
                rTile.mLabel->mTranslate = nw4r::math::VEC3(textX, valueTop + labelTop, 0.0f);
            }
            y -= ((last - first + p->mTileColumns - 1) / p->mTileColumns) * (cTileHeight + cGap) + cGap;
        }

        p->mEmpty->mTranslate =
            nw4r::math::VEC3((p->mListLeft + p->mListRight) * 0.5f, textCentre((p->mListTop + p->mListBottom) * 0.5f, cNameSize), 0.0f);
        p->mEmpty->SetVisible(!stats && p->mShownNum == 0);

        // The scroll bar, when there is more than fits.
        const f32 view = p->mListTop - p->mListBottom;
        const f32 maxScroll = getMaxScroll(p);
        const bool scrolls = maxScroll > 0.5f;
        p->mScrollTrack->SetVisible(scrolls);
        p->mScrollThumb->SetVisible(scrolls);
        if (scrolls) {
            const f32 trackX = right - 2.5f;
            shape(p->mScrollTrack, trackX, (p->mListTop + p->mListBottom) * 0.5f, 5.0f, view, 2.5f);
            const f32 thumb = clamp(view * view / p->mContentHeight, 24.0f, view);
            const f32 at = clamp(p->mScroll / maxScroll, 0.0f, 1.0f);
            shape(p->mScrollThumb, trackX, p->mListTop - thumb * 0.5f - (view - thumb) * at, 5.0f, thumb, 2.5f);
        }

        // The footer, on one line: Back (its arrow on the left end, its label after it), the guide on the right.
        // Back: its arrow inside the rounded left end, then B, then the word, all on the button's middle.
        Chip& rBack = p->mBack;
        placeChip(rBack, rBack.mX, p->mFooterY, true, p->mOverBack, pulse);
        const f32 arrowX = -rBack.mWidth * 0.5f + cBackHeight * 0.56f;
        rBack.mArrow->mTranslate.x = toFaceUnits(rBack, arrowX);
        const f32 glyphX = arrowX + backArrowSize(cBackHeight) * 0.5f + 6.0f + cBackGlyph * 0.5f;
        rBack.mGlyph->mTranslate = nw4r::math::VEC3(glyphX, glyphCentre(0.0f, cBackGlyph), 0.0f);
        rBack.mText->mTranslate = nw4r::math::VEC3(glyphX + cBackGlyph * 0.5f + 5.0f, textCentre(0.0f, cFooterSize + 1.0f), 0.0f);
        f32 x = p->mWidth * 0.5f - cMargin - 6.0f;
        for (s32 i = HintNum - 1; i >= 0; i--) {
            TextBox* pLabel = p->mHintLabels[i];
            pLabel->mTranslate = nw4r::math::VEC3(x, textCentre(p->mFooterY, cFooterSize), 0.0f);
            x -= pLabel->mSize.width + 7.0f;
            TextBox* pGlyphs = p->mHintGlyphs[i];
            const f32 glyphWidth = pGlyphs->mSize.width;
            pGlyphs->mTranslate = nw4r::math::VEC3(x - glyphWidth * 0.5f, glyphCentre(p->mFooterY, cFooterGlyph), 0.0f);
            x -= glyphWidth + 24.0f;
        }
    }

    // The pointer in board units; false when it is off the screen.
    bool readPointer(NebulaBoardParts* p, f32* pX, f32* pY) {
        if (!MR::isStarPointerInScreen(WPAD_CHAN0)) {
            return false;
        }
        TVec2f screen = MR::getStarPointerScreenPositionOrEdge(WPAD_CHAN0);
        TVec2f layout;
        MR::convertScreenPosToLayoutPos(&layout, screen);
        *pX = layout.x / p->mScaleX;
        *pY = layout.y - p->mBoard->mTranslate.y;
        return true;
    }

    void selectTab(NebulaBoardParts* p, s32 tab) {
        if (tab < 0 || tab >= TabNum || tab == p->mTab) {
            return;
        }
        p->mSwitchDirection = tab > p->mTab ? 1 : -1;
        p->mTab = tab;
        p->mTabFocus = tab;
        p->mSwitchFrame = 0;
        p->mScroll = 0.0f;
        p->mScrollTarget = 0.0f;
        p->mScrollVelocity = 0.0f;
        p->mCardFocus = p->mPointerMode ? -1 : 0;
        arrange(p);
        MR::startSystemSE("SE_SY_GALAMAP_PAGE_CHANGE");
    }

    // A row of tabs or filters wraps: past the last comes the first. The list slides in from the side moved toward.
    void stepTab(NebulaBoardParts* p, s32 delta) {
        selectTab(p, (p->mTab + delta + TabNum) % TabNum);
        p->mSwitchDirection = delta;
    }

    void selectFilter(NebulaBoardParts* p, s32 mode) {
        if (mode < 0 || mode >= FilterNum || mode == p->mFilterMode) {
            return;
        }
        p->mFilterMode = mode;
        p->mSwitchDirection = 0;
        p->mSwitchFrame = 0;
        p->mScroll = 0.0f;
        p->mScrollTarget = 0.0f;
        p->mScrollVelocity = 0.0f;
        p->mCardFocus = p->mPointerMode ? -1 : 0;
        arrange(p);
        MR::startSystemSE("SE_SY_TALK_FOCUS_ITEM");
    }

    void moveFocus(NebulaBoardParts* p, s32 direction) {
        const s32 beforeZone = p->mZone;
        const s32 before = p->mZone == ZoneTabs ? p->mTabFocus : p->mZone == ZoneFilter ? p->mFilterMode : p->mCardFocus;
        if (p->mZone == ZoneTabs) {
            // Left and right change the tab; down reaches the filters (the list on the stats page).
            if (direction == NebulaMenuFocus::Direction_Left) {
                stepTab(p, -1);
            } else if (direction == NebulaMenuFocus::Direction_Right) {
                stepTab(p, 1);
            } else if (direction == NebulaMenuFocus::Direction_Down) {
                p->mZone = p->mTab != TabStats ? ZoneFilter : ZoneList;
                p->mCardFocus = 0;
                followFocus(p);
            }
        } else if (p->mZone == ZoneFilter) {
            // Left and right change the filter; up to the tabs, down to the list.
            if (direction == NebulaMenuFocus::Direction_Left) {
                selectFilter(p, (p->mFilterMode + FilterNum - 1) % FilterNum);
            } else if (direction == NebulaMenuFocus::Direction_Right) {
                selectFilter(p, (p->mFilterMode + 1) % FilterNum);
            } else if (direction == NebulaMenuFocus::Direction_Up) {
                p->mZone = ZoneTabs;
                p->mTabFocus = p->mTab;
            } else if (direction == NebulaMenuFocus::Direction_Down) {
                p->mZone = ZoneList;
                p->mCardFocus = 0;
                followFocus(p);
            }
        } else if (p->mTab == TabStats || p->mShownNum == 0) {
            // Nothing to choose: up and down scroll, up at the top reaches the tabs.
            const f32 step = cTileHeight + cGap;
            if (direction == NebulaMenuFocus::Direction_Up) {
                if (p->mScrollTarget <= 0.5f) {
                    p->mZone = p->mTab != TabStats ? ZoneFilter : ZoneTabs;
                    p->mTabFocus = p->mTab;
                }
                p->mScrollTarget = clamp(p->mScrollTarget - step, 0.0f, getMaxScroll(p));
            } else if (direction == NebulaMenuFocus::Direction_Down) {
                p->mScrollTarget = clamp(p->mScrollTarget + step, 0.0f, getMaxScroll(p));
            }
        } else {
            const s32 columns = p->mColumns;
            s32 focus = p->mCardFocus < 0 ? 0 : p->mCardFocus;
            if (direction == NebulaMenuFocus::Direction_Up) {
                if (focus < columns) {
                    p->mZone = ZoneFilter;
                } else {
                    focus -= columns;
                }
            } else if (direction == NebulaMenuFocus::Direction_Down) {
                if (focus + columns < p->mShownNum) {
                    focus += columns;
                } else if (focus / columns < (p->mShownNum - 1) / columns) {
                    focus = p->mShownNum - 1;
                }
            } else if (direction == NebulaMenuFocus::Direction_Left && focus % columns > 0) {
                focus--;
            } else if (direction == NebulaMenuFocus::Direction_Right && focus % columns < columns - 1 && focus + 1 < p->mShownNum) {
                focus++;
            }
            p->mCardFocus = focus;
            followFocus(p);
        }
        const s32 after = p->mZone == ZoneTabs ? p->mTabFocus : p->mZone == ZoneFilter ? p->mFilterMode : p->mCardFocus;
        if (after != before || p->mZone != beforeZone) {
            MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
        }
    }

    bool isInside(f32 x, f32 y, f32 cx, f32 cy, f32 w, f32 h) {
        return x >= cx - w * 0.5f && x <= cx + w * 0.5f && y >= cy - h * 0.5f && y <= cy + h * 0.5f;
    }

    // The pointer: what it is over takes the highlight, silently (as in the settings menu); A picks a tab, the filter
    // or Back; held A drags the list. True when Back was picked.
    bool handlePointer(NebulaBoardParts* p) {
        f32 x, y;
        const bool in = readPointer(p, &x, &y);
        const bool moved = in && (!p->mPointerIn || __builtin_fabsf(x - p->mPointerX) > 1.5f || __builtin_fabsf(y - p->mPointerY) > 1.5f);
        p->mPointerIn = in;
        p->mOverBack = false;
        if (!in) {
            p->mDragging = false;
            return false;
        }
        p->mPointerX = x;
        p->mPointerY = y;
        if (moved) {
            p->mPointerMode = true;
        }
        if (!p->mPointerMode) {
            return false;
        }

        const bool inList = x >= p->mListLeft - 4.0f && x <= p->mListRight + 14.0f && y <= p->mListTop + 4.0f && y >= p->mListBottom - 4.0f;
        s32 overTab = -1;
        for (s32 i = 0; i < TabNum; i++) {
            if (isInside(x, y, p->mTabs[i].mX, p->mTabY, p->mTabs[i].mWidth, cTabHeight + 4.0f)) {
                overTab = i;
            }
        }
        s32 overFilter = -1;
        for (s32 i = 0; i < FilterNum && p->mTab != TabStats; i++) {
            if (isInside(x, y, p->mFilters[i].mX, p->mFilterY, p->mFilters[i].mWidth, cFilterHeight + 4.0f)) {
                overFilter = i;
            }
        }
        s32 overCard = -1;
        if (inList && p->mTab != TabStats) {
            for (s32 i = 0; i < p->mCardNum; i++) {
                const Card& rCard = p->mCards[i];
                if (rCard.mShown && rCard.mRoot->IsVisible() &&
                    isInside(x, y, rCard.mRoot->mTranslate.x + p->mList->mTranslate.x, rCard.mRoot->mTranslate.y, p->mCardWidth, p->mCardHeight)) {
                    overCard = rCard.mOrder;
                }
            }
        }
        p->mOverBack = isInside(x, y, p->mBack.mX, p->mFooterY, p->mBack.mWidth, cBackHeight + 4.0f);
        if (!p->mDragging) {
            if (overTab >= 0) {
                p->mZone = ZoneTabs;
                p->mTabFocus = overTab;
            } else if (overFilter >= 0) {
                // The highlight on a filter is its own; the chosen one stays until A.
                p->mZone = ZoneList;
                p->mCardFocus = -1;
            } else {
                p->mZone = ZoneList;
                p->mCardFocus = overCard;
            }
        }

        if (MR::testCorePadTriggerA(WPAD_CHAN0)) {
            if (p->mOverBack) {
                return true;
            }
            if (overTab >= 0) {
                selectTab(p, overTab);
            } else if (overFilter >= 0) {
                selectFilter(p, overFilter);
            } else if (inList) {
                p->mDragging = true;
                p->mDragMoved = false;
                p->mDragStartY = y;
                p->mDragLastY = y;
                p->mDragStartScroll = p->mScroll;
                p->mScrollVelocity = 0.0f;
            }
        }
        if (p->mDragging) {
            if (!MR::testCorePadButtonA(WPAD_CHAN0)) {
                p->mDragging = false;
            } else {
                const f32 delta = y - p->mDragStartY;
                if (__builtin_fabsf(delta) > 5.0f) {
                    p->mDragMoved = true;
                }
                if (p->mDragMoved) {
                    p->mScroll = clamp(p->mDragStartScroll + delta, 0.0f, getMaxScroll(p));
                    p->mScrollTarget = p->mScroll;
                    p->mScrollVelocity = y - p->mDragLastY;
                }
                p->mDragLastY = y;
            }
        }
        return false;
    }

    // The mouse wheel: a row of the page a notch (the stats' tiles or the cards), up toward the top, wherever the
    // pointer is.
    void scrollByWheel(NebulaBoardParts* p, s32 wheel) {
        if (wheel == 0 || p->mDragging) {
            return;
        }
        const f32 row = p->mTab == TabStats || p->mShownNum == 0 ? cTileHeight + cGap : p->mCardHeight + cGap;
        p->mScrollVelocity = 0.0f;
        p->mScrollTarget = clamp(p->mScrollTarget - wheel * row / 120.0f, 0.0f, getMaxScroll(p));
    }
};  // namespace

NebulaAchievementBoard::NebulaAchievementBoard()
    : LayoutActor("NebulaAchievementBoard", true), mParts(nullptr), mBackground(nullptr), mIsOpen(false) {
}

void NebulaAchievementBoard::init(const JMapInfoIter& rIter) {
    // A reset while the last scene's board was open never closed it.
    setSharp(SharpBoard, false);
    initLayoutManager("InformationWindow", 0);
    LayoutManager* pManager = getLayoutManager();
    static const char cLayoutName[] = "NebulaAchievementBoard";
    delete[] pManager->mLayoutName;
    pManager->mLayoutName = new char[sizeof(cLayoutName)];
    memcpy(pManager->mLayoutName, cLayoutName, sizeof(cLayoutName));

    // The galaxy map's backdrop without its frame window (WinFrame): the board's own panel takes its place.
    mBackground = new GalaxyMapBackground();
    mBackground->initWithoutIter();
    MR::hidePane(mBackground, "WinFrame");
    mBackground->kill();
    initNerve(&NrvNebulaAchievementBoard::NebulaAchievementBoardNrvAppear::sInstance);
    kill();

    NebulaBoardParts* p = new NebulaBoardParts();
    memset(static_cast< void* >(p), 0, sizeof(NebulaBoardParts));
    new (&p->mText) NebulaPaneKit();
    new (&p->mWindow) NebulaPaneKit();
    new (&p->mButton) NebulaPaneKit();
    Pane* pRoot = pManager->mLayout->mpRootPane;
    if (pRoot == nullptr || !p->mText.init(pManager->mLayoutHolder) || !p->mButton.init(MR::createAndAddLayoutHolder("BackButton.arc"))) {
        JUTNativeLog("NebulaAchievementBoard: no InformationWindow or BackButton layout\n");
        return;
    }
    const bool ringed = p->mWindow.init(MR::createAndAddLayoutHolder("SysInfoWindowMini.arc"));
    // The information window's own panes stay hidden; the board is built beside them.
    for (nw4r::lyt::PaneList::Iterator it = pRoot->GetChildList().GetBeginIter(); it != pRoot->GetChildList().GetEndIter(); ++it) {
        it->SetVisible(false);
    }

    // The icons' textures, from the game's files.
    const s32 textureNum = NebulaAchievements::getIconTextureNum();
    p->mTextures = new nw4r::lyt::TexMap*[textureNum];
    for (s32 i = 0; i < textureNum; i++) {
        p->mTextures[i] = nullptr;
        const NebulaAchievementIcon* pIcon = NebulaAchievements::getIconTexture(i);
        if (pIcon->mLayoutArchive) {
            LayoutHolder* pHolder = MR::createAndAddLayoutHolder(pIcon->mArchive);
            void* pTpl = pHolder != nullptr ? pHolder->GetResource('timg', pIcon->mTexture, nullptr) : nullptr;
            if (pTpl != nullptr) {
                p->mTextures[i] = new nw4r::lyt::TexMap();
                p->mTextures[i]->ReplaceImage(pTpl, 0);
            }
        } else if (MR::isExistResourceInArc(pIcon->mArchive, pIcon->mTexture)) {
            p->mTextures[i] = MR::createLytTexMap(pIcon->mArchive, pIcon->mTexture);
        }
    }

    Pane* pBoard = makeGroup(pRoot, "NbBoard");
    p->mBoard = pBoard;
    if (pBoard == nullptr) {
        return;
    }
    p->mPanel = makeWindow(p, pBoard, "NbPanel");
    paint(p->mPanel, cPanelFill, cPanelRing);

    // The header: the crown and title, how many are unlocked.
    p->mTitleGlyph = makeText(p, pBoard, "NbTitleY", 8);
    p->mTitleShadow = makeText(p, pBoard, "NbTitleS", 64);
    p->mTitle = makeText(p, pBoard, "NbTitle", 64);
    p->mSummary = makeText(p, pBoard, "NbSum", 32);
    p->mSummaryTrack = makeWindow(p, pBoard, "NbSumTr");
    p->mSummaryBar = makeWindow(p, pBoard, "NbSumBar");
    p->mPercent = makeText(p, pBoard, "NbSumPc", 16);
    if (p->mPanel == nullptr || p->mTitleGlyph == nullptr || p->mTitleShadow == nullptr || p->mTitle == nullptr ||
        p->mSummary == nullptr || p->mSummaryTrack == nullptr || p->mSummaryBar == nullptr || p->mPercent == nullptr) {
        p->mBoard = nullptr;
        return;
    }
    paint(p->mSummaryTrack, cTrack, cTrack);
    paint(p->mSummaryBar, cGold, cGoldTop);
    wchar_t text[192];
    NebulaText::get(text, 64, "BoardTitle");
    setText(p->mTitle, text);
    setText(p->mTitleShadow, text);
    setTextStyle(p->mTitle, cTitleSize, cGoldTop, cGoldBottom, 3);
    setTextStyle(p->mTitleShadow, cTitleSize, cTextShadow, cTextShadow, 3);
    setGlyph(p->mTitleGlyph, cGlyphCrown, cTitleGlyph);

    // The tabs and the filter.
    static const char* const cTabIds[TabNum] = {"TabAll", "TabStory", "TabChallenges", "TabExtras", "TabStats"};
    for (s32 i = 0; i < TabNum; i++) {
        char name[17];
        snprintf(name, sizeof(name), "NbTab%d", i);
        if (!makeChip(p, pBoard, &p->mTabs[i], name, false)) {
            p->mBoard = nullptr;
            return;
        }
        NebulaText::get(text, 48, cTabIds[i]);
        setText(p->mTabs[i].mText, text);
    }
    static const char* const cFilterIds[FilterNum] = {"FilterAll", "FilterUnlocked", "FilterLocked"};
    for (s32 i = 0; i < FilterNum; i++) {
        char name[17];
        snprintf(name, sizeof(name), "NbFlt%d", i);
        if (!makeChip(p, pBoard, &p->mFilters[i], name, false)) {
            p->mBoard = nullptr;
            return;
        }
        NebulaText::get(text, 48, cFilterIds[i]);
        setText(p->mFilters[i].mText, text);
        setTextStyle(p->mFilters[i].mText, cFilterSize, cButtonText, cButtonText, 4);
    }
    p->mTabCount = makeText(p, pBoard, "NbTabCnt", 32);
    if (p->mTabCount == nullptr) {
        p->mBoard = nullptr;
        return;
    }
    setTextStyle(p->mTabCount, cFilterSize + 1.0f, cSubText, cSubText, 5);

    // The list: a card for each achievement, the stats page's titles and tiles.
    p->mList = makeGroup(pBoard, "NbList");
    if (p->mList == nullptr) {
        p->mBoard = nullptr;
        return;
    }
    p->mCardNum = NebulaAchievements::getNum();
    p->mCards = new Card[p->mCardNum];
    memset(static_cast< void* >(p->mCards), 0, sizeof(Card) * p->mCardNum);
    for (s32 i = 0; i < p->mCardNum; i++) {
        Card& rCard = p->mCards[i];
        char name[17];
        snprintf(name, sizeof(name), "NbC%02d", i);
        rCard.mRoot = makeGroup(p->mList, name);
        if (rCard.mRoot == nullptr) {
            p->mCardNum = i;
            break;
        }
        snprintf(name, sizeof(name), "NbC%02dF", i);
        rCard.mFocus = makeWindow(p, rCard.mRoot, name);
        snprintf(name, sizeof(name), "NbC%02dS", i);
        rCard.mShadow = makeWindow(p, rCard.mRoot, name);
        snprintf(name, sizeof(name), "NbC%02dB", i);
        rCard.mBody = makeWindow(p, rCard.mRoot, name);
        snprintf(name, sizeof(name), "NbC%02dI", i);
        rCard.mBadge = makeWindow(p, rCard.mRoot, name);
        snprintf(name, sizeof(name), "NbC%02dY", i);
        rCard.mGlyph = makeText(p, rCard.mRoot, name, 8);
        const NebulaAchievementIcon* pIcon = NebulaAchievements::getIcon(i);
        const s32 texture = pIcon != nullptr && pIcon->mGlyph == 0 ? NebulaAchievements::findIconTexture(pIcon) : -1;
        if (texture >= 0) {
            snprintf(name, sizeof(name), "NbC%02dP", i);
            rCard.mPicture = makePicture(rCard.mRoot, name, p->mTextures[texture]);
        }
        snprintf(name, sizeof(name), "NbC%02dn", i);
        rCard.mNameShadow = makeText(p, rCard.mRoot, name, 96);
        snprintf(name, sizeof(name), "NbC%02dN", i);
        rCard.mName = makeText(p, rCard.mRoot, name, 96);
        snprintf(name, sizeof(name), "NbC%02dH", i);
        rCard.mHint = makeText(p, rCard.mRoot, name, 256);
        snprintf(name, sizeof(name), "NbC%02dM", i);
        rCard.mMeta = makeText(p, rCard.mRoot, name, 48);
        snprintf(name, sizeof(name), "NbC%02dT", i);
        rCard.mTrack = makeWindow(p, rCard.mRoot, name);
        snprintf(name, sizeof(name), "NbC%02dR", i);
        rCard.mBar = makeWindow(p, rCard.mRoot, name);
        if (rCard.mFocus == nullptr || rCard.mShadow == nullptr || rCard.mBody == nullptr || rCard.mBadge == nullptr ||
            rCard.mGlyph == nullptr || rCard.mNameShadow == nullptr || rCard.mName == nullptr || rCard.mHint == nullptr ||
            rCard.mMeta == nullptr || rCard.mTrack == nullptr || rCard.mBar == nullptr) {
            p->mCardNum = i;
            break;
        }
        paint(rCard.mFocus, cFocus, cFocus);
        paint(rCard.mShadow, cShadow, cShadow);
        paint(rCard.mTrack, cTrack, cTrack);
        paint(rCard.mBar, cGold, cGoldTop);

        // Its name in the played language.
        NebulaAchievements::getName(text, 96, NebulaAchievements::getId(i));
        setText(rCard.mName, text);
        setText(rCard.mNameShadow, text);
        setTextStyle(rCard.mName, cNameSize, cWhite, cWhite, 0);
        setTextStyle(rCard.mNameShadow, cNameSize, cTextShadow, cTextShadow, 0);
        setTextStyle(rCard.mHint, cHintSize, cSubText, cSubText, 0);
        setTextStyle(rCard.mMeta, cMetaSize, cGold, cGold, 5);
    }

    for (s32 i = 0; i < 2; i++) {
        char name[17];
        snprintf(name, sizeof(name), "NbSec%d", i);
        p->mSections[i] = makeText(p, p->mList, name, 64);
        if (p->mSections[i] == nullptr) {
            p->mBoard = nullptr;
            return;
        }
        NebulaText::get(text, 64, i == 0 ? "StatsFile" : "StatsAllTime");
        setText(p->mSections[i], text);
        setTextStyle(p->mSections[i], cSectionSize, cGoldTop, cGoldBottom, 0);
    }
    for (s32 i = 0; i < StatNum; i++) {
        Tile& rTile = p->mTiles[i];
        char name[17];
        snprintf(name, sizeof(name), "NbS%02d", i);
        rTile.mRoot = makeGroup(p->mList, name);
        if (rTile.mRoot == nullptr) {
            p->mBoard = nullptr;
            return;
        }
        snprintf(name, sizeof(name), "NbS%02dB", i);
        rTile.mBody = makeWindow(p, rTile.mRoot, name);
        snprintf(name, sizeof(name), "NbS%02dY", i);
        rTile.mGlyph = makeText(p, rTile.mRoot, name, 8);
        if (i == StatJumps || i == StatLivesLost || i == StatChapters) {
            // A picture: the player's face (Mario's or Luigi's, chosen when the board opens), the storybook's cover.
            const char* pTexture = i == StatChapters ? "PictureBookCoverFront.bti" : "mymiimario.tpl";
            for (s32 t = 0; t < textureNum; t++) {
                if (strcmp(NebulaAchievements::getIconTexture(t)->mTexture, pTexture) == 0 && p->mTextures[t] != nullptr) {
                    snprintf(name, sizeof(name), "NbS%02dP", i);
                    rTile.mPicture = makePicture(rTile.mRoot, name, p->mTextures[t]);
                }
            }
        }
        snprintf(name, sizeof(name), "NbS%02dv", i);
        rTile.mValueShadow = makeText(p, rTile.mRoot, name, 48);
        snprintf(name, sizeof(name), "NbS%02dV", i);
        rTile.mValue = makeText(p, rTile.mRoot, name, 48);
        snprintf(name, sizeof(name), "NbS%02dL", i);
        rTile.mLabel = makeText(p, rTile.mRoot, name, 64);
        if (rTile.mBody == nullptr || rTile.mGlyph == nullptr || rTile.mValueShadow == nullptr || rTile.mValue == nullptr ||
            rTile.mLabel == nullptr) {
            p->mBoard = nullptr;
            return;
        }
        paint(rTile.mBody, cTileFill, cTileRing);
        NebulaText::get(text, 64, cStatLabels[i]);
        setText(rTile.mLabel, text);
        setTextStyle(rTile.mValueShadow, cValueSize, cTextShadow, cTextShadow, 0);
        setTextStyle(rTile.mValue, cValueSize, cWhite, cWhite, 0);
        setTextStyle(rTile.mLabel, cLabelSize, cSubText, cSubText, 0);
    }
    p->mEmpty = makeText(p, pBoard, "NbEmpty", 128);
    p->mScrollTrack = makeWindow(p, pBoard, "NbScrTr");
    p->mScrollThumb = makeWindow(p, pBoard, "NbScrTh");
    if (p->mEmpty == nullptr || p->mScrollTrack == nullptr || p->mScrollThumb == nullptr) {
        p->mBoard = nullptr;
        return;
    }
    setTextStyle(p->mEmpty, cNameSize, cSubText, cSubText, 4);
    paint(p->mScrollTrack, cTrack, cTrack);
    paint(p->mScrollThumb, 0xFFFFFFA0, 0xFFFFFFA0);

    // The footer: the game's Back button and its own label (B and "Back" in the played language); the pad's guide:
    // - and + for the tabs, the D-pad or stick to choose.
    if (!makeChip(p, pBoard, &p->mBack, "NbBack", true)) {
        p->mBoard = nullptr;
        return;
    }
    p->mBack.mGlyph = makeText(p, p->mBack.mRoot, "NbBackY", 8);
    if (p->mBack.mGlyph == nullptr || p->mBack.mArrow == nullptr) {
        p->mBoard = nullptr;
        return;
    }
    setGlyph(p->mBack.mGlyph, cGlyphB, cBackGlyph);
    NebulaText::get(text, 48, "BoardBack");
    const wchar_t* pBack = text;
    while (*pBack == L' ') {
        pBack++;
    }
    setText(p->mBack.mText, pBack);
    setTextStyle(p->mBack.mText, cFooterSize + 1.0f, cButtonText, cButtonText, 3);
    static const char* const cHintIds[HintNum] = {"HintTabs", "HintMove"};
    for (s32 i = 0; i < HintNum; i++) {
        char name[17];
        snprintf(name, sizeof(name), "NbHint%dY", i);
        p->mHintGlyphs[i] = makeText(p, pBoard, name, 16);
        snprintf(name, sizeof(name), "NbHint%dT", i);
        p->mHintLabels[i] = makeText(p, pBoard, name, 48);
        if (p->mHintGlyphs[i] == nullptr || p->mHintLabels[i] == nullptr) {
            p->mBoard = nullptr;
            return;
        }
        wchar_t glyphs[16] = {0};
        wchar_t* q = glyphs;
        if (i == HintTabs) {
            q = MR::addPictureFontTag(q, cGlyphMinus);
            q = MR::addPictureFontTag(q, cGlyphPlus);
        } else {
            MR::addPictureFontTag(q, cGlyphPad);
        }
        setText(p->mHintGlyphs[i], glyphs);
        p->mHintGlyphs[i]->SetFontSize(glyphFontSize(p->mHintGlyphs[i], cFooterGlyph));
        p->mHintGlyphs[i]->mCharSpace = 3.0f;
        setAnchor(p->mHintGlyphs[i], 4);
        NebulaText::get(text, 48, cHintIds[i]);
        setText(p->mHintLabels[i], text);
        setTextStyle(p->mHintLabels[i], cFooterSize, cSubText, cSubText, 5);
    }

    // The backdrop fades in and out with the board.
    if (Pane* pBackground = mBackground->getLayoutManager()->mLayout->mpRootPane) {
        setInfluencedAlphaRecursive(pBackground, 0);
    }

    mParts = p;
    pManager->rebuildPaneInfo();
    JUTNativeLog("NebulaAchievementBoard: %d cards, %s windows\n", p->mCardNum, ringed ? "ringed" : "plain");
}

void NebulaAchievementBoard::open() {
    NebulaBoardParts* p = mParts;
    if (p == nullptr || p->mBoard == nullptr) {
        return;
    }
    NebulaAchievements::flushStats();
    mIsOpen = true;
    setSharp(SharpBoard, true);
    p->mTab = TabAll;
    p->mFilterMode = FilterAll;
    p->mZone = ZoneList;
    p->mTabFocus = TabAll;
    p->mCardFocus = 0;
    p->mScroll = 0.0f;
    p->mScrollTarget = 0.0f;
    p->mScrollVelocity = 0.0f;
    p->mDragging = false;
    p->mPointerMode = false;
    p->mPointerIn = false;
    p->mOverBack = false;
    NebulaMenuFocus::holdCurrent(&p->mRepeat);
    (void)NebulaHost::pointerWheel();  // turns from before it opened scroll nothing
    p->mFrame = 0;
    p->mSwitchFrame = 0;
    p->mSwitchDirection = 0;
    p->mOpenLevel = 1.0f;
    p->mShown = false;
    p->mArranged = false;
    p->mBack.mFocusLevel = 0.0f;
    for (s32 i = 0; i < TabNum; i++) {
        p->mTabs[i].mFocusLevel = 0.0f;
    }
    for (s32 i = 0; i < FilterNum; i++) {
        p->mFilters[i].mFocusLevel = 0.0f;
    }

    // The cards: unlocked or not, the day, what the player did (or the locked line), the file's progress.
    NebulaFileProgress file;
    NebulaAchievements::getFileProgress(&file);
    p->mUnlockedNum = 0;
    for (s32 i = 0; i < p->mCardNum; i++) {
        Card& rCard = p->mCards[i];
        rCard.mDate = NebulaAchievements::getUnlockDate(i);
        rCard.mUnlocked = rCard.mDate != 0;
        p->mUnlockedNum += rCard.mUnlocked ? 1 : 0;
        rCard.mGoal = 0;
        if (!rCard.mUnlocked && !NebulaAchievements::getProgress(i, file, &rCard.mCurrent, &rCard.mGoal)) {
            rCard.mGoal = 0;
        }
        if (rCard.mUnlocked) {
            char id[48];
            snprintf(id, sizeof(id), "AchievementHint.%s", NebulaAchievements::getId(i));
            NebulaText::get(rCard.mHintText, 192, id);
        } else if (rCard.mGoal > 0) {
            // Its progress bar says enough.
            rCard.mHintText[0] = 0;
        } else {
            // What earns it stays a surprise until it is unlocked.
            NebulaText::get(rCard.mHintText, 192, "AchievementLocked");
        }
        wchar_t meta[48] = {0};
        if (rCard.mDate > 1) {
            formatDate(meta, 48, rCard.mDate);
        } else if (rCard.mGoal > 0) {
            formatCount(meta, 48, rCard.mCurrent, rCard.mGoal);
        }
        setText(rCard.mMeta, meta);
        const bool on = rCard.mUnlocked;
        paint(rCard.mBody, on ? cCardFill : cLockedFill, on ? cCardRing : cLockedRing);
        paint(rCard.mBadge, on ? cBadgeFill : cLockedBadgeFill, on ? cCardRing : cLockedRing);
        setTextColor(rCard.mName, on ? cWhite : cLockedText, on ? cWhite : cLockedText);
        setTextColor(rCard.mHint, on ? cSubText : cLockedSubText, on ? cSubText : cLockedSubText);
        setTextColor(rCard.mMeta, on ? cGold : cLockedText, on ? cGold : cLockedText);
        rCard.mNameShadow->SetVisible(on);
        setIcon(p, rCard.mGlyph, rCard.mPicture, NebulaAchievements::getIcon(i), cIcon, on ? cWhite : cIconLocked);
        rCard.mFocusLevel = 0.0f;
    }

    // The header: how many are unlocked.
    wchar_t text[48];
    formatCount(text, 48, p->mUnlockedNum, p->mCardNum);
    setText(p->mSummary, text);
    setTextStyle(p->mSummary, cSummarySize, cWhite, cWhite, 5);
    wchar_t percent[8];
    formatInt(percent, 8, p->mCardNum > 0 ? p->mUnlockedNum * 100 / p->mCardNum : 0, 1);
    const char* const percentNames[] = {"n"};
    const wchar_t* const percentValues[] = {percent};
    fill(text, 48, "BoardPercent", percentNames, percentValues, 1);
    setText(p->mPercent, text);
    setTextStyle(p->mPercent, cSummarySize, cGoldTop, cGoldBottom, 5);

    // The stats page's numbers.
    const NebulaAchievementIcon* pFace = nullptr;
    const NebulaAchievementIcon* pBook = nullptr;
    for (s32 i = 0; i < NebulaAchievements::getIconTextureNum(); i++) {
        const NebulaAchievementIcon* pIcon = NebulaAchievements::getIconTexture(i);
        if (strcmp(pIcon->mTexture, file.mMario ? "mymiimario.tpl" : "mymiiluigi.tpl") == 0) {
            pFace = pIcon;
        } else if (strcmp(pIcon->mTexture, "PictureBookCoverFront.bti") == 0) {
            pBook = pIcon;
        }
    }
    for (s32 i = 0; i < StatNum; i++) {
        Tile& rTile = p->mTiles[i];
        s32 current = -1;
        s32 goal = 0;
        u64 count = 0;
        s32 glyph = 0;  // 0: a picture
        const NebulaAchievementIcon* pPicture = pFace;
        switch (i) {
        case StatPowerStars:
            current = file.mPowerStars, goal = file.mPowerStarsMax, glyph = '7';
            break;
        case StatStarBits:
            count = static_cast< u64 >(file.mStarBits), glyph = ';';
            break;
        case StatLives:
            count = static_cast< u64 >(file.mLives), glyph = 'a';
            break;
        case StatChapters:
            current = file.mChapters, goal = 9, pPicture = pBook;
            break;
        case StatComets:
            current = file.mComets, goal = file.mCometsMax, glyph = 'e';
            break;
        case StatPurpleCoins:
            current = file.mPurpleCoins, goal = file.mPurpleCoinsMax, glyph = 'N';
            break;
        case StatHidden:
            current = file.mHidden, goal = file.mHiddenMax, glyph = 'Z';
            break;
        case StatGreenStars:
            current = file.mGreenStars, goal = 3, glyph = 'O';
            break;
        case StatLumas:
            current = file.mLumasFed, goal = file.mLumasFedMax, glyph = 'c';
            break;
        case StatJumps:
            count = NebulaAchievements::getStat(NebulaStat_Jumps);
            break;
        case StatSpins:
            count = NebulaAchievements::getStat(NebulaStat_Spins), glyph = '3';
            break;
        case StatCoins:
            count = NebulaAchievements::getStat(NebulaStat_Coins), glyph = 'A';
            break;
        case StatStarBitsCollected:
            count = NebulaAchievements::getStat(NebulaStat_StarBits), glyph = ';';
            break;
        case StatLivesLost:
            count = NebulaAchievements::getStat(NebulaStat_LivesLost);
            break;
        case StatGameOvers:
            count = NebulaAchievements::getStat(NebulaStat_GameOvers), glyph = 'S';
            break;
        case StatOneUps:
            count = NebulaAchievements::getStat(NebulaStat_OneUps), glyph = 'a';
            break;
        case StatPlayTime:
            count = NebulaAchievements::getStat(NebulaStat_PlaySeconds), glyph = 'W';
            break;
        }
        wchar_t value[48];
        if (i == StatPlayTime) {
            wchar_t hours[16], minutes[8];
            formatNumber(hours, 16, count / 3600);
            formatInt(minutes, 8, static_cast< s32 >(count / 60 % 60), 1);
            const char* const names[] = {"h", "m"};
            const wchar_t* const values[] = {hours, minutes};
            fill(value, 48, "PlayTime", names, values, 2);
        } else if (current >= 0 && goal > 0) {
            formatCount(value, 48, current, goal);
        } else {
            formatNumber(value, 48, count);
        }
        setText(rTile.mValue, value);
        setText(rTile.mValueShadow, value);
        if (glyph == 0) {
            setIcon(p, rTile.mGlyph, rTile.mPicture, pPicture, 36.0f, cWhite);
        } else {
            const NebulaAchievementIcon icon = {glyph, nullptr, false, nullptr, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f};
            setIcon(p, rTile.mGlyph, nullptr, &icon, 36.0f, cWhite);
            if (rTile.mPicture != nullptr) {
                rTile.mPicture->SetVisible(false);
            }
        }
    }

    LayoutActor::appear();
    setNerve(&NrvNebulaAchievementBoard::NebulaAchievementBoardNrvAppear::sInstance);
}

bool NebulaAchievementBoard::isOpen() const {
    return mIsOpen;
}

void NebulaAchievementBoard::update() {
    movement();
    mBackground->movement();
    calcAnim();
    mBackground->calcAnim();
}

void NebulaAchievementBoard::drawOnTop() const {
    NebulaBoardParts* p = mParts;
    if (!mIsOpen || p == nullptr || p->mBoard == nullptr || !p->mShown) {
        return;
    }
    mBackground->draw();
    // The board, then its list alone, cut to the list's window so that what scrolls out never covers the tabs or the
    // footer.
    const bool listShown = p->mList->IsVisible();
    p->mList->SetVisible(false);
    draw();
    p->mList->SetVisible(listShown);
    if (!listShown || getLayoutManager()->mIsScreenHidden) {
        return;
    }
    drawClipped(p->mList, getLayoutManager()->mDrawInfo, p->mBoard->mTranslate.y, p->mListLeft - 16.0f, p->mListRight + 10.0f,
                p->mListTop + 2.0f, p->mListBottom - 6.0f);
}

void NebulaAchievementBoard::control() {
    NebulaBoardParts* p = mParts;
    if (p == nullptr || p->mBoard == nullptr) {
        return;
    }
    // A new screen shape (a resized window) lays the board out again.
    f32 scaleX, width, height;
    readScreen(&scaleX, &width, &height);
    if (!p->mArranged || __builtin_fabsf(width - p->mWidth) > 0.5f || __builtin_fabsf(height - p->mHeight) > 0.5f || scaleX != p->mScaleX) {
        arrange(p);
        followFocus(p);
    }

    p->mFrame++;
    if (p->mSwitchFrame < cSwitchFrames + 40) {
        p->mSwitchFrame++;
    }
    // The scroll eases to its target; a released drag glides on.
    if (!p->mDragging) {
        if (p->mScrollVelocity != 0.0f) {
            p->mScrollTarget = clamp(p->mScrollTarget + p->mScrollVelocity, 0.0f, getMaxScroll(p));
            p->mScrollVelocity *= 0.9f;
            if (__builtin_fabsf(p->mScrollVelocity) < 0.2f) {
                p->mScrollVelocity = 0.0f;
            }
        }
        p->mScroll += (p->mScrollTarget - p->mScroll) * 0.25f;
        if (__builtin_fabsf(p->mScrollTarget - p->mScroll) < 0.05f) {
            p->mScroll = p->mScrollTarget;
        }
    }
    place(p, p->mOpenLevel, p->mFrame);
}

// The world fades to black (the game's system wipe, drawn over every layout); the board is there when it opens.
void NebulaAchievementBoard::exeAppear() {
    NebulaBoardParts* p = mParts;
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_GALAMAP_WINDOW_OPEN");
        MR::closeSystemWipeFade(cFadeFrames);
        return;
    }
    if (MR::isSystemWipeActive()) {
        return;
    }
    p->mShown = true;
    p->mSwitchFrame = 0;
    mBackground->appear();
    mBackground->getLayoutManager()->mLayout->mpRootPane->mAlpha = 255;
    MR::openSystemWipeFade(cFadeFrames);
    setNerve(&NrvNebulaAchievementBoard::NebulaAchievementBoardNrvWait::sInstance);
}

void NebulaAchievementBoard::exeWait() {
    NebulaBoardParts* p = mParts;
    const s32 wheel = NebulaHost::pointerWheel();
    if (MR::isSystemWipeActive()) {
        return;  // the board takes no input while it comes out of the black
    }
    scrollByWheel(p, wheel);
    if (MR::testSystemTriggerB()) {
        MR::startSystemSE("SE_SY_GALAMAP_WINDOW_CLOSE");
        setNerve(&NrvNebulaAchievementBoard::NebulaAchievementBoardNrvDisappear::sInstance);
        return;
    }
    if (MR::testCorePadTriggerMinus(WPAD_CHAN0)) {
        stepTab(p, -1);
    } else if (MR::testCorePadTriggerPlus(WPAD_CHAN0)) {
        stepTab(p, 1);
    }
    const s32 press = NebulaMenuFocus::readPress(&p->mRepeat);
    if (press != 0) {
        if (p->mPointerMode) {
            // The D-pad takes over from where the pointer was.
            p->mPointerMode = false;
            p->mOverBack = false;
            if (p->mZone == ZoneList && p->mCardFocus < 0) {
                p->mCardFocus = 0;
                followFocus(p);
                MR::startSystemSE("SE_SY_BUTTON_CURSOR_ON");
            } else {
                moveFocus(p, press);
            }
        } else {
            moveFocus(p, press);
        }
    } else if (handlePointer(p)) {
        MR::startSystemSE("SE_SY_GALAMAP_WINDOW_CLOSE");
        setNerve(&NrvNebulaAchievementBoard::NebulaAchievementBoardNrvDisappear::sInstance);
        return;
    }
}

// The board fades to black; the world comes back out of it (GameScene returns to play as it opens).
void NebulaAchievementBoard::exeDisappear() {
    NebulaBoardParts* p = mParts;
    if (MR::isFirstStep(this)) {
        MR::closeSystemWipeFade(cFadeFrames);
        return;
    }
    if (MR::isSystemWipeActive()) {
        return;
    }
    p->mShown = false;
    mBackground->kill();
    kill();
    mIsOpen = false;
    setSharp(SharpBoard, false);
    NebulaAchievements::flushStats();
    MR::openSystemWipeFade(cFadeFrames);
}
#endif
