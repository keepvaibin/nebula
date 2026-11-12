#include "Game/System/NebulaText.hpp"
#include <nebula_module.h>

#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/System/GameAssets.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/Language.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/MessageTag.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>
#include <Game/Ui/TextBox.hpp>
#include <Game/Ui/Rect.hpp>

namespace {
#include "NebulaTextTable.inc"

    struct Override {
        const char* mId;  // in sFile, not terminated
        s32 mIdLength;
        s32 mText;  // into sText
    };

    // One allocation sized by the file (load): its overrides, their texts, the file's bytes (the ids point into them)
    // and the room a token's id is copied to (as long as the longest text, the file's or the table's).
    Override* sOverrides = nullptr;
    s32 sOverrideNum = 0;
    char16_t* sText = nullptr;
    s32 sTextUsed = 0;
    u8* sFile = nullptr;
    char* sTokenId = nullptr;
    s32 sColumn = 0;
    bool sLoaded = false;

    // The talk getTalk builds, sized the first time it is asked for (the played language's texts do not change while
    // the game runs): the talk with room for a break after every unit, measureLine's copy of a line, and fitTalk's
    // breaks, one per unit at most. One allocation.
    char16_t* sTalk = nullptr;
    s32 sTalkRoom = 0;
    char16_t* sTalkLine = nullptr;
    s32* sTalkBreaks = nullptr;
    s32* sTalkChosen = nullptr;
    s32* sTalkTrial = nullptr;
    bool sTalkFitted = false;

    s32 lengthOf(const char* pText) {
        s32 length = 0;
        while (pText[length] != 0) {
            length++;
        }
        return length;
    }

    s32 lengthOf(const char16_t* pText) {
        s32 length = 0;
        while (pText[length] != 0) {
            length++;
        }
        return length;
    }

    bool same(const char* pA, s32 aLength, const char* pB, s32 bLength) {
        return aLength == bLength && memcmp(pA, pB, aLength) == 0;
    }

    // Whether the terminated text pA is the `bLength` bytes of pB (a terminated text of that length). Stops at the first
    // difference: most of the table's ids differ in their first characters, and find asks for every row.
    bool sameTerminated(const char* pA, const char* pB, s32 bLength) {
        for (s32 i = 0; i < bLength; i++) {
            if (pA[i] != pB[i]) {
                return false;
            }
        }
        return pA[bLength] == 0;
    }

    s32 findColumn(const char* pLanguage, s32 length) {
        for (s32 i = 0; i < cNebulaTextLanguageNum; i++) {
            if (same(cNebulaTextLanguages[i], lengthOf(cNebulaTextLanguages[i]), pLanguage, length)) {
                return i;
            }
        }
        return -1;
    }

    // The other spelling of the same language (American and European English, French, Spanish): -1 when none.
    s32 getSister(s32 column) {
        static const char* const cPairs[][2] = {
            {"UsEnglish", "EuEnglish"},
            {"UsFrench", "EuFrench"},
            {"UsSpanish", "EuSpanish"},
        };
        const char* pName = cNebulaTextLanguages[column];
        const s32 nameLength = lengthOf(pName);
        for (u32 i = 0; i < sizeof(cPairs) / sizeof(cPairs[0]); i++) {
            for (s32 side = 0; side < 2; side++) {
                if (same(pName, nameLength, cPairs[i][side], lengthOf(cPairs[i][side]))) {
                    return findColumn(cPairs[i][1 - side], lengthOf(cPairs[i][1 - side]));
                }
            }
        }
        return -1;
    }

    // One UTF-8 line's value into sText, with \n and \\ unescaped; its start. A value's bytes make at most as many
    // UTF-16 units (a 4-byte sequence makes 2), which load sized sText by.
    s32 storeValue(const u8* pValue, const u8* pEnd) {
        const s32 start = sTextUsed;
        while (pValue < pEnd) {
            u32 code = *pValue++;
            if (code == '\\' && pValue < pEnd && (*pValue == 'n' || *pValue == '\\')) {
                code = *pValue++ == 'n' ? '\n' : '\\';
            } else if (code >= 0xC0) {
                const s32 extra = code >= 0xF0 ? 3 : code >= 0xE0 ? 2 : 1;
                code &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : 0x1F;
                for (s32 i = 0; i < extra && pValue < pEnd && (*pValue & 0xC0) == 0x80; i++) {
                    code = (code << 6) | (*pValue++ & 0x3F);
                }
            }
            if (code >= 0x10000) {
                code -= 0x10000;
                sText[sTextUsed++] = static_cast< char16_t >(0xD800 + (code >> 10));
                sText[sTextUsed++] = static_cast< char16_t >(0xDC00 + (code & 0x3FF));
            } else {
                sText[sTextUsed++] = static_cast< char16_t >(code);
            }
        }
        sText[sTextUsed++] = 0;
        return start;
    }

