#include "Game/Util/TextString.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Screen/ReplaceTagProcessor.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include <cctype>
#include <cstdarg>
#include <cstdio>

#define CENTISEC_PER_SEC 100
#define SEC_PER_MIN 60
#define MIN_PER_HOUR 60
#define FRAME_PER_SEC 60
#define FRAME_PER_MIN (FRAME_PER_SEC * SEC_PER_MIN)
#define FRAME_PER_HOUR (FRAME_PER_SEC * SEC_PER_MIN * MIN_PER_HOUR)

extern "C" {
};

namespace {
    // A decimal number at p after any spaces; false if there is none.
    bool scanInt(const char*& rP, int* pValue) {
        char* pEnd;
        const long value = strtol(rP, &pEnd, 10);
        if (pEnd == rP) {
            return false;
        }
        rP = pEnd;
        *pValue = static_cast< int >(value);
        return true;
    }

    bool scanFloat(const char*& rP, f32* pValue) {
        const char* p = rP;
        while (*p == ' ' || (*p >= '\t' && *p <= '\r')) {
            p++;
        }
        const bool negative = *p == '-';
        if (*p == '-' || *p == '+') {
            p++;
        }
        double value = 0.0;
        double scale = 1.0;
        bool any = false;
        for (; *p >= '0' && *p <= '9'; p++) {
            value = value * 10.0 + (*p - '0');
            any = true;
        }
        if (*p == '.') {
            for (p++; *p >= '0' && *p <= '9'; p++) {
                value = value * 10.0 + (*p - '0');
                scale *= 10.0;
                any = true;
            }
        }
        if (!any) {
            return false;
        }
        if (*p == 'e' || *p == 'E') {
            const char* pExponent = p + 1;
            int exponent = 0;
            if (scanInt(pExponent, &exponent)) {
                p = pExponent;
                for (; exponent > 0; exponent--) {
                    value *= 10.0;
                }
                for (; exponent < 0; exponent++) {
                    scale *= 10.0;
                }
            }
        }
        rP = p;
        *pValue = static_cast< f32 >((negative ? -value : value) / scale);
        return true;
    }

    // The list's opening brace after any spaces.
    bool scanOpen(const char*& rP) {
        while (*rP == ' ' || (*rP >= '\t' && *rP <= '\r')) {
            rP++;
        }
        if (*rP != '{') {
            return false;
        }
        rP++;
        return true;
    }
}  // namespace

namespace MR {
    struct Tag {
        u16 _0;
        u8 mDataSize;
        u8 _3;
        char16_t mBuffer[1];
    };

    struct TagHeader {
        u8 mDataSize;
        u8 mGroup;
        u16 mTag;
    };

    void addFilePrefix(char* pDst, u32 size, const char* pChild, const char* pParent) {
        if (*pChild == '/') {
            pChild++;
        }

        nebula_snprintf(pDst, size, "%s/%s", pParent, pChild);
    }

    void makeDateString(char16_t* pDst, s32 size, s32 year, s32 month, s32 day) {
        const char16_t* pMessage = MR::getGameMessageDirect("System_Date000");

        ReplaceTagFunction::ReplaceArgs(pDst, size, pMessage, year, month, day);
    }

    void makeTimeString(char16_t* pDst, s32 size, s32 hour, s32 minute) {
        const char16_t* pMessage = MR::getGameMessageDirect("System_Time002");

        ReplaceTagFunction::ReplaceArgs(pDst, size, pMessage, hour, minute);
    }

    void makeClearTimeString(char16_t* pDst, u32 frame) {
        s32 minute;
        s32 second;
        s32 centisecond;

        if (frame >= FRAME_PER_HOUR) {
            minute = 59;
            second = 59;
            centisecond = 99;
        } else {
            s32 signedFrame = frame;

            minute = signedFrame / FRAME_PER_MIN;
            second = (signedFrame / FRAME_PER_SEC) % SEC_PER_MIN;
            centisecond = ((signedFrame % FRAME_PER_SEC) * CENTISEC_PER_SEC) / FRAME_PER_SEC;
        }

        const char16_t* pMessage = MR::getGameMessageDirect("System_Time000");

        ReplaceTagFunction::ReplaceArgs(pDst, 9, pMessage, 0, minute, second, centisecond);
    }

