#include "Game/System/NebulaText.hpp"

#ifdef TARGET_PC
#include "Game/System/Language.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>
#include <cstring>
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

        // {galaxy:Name}: the galaxy's name in the game's text (the internal name when it has none).
        static const wchar_t cGalaxy[] = L"{galaxy:";
        const s32 tokenLength = sizeof(cGalaxy) / sizeof(cGalaxy[0]) - 1;
        while (*pText != 0) {
            const wchar_t* pClose = nullptr;
            if (startsWith(pText, cGalaxy)) {
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
            char galaxy[cIdMax];
            s32 n = 0;
            for (const wchar_t* p = pText + tokenLength; p < pClose; p++) {
                galaxy[n++] = static_cast< char >(*p);
            }
            galaxy[n] = 0;
            const wchar_t* pName = MR::getGalaxyNameOnCurrentLanguage(galaxy);
            if (pName != nullptr) {
                length = append(pDst, length, capacity, pName, -1);
            } else {
                for (s32 i = 0; i < n && length + 1 < capacity; i++) {
                    pDst[length++] = static_cast< wchar_t >(galaxy[i]);
                }
            }
            pText = pClose + 1;
        }
        pDst[length] = 0;
        return length;
    }
};  // namespace NebulaText
#endif