    bool isSpace(u8 c) {
        return c == ' ' || c == '\t' || c == '\r';
    }

    // "id = text" lines; # starts a comment; "language = EuGerman" picks the built-in column the rest falls back to.
    void parse(const u8* pText, s32 length) {
        const u8* p = pText;
        const u8* pEnd = pText + length;
        if (length >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) {
            p += 3;
        }
        while (p < pEnd) {
            const u8* pLine = p;
            while (p < pEnd && *p != '\n') {
                p++;
            }
            const u8* pLineEnd = p;
            if (p < pEnd) {
                p++;
            }
            while (pLine < pLineEnd && isSpace(*pLine)) {
                pLine++;
            }
            while (pLineEnd > pLine && isSpace(pLineEnd[-1])) {
                pLineEnd--;
            }
            if (pLine == pLineEnd || *pLine == '#') {
                continue;
            }
            const u8* pEqual = pLine;
            while (pEqual < pLineEnd && *pEqual != '=') {
                pEqual++;
            }
            if (pEqual == pLineEnd) {
                continue;
            }
            const u8* pKeyEnd = pEqual;
            while (pKeyEnd > pLine && isSpace(pKeyEnd[-1])) {
                pKeyEnd--;
            }
            const u8* pValue = pEqual + 1;
            while (pValue < pLineEnd && isSpace(*pValue)) {
                pValue++;
            }
            const char* pKey = reinterpret_cast< const char* >(pLine);
            const s32 keyLength = static_cast< s32 >(pKeyEnd - pLine);
            if (keyLength <= 0) {
                continue;
            }
            if (same(pKey, keyLength, "language", 8)) {
                const s32 column = findColumn(reinterpret_cast< const char* >(pValue), static_cast< s32 >(pLineEnd - pValue));
                if (column >= 0) {
                    sColumn = column;
                } else {
                    nebula_log("NebulaText: unknown language in NebulaText.txt\n");
                }
                continue;
            }
            Override& rOverride = sOverrides[sOverrideNum++];
            rOverride.mId = pKey;
            rOverride.mIdLength = keyLength;
            rOverride.mText = storeValue(pValue, pLineEnd);
        }
    }

    // The texts are the played language's: until the game's objects (and its language) exist, a lookup (a mod's report
    // at its start) answers from the table in English, loads nothing and expands no token; the first after loads once.
    void load();

    bool ready() {
        if (sLoaded) {
            return true;
        }
        const GameSystem* const pSystem = SingletonHolder< GameSystem >::get();
        if (pSystem == nullptr || pSystem->mObjHolder == nullptr) {
            return false;
        }
        load();
        return true;
    }

    void load() {
        sLoaded = true;
        const char* pPrefix = MR::getCurrentLanguagePrefix();
        const s32 column = findColumn(pPrefix, lengthOf(pPrefix));
        sColumn = column >= 0 ? column : 0;
        nebula_log("NebulaText: %s\n", pPrefix);

        MR::FormattedName path("/%s/MessageData/NebulaText.txt", pPrefix);
        GameAssets::File file;
        const u32 length = GameAssets::open(path.c_str(), &file) ? file.mLength : 0;
        // An override's line holds at least its id, '=' and (all but the last) a line feed: at most (length + 1) / 3
        // of them. Their values make at most `length` units, and each its terminator.
        const u32 overrideRoom = (length + 1) / 3;
        const u32 textRoom = length + overrideRoom;
        u32 longest = length;
        for (u32 i = 0; i < sizeof(cNebulaTextRows) / sizeof(cNebulaTextRows[0]); i++) {
            for (s32 c = 0; c < cNebulaTextLanguageNum; c++) {
                const char16_t* pRowText = cNebulaTextRows[i].mText[c];
                const u32 units = pRowText != nullptr ? static_cast< u32 >(lengthOf(pRowText)) : 0;
                longest = units > longest ? units : longest;
            }
        }
        const u32 bytes = overrideRoom * sizeof(Override) + textRoom * sizeof(char16_t) + length + longest + 1;
        u8* pBlock = static_cast< u8* >(NebulaArena::alloc(bytes, 8, NebulaArena::root()));
        sOverrides = reinterpret_cast< Override* >(pBlock);
        sText = reinterpret_cast< char16_t* >(pBlock + overrideRoom * sizeof(Override));
        sFile = reinterpret_cast< u8* >(sText + textRoom);
        sTokenId = reinterpret_cast< char* >(sFile + length);
        if (length > 0 && GameAssets::read(&file, sFile, static_cast< s32 >(length), 0) >= 0) {
            parse(sFile, static_cast< s32 >(length));
            nebula_log("NebulaText: %s (%d texts)\n", path.c_str(), sOverrideNum);
        }
    }