    void makeMinuteAndSecondString(char16_t* pDst, u32 frame) {
        s32 minute;
        s32 second;

        if (frame >= FRAME_PER_HOUR) {
            minute = 59;
            second = 59;
        } else {
            minute = frame / FRAME_PER_MIN;
            second = (frame % FRAME_PER_MIN) / FRAME_PER_SEC;
        }

        const char16_t* pMessage = MR::getGameMessageDirect("System_Time001");

        ReplaceTagFunction::ReplaceArgs(pDst, 16, pMessage, 0, minute, second);
    }

    char16_t* addNumberFontTag(char16_t* pDst, int tag) {
        return MR::addNumberFontTag(pDst, u"%d", tag);
    }

    char* removeExtensionString(char* pDst, u32 size, const char* pPath) {
        nebula_snprintf(pDst, size, "%s", pPath);

        char* pExtSeparator = strrchr(pDst, '.');
        char* pDirSeparator = strrchr(pDst, '/');

        if (pExtSeparator < pDirSeparator || (pDirSeparator != nullptr && pDirSeparator + 1 == pExtSeparator)) {
            return pDirSeparator;
        }

        *pExtSeparator = '\0';

        return pDirSeparator;
    }

    void makeRaceBestTimeString(char16_t* pDst, int raceId) {
        MR::makeClearTimeString(pDst, getRaceBestTime(raceId));
    }

    void makeRaceCurrentTimeString(char16_t* pDst) {
        MR::makeClearTimeString(pDst, getRaceCurrentTime());
    }

    void copyString(char* pDst, const char* pSrc, u32 num) {
        strncpy(pDst, pSrc, num);
    }

    void copyString(char16_t* pDst, const char16_t* pSrc, u32 num) {
        MR::copyText(pDst, pSrc, num);
    }

    char16_t* addPictureFontCode(char16_t* pDst, int code) {
        pDst[0] = code;
        pDst[1] = '\0';

        return &pDst[1];
    }

    char16_t* addPictureFontTag(char16_t* pDst, int tag) {
        Tag* pTag = reinterpret_cast< Tag* >(pDst);

        pTag->_0 = 26;
        pTag->mDataSize = sizeof(Tag);
        pTag->_3 = 3;
        pTag->mBuffer[0] = tag - '0';
        pTag->mBuffer[1] = '\0';

        return &pTag->mBuffer[1];
    }

    char16_t* addNewLine(char16_t* pDst) {
        pDst[0] = '\n';
        pDst[1] = '\0';

        return &pDst[1];
    }

    char16_t* addNumberFontTag(char16_t* pDst, const char16_t* pFmt, ...) {
        *pDst++ = 0x1A;
        TagHeader* pTag = reinterpret_cast< TagHeader* >(pDst);
        pDst += sizeof(TagHeader) / sizeof(char16_t);

        va_list args;
        va_start(args, pFmt);

        int num = MR::formatTextV(pDst, 256, pFmt, args);
        va_end(args);

        pDst += num;
        pTag->mGroup = 10;
        pTag->mTag = 0;
        pTag->mDataSize = num * sizeof(char16_t) + sizeof(TagHeader) + sizeof(char16_t);
        *pDst = '\0';

        return pDst;
    }

    const char* getBasename(const char* pPath) {
        const char* pBasename = strrchr(pPath, '/');

        if (pBasename == nullptr) {
            pBasename = pPath;
        } else {
            pBasename++;
        }

        return pBasename;
    }

    void extractString(char* pDst, const char* pSrc, u32 num, u32) {
        strncpy(pDst, pSrc, num);

        pDst[num] = '\0';
    }

    int convertUTF16ToASCII(char* pDst, const char16_t* pSrc, s32 num) {
        u32 i;

        for (i = 0; i < num - 1; pDst++, pSrc++, i++) {
            if (*pSrc == 0 || *pSrc > 0xFF) {
                break;
            }

            *pDst = static_cast< char >(*pSrc);
        }

        *pDst = '\0';

        return i;
    }

    int strcasecmp(const char* pStr1, const char* pStr2) {
        return ::nebula_stricmp(pStr1, pStr2);
    }

