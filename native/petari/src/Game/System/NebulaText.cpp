#include "Game/System/NebulaText.hpp"

#ifdef TARGET_PC
#include "Game/Screen/LayoutCoreUtil.hpp"
#include "Game/System/Language.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nw4r/lyt/textBox.h>
#include <nw4r/ut/Rect.h>
#include <revolution/dvd.h>
#include <revolution/os.h>

namespace {
#include "NebulaTextTable.inc"

    const s32 cFileMax = 0x10000;
    const s32 cTextMax = 0x8000;
    const s32 cOverrideMax = 512;
    const s32 cIdMax = 48;

    struct Override {
        char mId[cIdMax];
        s32 mText;  // into sText
    };

    alignas(32) u8 sFile[cFileMax];
    wchar_t sText[cTextMax];
    s32 sTextUsed = 0;
    Override sOverrides[cOverrideMax];
    s32 sOverrideNum = 0;
    s32 sColumn = 0;
    bool sLoaded = false;

    s32 findColumn(const char* pLanguage, s32 length) {
        for (s32 i = 0; i < cNebulaTextLanguageNum; i++) {
            if (static_cast< s32 >(strlen(cNebulaTextLanguages[i])) == length && strncmp(cNebulaTextLanguages[i], pLanguage, length) == 0) {
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
        for (u32 i = 0; i < sizeof(cPairs) / sizeof(cPairs[0]); i++) {
            for (s32 side = 0; side < 2; side++) {
                if (strcmp(pName, cPairs[i][side]) == 0) {
                    return findColumn(cPairs[i][1 - side], strlen(cPairs[i][1 - side]));
                }
            }
        }
        return -1;
    }

    // One UTF-8 line's value into sText, with \n and \\ unescaped; its start, or -1 when sText is full.
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
            if (sTextUsed + 3 >= cTextMax) {
                sTextUsed = start;
                return -1;
            }
            if (code >= 0x10000) {
                code -= 0x10000;
                sText[sTextUsed++] = static_cast< wchar_t >(0xD800 + (code >> 10));
                sText[sTextUsed++] = static_cast< wchar_t >(0xDC00 + (code & 0x3FF));
            } else {
                sText[sTextUsed++] = static_cast< wchar_t >(code);
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
            const s32 keyLength = static_cast< s32 >(pKeyEnd - pLine);
            if (keyLength <= 0 || keyLength >= cIdMax) {
                continue;
            }
            if (keyLength == 8 && strncmp(reinterpret_cast< const char* >(pLine), "language", 8) == 0) {
                const s32 column = findColumn(reinterpret_cast< const char* >(pValue), static_cast< s32 >(pLineEnd - pValue));
                if (column >= 0) {
                    sColumn = column;
                } else {
                    OSReport("NebulaText: unknown language in NebulaText.txt\n");
                }
                continue;
            }
            if (sOverrideNum >= cOverrideMax) {
                continue;
            }
            const s32 text = storeValue(pValue, pLineEnd);
            if (text < 0) {
                continue;
            }
            Override& rOverride = sOverrides[sOverrideNum++];
            memcpy(rOverride.mId, pLine, keyLength);
            rOverride.mId[keyLength] = 0;
            rOverride.mText = text;
        }
    }

    void load() {
        sLoaded = true;
        const char* pPrefix = MR::getCurrentLanguagePrefix();
        const s32 column = findColumn(pPrefix, strlen(pPrefix));
        sColumn = column >= 0 ? column : 0;
        OSReport("NebulaText: %s\n", pPrefix);

        char path[96];
        snprintf(path, sizeof(path), "/%s/MessageData/NebulaText.txt", pPrefix);
        if (DVDConvertPathToEntrynum(path) < 0) {
            return;
        }
        DVDFileInfo file;
        if (!DVDOpen(path, &file)) {
            return;
        }
        const s32 length = file.length < static_cast< u32 >(cFileMax) ? static_cast< s32 >(file.length) : cFileMax;
        const s32 read = (length + 31) & ~31;
        if (length > 0 && read <= cFileMax && DVDReadPrio(&file, sFile, read, 0, 2) >= 0) {
            parse(sFile, length);
            OSReport("NebulaText: %s (%d texts)\n", path, sOverrideNum);
        }
        DVDClose(&file);
    }

    const wchar_t* find(const char* pId) {
        for (s32 i = sOverrideNum - 1; i >= 0; i--) {
            if (strcmp(sOverrides[i].mId, pId) == 0) {
                return &sText[sOverrides[i].mText];
            }
        }
        for (u32 i = 0; i < sizeof(cNebulaTextRows) / sizeof(cNebulaTextRows[0]); i++) {
            const NebulaTextRow& rRow = cNebulaTextRows[i];
            if (strcmp(rRow.mId, pId) != 0) {
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

    // wcsncmp would read 32-bit units on Linux and Android (the game's wchar_t is 16-bit, -fshort-wchar).
    bool startsWith(const wchar_t* pText, const wchar_t* pPrefix) {
        for (; *pPrefix != 0; pText++, pPrefix++) {
            if (*pText != *pPrefix) {
                return false;
            }
        }
        return true;
    }

    s32 append(wchar_t* pDst, s32 length, s32 capacity, const wchar_t* pSrc, s32 count) {
        for (s32 i = 0; (count < 0 || i < count) && pSrc[i] != 0 && length + 1 < capacity; i++) {
            pDst[length++] = pSrc[i];
        }
        return length;
    }
};  // namespace

namespace NebulaText {
    bool has(const char* pId) {
        if (!sLoaded) {
            load();
        }
        return find(pId) != nullptr;
    }

    s32 get(wchar_t* pDst, s32 capacity, const char* pId) {
        if (capacity <= 0) {
            return 0;
        }
        if (!sLoaded) {
            load();
        }
        const wchar_t* pText = find(pId);
        s32 length = 0;
        if (pText == nullptr) {
            for (; pId[length] != 0 && length + 1 < capacity; length++) {
                pDst[length] = static_cast< wchar_t >(pId[length]);
            }
            pDst[length] = 0;
            return length;
        }

        // {galaxy:Name}: the galaxy's name in the game's text; {message:Id}: one of the game's messages (a dome's name,
        // Layout_MapGrandGalaxyOne), its tags left out. The id itself when the game has neither.
        static const wchar_t cGalaxy[] = L"{galaxy:";
        static const wchar_t cMessage[] = L"{message:";
        static const wchar_t cPlayer[] = L"{player}";
        while (*pText != 0) {
            // {player}: the game's own tag for the player's name (Mario or Luigi, as the file is played).
            if (startsWith(pText, cPlayer)) {
                // Copied whole: a tag's parameters may be zero (the game's text is measured tag by tag).
                static const wchar_t cPlayerTag[] = {0x1A, 0x0508, 0x0000, 0x0000};
                const s32 units = sizeof(cPlayerTag) / sizeof(cPlayerTag[0]);
                if (length + units < capacity) {
                    for (s32 i = 0; i < units; i++) {
                        pDst[length++] = cPlayerTag[i];
                    }
                }
                pText += sizeof(cPlayer) / sizeof(cPlayer[0]) - 1;
                continue;
            }
            const wchar_t* pClose = nullptr;
            s32 tokenLength = 0;
            if (startsWith(pText, cGalaxy)) {
                tokenLength = sizeof(cGalaxy) / sizeof(cGalaxy[0]) - 1;
            } else if (startsWith(pText, cMessage)) {
                tokenLength = sizeof(cMessage) / sizeof(cMessage[0]) - 1;
            }
            if (tokenLength > 0) {
                for (const wchar_t* p = pText + tokenLength; *p != 0; p++) {
                    if (*p == L'}') {
                        pClose = p;
                        break;
                    }
                }
            }
            if (pClose == nullptr || pClose - (pText + tokenLength) >= cIdMax) {
                length = append(pDst, length, capacity, pText, 1);
                pText++;
                continue;
            }
            char id[cIdMax];
            s32 n = 0;
            for (const wchar_t* p = pText + tokenLength; p < pClose; p++) {
                id[n++] = static_cast< char >(*p);
            }
            id[n] = 0;
            const bool galaxy = startsWith(pText, cGalaxy);
            const wchar_t* pName = galaxy ? MR::getGalaxyNameOnCurrentLanguage(id) : MR::getGameMessageDirect(id);
            if (pName != nullptr) {
                // The game's text without its tags (0x1A, then the tag's size in bytes in the next unit's low byte).
                for (const wchar_t* p = pName; *p != 0 && length + 1 < capacity;) {
                    if (*p == 0x1A && p[1] != 0) {
                        // Its whole size: a tag's parameters may be zero.
                        const s32 units = (p[1] & 0xFF) / 2;
                        p += units > 0 ? units : 1;
                        continue;
                    }
                    pDst[length++] = *p++;
                }
            } else {
                for (s32 i = 0; i < n && length + 1 < capacity; i++) {
                    pDst[length++] = static_cast< wchar_t >(id[i]);
                }
            }
            pText = pClose + 1;
        }
        pDst[length] = 0;
        return length;
    }

    // The talk getTalk gives; its lines are fitted to the balloon the first time it shows (fitTalk).
    const s32 cTalkMax = 384;
    const s32 cTalkBreaks = 32;  // room left for the breaks fitTalk puts between Japanese or Chinese characters
    wchar_t sTalk[cTalkMax];
    bool sTalkFitted = false;

    // Japanese and Chinese lines may break between characters (Korean, as English, at spaces): before a wide character
    // that is not closing punctuation or a small kana, after one that is not an opening bracket.
    bool breaksBefore(wchar_t c, wchar_t previous) {
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
    f32 measureLine(nw4r::lyt::TextBox* pBox, const wchar_t* pText, s32 count) {
        wchar_t line[cTalkMax];
        const s32 length = count < cTalkMax - 1 ? count : cTalkMax - 1;
        memcpy(line, pText, length * sizeof(wchar_t));
        line[length] = 0;
        LayoutCoreUtil::setTextBoxMessage(pBox, line);
        const nw4r::lyt::Size size = pBox->mSize;
        pBox->mSize.width = 4000.0f;
        const nw4r::ut::Rect rect = pBox->GetTextDrawRect();
        pBox->mSize = size;
        return rect.right - rect.left;
    }

    const wchar_t* getTalk(const char* pId) {
        if (strcmp(pId, "AchievementLuma") != 0) {
            return nullptr;
        }
        const s32 capacity = cTalkMax - cTalkBreaks;
        s32 length = get(sTalk, capacity - 8, "LumaGreeting");
        // The next page (the game's <1:1> tag).
        sTalk[length++] = 0x1A;
        sTalk[length++] = 0x0106;
        sTalk[length++] = 0x0001;
        get(sTalk + length, capacity - length, "LumaBoard");
        sTalkFitted = false;
        return sTalk;
    }

    // Where a paragraph may break: at a space (not one French keeps with its punctuation: before ! ? : ; », after «),
    // or before a Japanese or Chinese character. Offsets from the paragraph's start; at most `max`.
    s32 findBreaks(const wchar_t* pText, s32 length, s32* pBreaks, s32 max) {
        s32 count = 0;
        for (s32 i = 0; i < length && count < max; i++) {
            const wchar_t c = pText[i];
            if (c == 0x1A && i + 1 < length && pText[i + 1] != 0) {
                // A tag, skipped whole: its parameters are not text.
                const s32 units = (pText[i + 1] & 0xFF) / 2;
                i += (units > 0 ? units : 1) - 1;
                continue;
            }
            if (i == 0) {
                continue;
            }
            if (c == L' ') {
                const wchar_t next = i + 1 < length ? pText[i + 1] : 0;
                const bool keptBefore = next == L'!' || next == L'?' || next == L':' || next == L';' || next == 0xBB;
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
    s32 setGreedy(nw4r::lyt::TextBox* pBox, const wchar_t* pText, s32 length, const s32* pBreaks, s32 breakNum, f32 limit, s32* pChosen) {
        s32 chosen = 0;
        s32 lineStart = 0;
        s32 last = -1;  // the last break the current line fits before
        for (s32 b = 0; b <= breakNum; b++) {
            const s32 at = b < breakNum ? pBreaks[b] : length;
            if (measureLine(pBox, pText + lineStart, at - lineStart) > limit && last >= 0) {
                pChosen[chosen++] = pBreaks[last];
                lineStart = pBreaks[last] + (pText[pBreaks[last]] == L' ' ? 1 : 0);
                last = -1;
                b--;
                continue;
            }
            last = b < breakNum ? b : last;
        }
        return chosen;
    }

    bool isTalk(const wchar_t* pMessage) {
        return pMessage >= sTalk && pMessage < sTalk + cTalkMax;
    }

    void fitTalk(const wchar_t* pMessage, nw4r::lyt::TextBox* pBox) {
        if (pBox == nullptr || sTalkFitted || !isTalk(pMessage)) {
            return;
        }
        sTalkFitted = true;
        const f32 limit = pBox->mSize.width;
        // Its length: tags skipped whole (the player's name is a tag whose parameters are zero).
        s32 length = 0;
        while (sTalk[length] != 0) {
            const s32 units = sTalk[length] == 0x1A && sTalk[length + 1] != 0 ? (sTalk[length + 1] & 0xFF) / 2 : 1;
            length += units > 0 ? units : 1;
        }
        // Paragraph by paragraph (a line break or a page ends one): as few lines as the box allows, and those as even
        // as they can be (the narrowest width that still needs no more lines), as the game's writers break them.
        s32 paragraph = 0;
        while (paragraph <= length) {
            s32 paragraphEnd = paragraph;
            while (paragraphEnd < length && sTalk[paragraphEnd] != L'\n' && !(sTalk[paragraphEnd] == 0x1A && sTalk[paragraphEnd + 1] == 0x0106)) {
                if (sTalk[paragraphEnd] == 0x1A && sTalk[paragraphEnd + 1] != 0) {
                    // Its whole size: a tag's parameters may be zero.
                    const s32 units = (sTalk[paragraphEnd + 1] & 0xFF) / 2;
                    paragraphEnd += units > 0 ? units : 1;
                } else {
                    paragraphEnd++;
                }
            }
            if (paragraphEnd > length) {
                paragraphEnd = length;
            }
            wchar_t* pText = sTalk + paragraph;
            const s32 textLength = paragraphEnd - paragraph;
            // As many as the text has places: one per unit at most.
            static s32 breaks[cTalkMax];
            static s32 chosen[cTalkMax];
            static s32 trial[cTalkMax];
            const s32 breakNum = findBreaks(pText, textLength, breaks, cTalkMax);
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
                wchar_t* pBreak = pText + chosen[i];
                if (*pBreak == L' ') {
                    *pBreak = L'\n';
                } else if (length + 1 < cTalkMax) {
                    memmove(pBreak + 1, pBreak, (length + 1 - (pBreak - sTalk)) * sizeof(wchar_t));
                    *pBreak = L'\n';
                    length++;
                    added++;
                }
            }
            paragraphEnd += added;
            if (paragraphEnd >= length) {
                break;
            }
            if (sTalk[paragraphEnd] == L'\n') {
                paragraph = paragraphEnd + 1;
            } else {
                const s32 units = (sTalk[paragraphEnd + 1] & 0xFF) / 2;
                paragraph = paragraphEnd + (units > 0 ? units : 1);
            }
        }
    }
};  // namespace NebulaText
#endif