    const char16_t* find(const char* pId) {
        const s32 idLength = lengthOf(pId);
        for (s32 i = sOverrideNum - 1; i >= 0; i--) {
            if (same(sOverrides[i].mId, sOverrides[i].mIdLength, pId, idLength)) {
                return &sText[sOverrides[i].mText];
            }
        }
        for (u32 i = 0; i < sizeof(cNebulaTextRows) / sizeof(cNebulaTextRows[0]); i++) {
            const NebulaTextRow& rRow = cNebulaTextRows[i];
            if (!sameTerminated(rRow.mId, pId, idLength)) {
                continue;
            }
            if (rRow.mText[sColumn] != nullptr) {
                return rRow.mText[sColumn];
            }
            const s32 sister = getSister(sColumn);
            if (sister >= 0 && rRow.mText[sister] != nullptr) {
                return rRow.mText[sister];
            }
            return rRow.mText[0];
        }
        return nullptr;
    }

    // wcsncmp would read 32-bit units on Linux and Android (the game's char16_t is 16-bit, -fshort-wchar).
    bool startsWith(const char16_t* pText, const char16_t* pPrefix) {
        for (; *pPrefix != 0; pText++, pPrefix++) {
            if (*pText != *pPrefix) {
                return false;
            }
        }
        return true;
    }

    // Where a text is expanded to: the first `mCapacity` units of mDst, the last kept for the terminator; with no mDst
    // only counted.
    struct Sink {
        char16_t* mDst;
        s32 mCapacity;
        s32 mLength;

        bool fits(s32 units) const {
            return mDst == nullptr || mLength + units < mCapacity;
        }

        void put(char16_t unit) {
            if (mDst != nullptr) {
                mDst[mLength] = unit;
            }
            mLength++;
        }
    };