    bool isEqualString(const char* pStr1, const char* pStr2) {
        return strcmp(pStr1, pStr2) == 0;
    }

    bool isEqualStringCase(const char* pStr1, const char* pStr2) {
        return ::nebula_stricmp(pStr1, pStr2) == 0;
    }

    bool isEqualSubString(const char* pStr, const char* pSubStr) {
        return strstr(pStr, pSubStr) != nullptr;
    }

    bool hasStringSpace(const char* pStr) {
        return strchr(pStr, ' ') != nullptr;
    }

    bool isDigitStringTail(const char* pStr, int digitNum) {
        for (int i = 1; i <= digitNum; i++) {
            int ch = static_cast< unsigned char >(pStr[strlen(pStr) - i]);

            if (isdigit(ch) == 0) {
                return false;
            }
        }

        return true;
    }

    bool isExistString(const char* pStr, const char* const* ppStr, u32 strNum) {
        for (int i = 0; i < strNum; i++) {
            if (MR::isEqualString(ppStr[i], pStr)) {
                return true;
            }
        }

        return false;
    }

    bool isNullOrEmptyString(const char* pStr) {
        return pStr == nullptr || MR::isEqualString(pStr, "");
    }

    bool isMessageEditorNextTag(const char16_t* pStr) {
        const Tag* pTag = reinterpret_cast< const Tag* >(pStr);
        u8 v1 = pTag->_3;
        char16_t v2 = pTag->mBuffer[0];

        return v1 == 1 && v2 == 1;
    }

    int getStringLengthWithMessageTag(const char16_t* pMessage) {
        int length = 0;

        while (*pMessage != '\0') {
            if (*pMessage == 0x1A) {
                if (isMessageEditorNextTag(pMessage)) {
                    break;
                }

                u16 dataSize = reinterpret_cast< const Tag* >(pMessage)->mDataSize;

                pMessage += dataSize / sizeof(u16);
                length += dataSize / sizeof(u16);
                pMessage--;
                length--;
            }

            pMessage++;
            length++;
        }

        return length;
    }

    void scan32(const char* pSrc, const char* pSubStr, s32* pDst) {
        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        const char* p = pSrc;
        int value;
        if (scanInt(p, &value)) {
            *pDst = value;
        }
    }

    void scan16(const char* pSrc, const char* pSubStr, u16* pDst) {
        int temp;

        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        const char* p = pSrc;
        if (!scanInt(p, &temp)) {
            return;
        }

        *pDst = temp;
    }

    void scan8(const char* pSrc, const char* pSubStr, u8* pDst) {
        int temp;

        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        const char* p = pSrc;
        if (!scanInt(p, &temp)) {
            return;
        }

        *pDst = temp;
    }

    void scanf32(const char* pSrc, const char* pSubStr, f32* pDst) {
        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        const char* p = pSrc;
        scanFloat(p, pDst);
    }

    void scanu8x4(const char* pSrc, const char* pSubStr, u8* pDst) {
        int temp[4];

        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        const char* p = pSrc;
        if (!scanOpen(p)) {
            return;
        }
        for (s32 i = 0; i < 4 && scanInt(p, &temp[i]); i++) {
            if (*p != ',') {
                break;
            }
            p++;
        }

        for (int i = 0; i < ARRAY_SIZE(temp); i++) {
            pDst[i] = temp[i];
        }
    }

    void scans16x4(const char* pSrc, const char* pSubStr, s16* pDst) {
        int temp[4];

        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        const char* p = pSrc;
        if (!scanOpen(p)) {
            return;
        }
        for (s32 i = 0; i < 4 && scanInt(p, &temp[i]); i++) {
            if (*p != ',') {
                break;
            }
            p++;
        }

        for (int i = 0; i < ARRAY_SIZE(temp); i++) {
            pDst[i] = temp[i];
        }
    }

    void scanf32x4(const char* pSrc, const char* pSubStr, f32* pDst) {
        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        const char* p = pSrc;
        if (!scanOpen(p)) {
            return;
        }
        for (s32 i = 0; i < 4 && scanFloat(p, &pDst[i]); i++) {
            if (*p == 'f') {
                p++;
            }
            if (*p != ',') {
                break;
            }
            p++;
        }
    }
};  // namespace MR