    void expand(Sink* pSink, const char* pId) {
        const bool loaded = ready();
        const char16_t* pText = find(pId);
        if (pText == nullptr) {
            for (s32 i = 0; pId[i] != 0 && pSink->fits(1); i++) {
                pSink->put(static_cast< char16_t >(pId[i]));
            }
            return;
        }
        if (!loaded) {
            for (; *pText != 0 && pSink->fits(1); pText++) {
                pSink->put(*pText);
            }
            return;
        }

        // {galaxy:Name}: the galaxy's name in the game's text; {message:Id}: one of the game's messages (a dome's name,
        // Layout_MapGrandGalaxyOne), its tags left out. The id itself when the game has neither.
        static const char16_t cGalaxy[] = u"{galaxy:";
        static const char16_t cMessage[] = u"{message:";
        static const char16_t cPlayer[] = u"{player}";
        while (*pText != 0) {
            // {player}: the game's own tag for the player's name (Mario or Luigi, as the file is played).
            if (startsWith(pText, cPlayer)) {
                // Copied whole: a tag's parameters may be zero (the game's text is measured tag by tag).
                static const char16_t cPlayerTag[] = {cMessageTagEscape, makeMessageTagHead(MessageTagGroup_Localize, 8), 0x0000, 0x0000};
                const s32 units = sizeof(cPlayerTag) / sizeof(cPlayerTag[0]);
                if (pSink->fits(units)) {
                    for (s32 i = 0; i < units; i++) {
                        pSink->put(cPlayerTag[i]);
                    }
                }
                pText += sizeof(cPlayer) / sizeof(cPlayer[0]) - 1;
                continue;
            }
            const char16_t* pClose = nullptr;
            s32 tokenLength = 0;
            if (startsWith(pText, cGalaxy)) {
                tokenLength = sizeof(cGalaxy) / sizeof(cGalaxy[0]) - 1;
            } else if (startsWith(pText, cMessage)) {
                tokenLength = sizeof(cMessage) / sizeof(cMessage[0]) - 1;
            }
            if (tokenLength > 0) {
                for (const char16_t* p = pText + tokenLength; *p != 0; p++) {
                    if (*p == u'}') {
                        pClose = p;
                        break;
                    }
                }
            }
            if (pClose == nullptr) {
                if (pSink->fits(1)) {
                    pSink->put(*pText);
                }
                pText++;
                continue;
            }
            // The id fits sTokenId: it is part of a text, and sTokenId is as long as the longest.
            s32 n = 0;
            for (const char16_t* p = pText + tokenLength; p < pClose; p++) {
                sTokenId[n++] = static_cast< char >(*p);
            }
            sTokenId[n] = 0;
            const bool galaxy = startsWith(pText, cGalaxy);
            const char16_t* pName = galaxy ? MR::getGalaxyNameOnCurrentLanguage(sTokenId) : MR::getGameMessageDirect(sTokenId);
            if (pName != nullptr) {
                // The game's text without its tags.
                for (const char16_t* p = pName; *p != 0 && pSink->fits(1);) {
                    if (*p == cMessageTagEscape && p[1] != 0) {
                        // Its whole size: a tag's parameters may be zero.
                        const s32 units = getMessageTagUnits(p[1]);
                        p += units > 0 ? units : 1;
                        continue;
                    }
                    pSink->put(*p++);
                }
            } else {
                for (s32 i = 0; i < n && pSink->fits(1); i++) {
                    pSink->put(static_cast< char16_t >(sTokenId[i]));
                }
            }
            pText = pClose + 1;
        }
    }

    // The units get gives for `pId` with room for all of them.
    s32 measure(const char* pId) {
        Sink sink = {nullptr, 0, 0};
        expand(&sink, pId);
        return sink.mLength;
    }
};  // namespace

namespace NebulaText {
    bool has(const char* pId) {
        (void)ready();
        return find(pId) != nullptr;
    }

    s32 get(char16_t* pDst, s32 capacity, const char* pId) {
        if (capacity <= 0) {
            return 0;
        }
        Sink sink = {pDst, capacity, 0};
        expand(&sink, pId);
        pDst[sink.mLength] = 0;
        return sink.mLength;
    }

    s32 length(const char* pId) {
        return measure(pId);
    }

    const char16_t* english(const char* pId) {
        const s32 idLength = lengthOf(pId);
        for (u32 i = 0; i < sizeof(cNebulaTextRows) / sizeof(cNebulaTextRows[0]); i++) {
            if (sameTerminated(cNebulaTextRows[i].mId, pId, idLength)) {
                return cNebulaTextRows[i].mText[0];
            }
        }
        return nullptr;
    }

    const char16_t* get(MR::GrowBuffer< char16_t >* pText, const char* pId) {
        const s32 units = measure(pId) + 1;
        get(pText->room(units), units, pId);
        return pText->mItems;
    }

    // Japanese and Chinese lines may break between characters (Korean, as English, at spaces): before a wide character
    // that is not closing punctuation or a small kana, after one that is not an opening bracket.
    bool breaksBefore(char16_t c, char16_t previous) {
        static const u16 cNoStart[] = {0x3001, 0x3002, 0xFF0C, 0xFF0E, 0xFF01, 0xFF1F, 0xFF1A, 0xFF1B, 0x300D, 0x300F,
                                       0xFF09, 0x3011, 0x3009, 0x300B, 0x30FC, 0x2026, 0xFF5E, 0x30FB, 0x3041, 0x3043,
                                       0x3045, 0x3047, 0x3049, 0x3063, 0x3083, 0x3085, 0x3087, 0x308E, 0x30A1, 0x30A3,
                                       0x30A5, 0x30A7, 0x30A9, 0x30C3, 0x30E3, 0x30E5, 0x30E7, 0x30EE};
        static const u16 cNoEnd[] = {0x300C, 0x300E, 0xFF08, 0x3010, 0x3008, 0x300A};
        const u16 unit = static_cast< u16 >(c);
        const bool hangul = unit >= 0x3130 && unit < 0x3190;
        const bool wide = !hangul && ((unit >= 0x2E80 && unit < 0xA000) || (unit >= 0xF900 && unit < 0xFB00) || (unit >= 0xFF00 && unit < 0xFFF0));
        if (!wide) {
            return false;
        }
        for (u32 i = 0; i < sizeof(cNoStart) / sizeof(cNoStart[0]); i++) {
            if (unit == cNoStart[i]) {
                return false;
            }
        }
        for (u32 i = 0; i < sizeof(cNoEnd) / sizeof(cNoEnd[0]); i++) {
            if (static_cast< u16 >(previous) == cNoEnd[i]) {
                return false;
            }
        }
        return true;
    }

    // The width of pText[0, count) as pBox draws it: its font, size and spacing, and its tags, set as the game sets a
    // message (LayoutCoreUtil::setTextBoxMessage: the player's name written out, the length counted tag by tag).
    f32 measureLine(Ui::TextBox* pBox, const char16_t* pText, s32 count) {
        const s32 length = count < sTalkRoom - 1 ? count : sTalkRoom - 1;
        memcpy(sTalkLine, pText, length * sizeof(char16_t));
        sTalkLine[length] = 0;
        LayoutCoreUtil::setTextBoxMessage(pBox, sTalkLine);
        const Ui::Size size = pBox->mSize;
        pBox->mSize.width = 4000.0f;
        const Ui::Rect rect = pBox->GetTextDrawRect();
        pBox->mSize = size;
        return rect.right - rect.left;
    }

    // The talk's lines are fitted to the balloon the first time it shows (fitTalk).
    const char16_t* getTalk(const char* pId) {
        if (!same(pId, lengthOf(pId), "AchievementLuma", 15)) {
            return nullptr;
        }
        if (sTalk == nullptr) {
            // Both pages, the page tag between them and the terminator; then a break after every unit.
            sTalkRoom = (measure("LumaGreeting") + 3 + measure("LumaBoard") + 1) * 2;
            const u32 bytes = sTalkRoom * (3 * sizeof(s32) + 2 * sizeof(char16_t));
            u8* pBlock = static_cast< u8* >(NebulaArena::alloc(bytes, 4, NebulaArena::root()));
            sTalkBreaks = reinterpret_cast< s32* >(pBlock);
            sTalkChosen = sTalkBreaks + sTalkRoom;
            sTalkTrial = sTalkChosen + sTalkRoom;
            sTalk = reinterpret_cast< char16_t* >(sTalkTrial + sTalkRoom);
            sTalkLine = sTalk + sTalkRoom;
        }
        s32 length = get(sTalk, sTalkRoom, "LumaGreeting");
        // The next page (the game's <1:1> tag).
        sTalk[length++] = cMessageTagEscape;
        sTalk[length++] = cMessageTagNextPageHead;
        sTalk[length++] = cMessageTagNextPage;
        get(sTalk + length, sTalkRoom - length, "LumaBoard");
        sTalkFitted = false;
        return sTalk;
    }

    // Where a paragraph may break: at a space (not one French keeps with its punctuation: before ! ? : ; », after «),
    // or before a Japanese or Chinese character. Offsets from the paragraph's start; at most `max`.
    s32 findBreaks(const char16_t* pText, s32 length, s32* pBreaks, s32 max) {
        s32 count = 0;
        for (s32 i = 0; i < length && count < max; i++) {
            const char16_t c = pText[i];
            if (c == cMessageTagEscape && i + 1 < length && pText[i + 1] != 0) {
                // A tag, skipped whole: its parameters are not text.
                const s32 units = getMessageTagUnits(pText[i + 1]);
                i += (units > 0 ? units : 1) - 1;
                continue;
            }
            if (i == 0) {
                continue;
            }
            if (c == u' ') {
                const char16_t next = i + 1 < length ? pText[i + 1] : 0;
                const bool keptBefore = next == u'!' || next == u'?' || next == u':' || next == u';' || next == 0xBB;
                if (!keptBefore && pText[i - 1] != 0xAB) {
                    pBreaks[count++] = i;
                }
            } else if (breaksBefore(c, pText[i - 1])) {
                pBreaks[count++] = i;
            }
        }
        return count;
    }

    // The breaks a greedy setter takes at `limit`: each line runs to the last break it fits before. Their number.
    s32 setGreedy(Ui::TextBox* pBox, const char16_t* pText, s32 length, const s32* pBreaks, s32 breakNum, f32 limit, s32* pChosen) {
        s32 chosen = 0;
        s32 lineStart = 0;
        s32 last = -1;  // the last break the current line fits before
        for (s32 b = 0; b <= breakNum; b++) {
            const s32 at = b < breakNum ? pBreaks[b] : length;
            if (measureLine(pBox, pText + lineStart, at - lineStart) > limit && last >= 0) {
                pChosen[chosen++] = pBreaks[last];
                lineStart = pBreaks[last] + (pText[pBreaks[last]] == u' ' ? 1 : 0);
                last = -1;
                b--;
                continue;
            }
            last = b < breakNum ? b : last;
        }
        return chosen;
    }

    bool isTalk(const char16_t* pMessage) {
        return sTalk != nullptr && pMessage >= sTalk && pMessage < sTalk + sTalkRoom;
    }

    void fitTalk(const char16_t* pMessage, Ui::TextBox* pBox) {
        if (pBox == nullptr || sTalkFitted || !isTalk(pMessage)) {
            return;
        }
        sTalkFitted = true;
        const f32 limit = pBox->mSize.width;
        // Its length: tags skipped whole (the player's name is a tag whose parameters are zero).
        s32 length = 0;
        while (sTalk[length] != 0) {
            const s32 units = sTalk[length] == cMessageTagEscape && sTalk[length + 1] != 0 ? getMessageTagUnits(sTalk[length + 1]) : 1;
            length += units > 0 ? units : 1;
        }
        // Paragraph by paragraph (a line break or a page ends one): as few lines as the box allows, and those as even
        // as they can be (the narrowest width that still needs no more lines), as the game's writers break them.
        s32 paragraph = 0;
        while (paragraph <= length) {
            s32 paragraphEnd = paragraph;
            while (paragraphEnd < length && sTalk[paragraphEnd] != u'\n' && !(sTalk[paragraphEnd] == cMessageTagEscape && sTalk[paragraphEnd + 1] == cMessageTagNextPageHead)) {
                if (sTalk[paragraphEnd] == cMessageTagEscape && sTalk[paragraphEnd + 1] != 0) {
                    // Its whole size: a tag's parameters may be zero.
                    const s32 units = getMessageTagUnits(sTalk[paragraphEnd + 1]);
                    paragraphEnd += units > 0 ? units : 1;
                } else {
                    paragraphEnd++;
                }
            }
            if (paragraphEnd > length) {
                paragraphEnd = length;
            }
            char16_t* pText = sTalk + paragraph;
            const s32 textLength = paragraphEnd - paragraph;
            // As many as the text has places: one per unit at most.
            s32* breaks = sTalkBreaks;
            s32* chosen = sTalkChosen;
            s32* trial = sTalkTrial;
            const s32 breakNum = findBreaks(pText, textLength, breaks, sTalkRoom);
            s32 chosenNum = setGreedy(pBox, pText, textLength, breaks, breakNum, limit, chosen);
            if (chosenNum > 0) {
                f32 low = 0.0f;
                f32 high = limit;
                for (s32 step = 0; step < 12; step++) {
                    const f32 middle = (low + high) * 0.5f;
                    if (setGreedy(pBox, pText, textLength, breaks, breakNum, middle, trial) == chosenNum) {
                        high = middle;
                    } else {
                        low = middle;
                    }
                }
                chosenNum = setGreedy(pBox, pText, textLength, breaks, breakNum, high, chosen);
            }
            // From the last break back, so the earlier offsets hold: a space becomes the break; before a Japanese or
            // Chinese character one is put in.
            s32 added = 0;
            for (s32 i = chosenNum - 1; i >= 0; i--) {
                char16_t* pBreak = pText + chosen[i];
                if (*pBreak == u' ') {
                    *pBreak = u'\n';
                } else if (length + 1 < sTalkRoom) {
                    memmove(pBreak + 1, pBreak, (length + 1 - (pBreak - sTalk)) * sizeof(char16_t));
                    *pBreak = u'\n';
                    length++;
                    added++;
                }
            }
            paragraphEnd += added;
            if (paragraphEnd >= length) {
                break;
            }
            if (sTalk[paragraphEnd] == u'\n') {
                paragraph = paragraphEnd + 1;
            } else {
                const s32 units = getMessageTagUnits(sTalk[paragraphEnd + 1]);
                paragraph = paragraphEnd + (units > 0 ? units : 1);
            }
        }
    }
};  // namespace NebulaText
