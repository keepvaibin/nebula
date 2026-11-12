#include "Game/Util/TextString.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Screen/ReplaceTagProcessor.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/MessageTag.hpp"
#include "Game/System/NebulaArena.hpp"
#include <cctype>
#include <cstddef>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <nebula_format.h>
#include "Game/Util/Tunable.hpp"

namespace {
    // Clear times count game frames.
    NEBULA_TUNABLE(s32, cCentisecPerSec, 100);
    const s32 cSecPerMin = 60;
    const s32 cMinPerHour = 60;
    const s32 cFramePerSec = 60;
    const s32 cFramePerMin = cFramePerSec * cSecPerMin;
    const s32 cFramePerHour = cFramePerMin * cMinPerHour;
}  // namespace

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
    // A message tag laid over the UTF-16 text: the escape unit, the tag's size in bytes, its group, then its data.
    struct Tag {
        u16 mEscape;
        u8 mDataSize;
        u8 mGroup;
        char16_t mBuffer[1];
    };

    struct TagHeader {
        u8 mDataSize;
        u8 mGroup;
        u16 mTag;
    };

    void addFilePrefix(FormattedName& rDst, const char* pChild, const char* pParent) {
        // A null name is the empty name.
        if (pChild == nullptr) {
            pChild = "";
        }
        if (pParent == nullptr) {
            pParent = "";
        }

        if (*pChild == '/') {
            pChild++;
        }

        rDst.format("%s/%s", pParent, pChild);
    }

    namespace {
        // The message with its number tags replaced, whole: ReplaceArgs gives the whole length even when it cut the
        // text, so a text past the room is made again in room for it.
        template < class... Numbers >
        const char16_t* replaceWhole(Text16& rDst, const char* pMessageId, Numbers... numbers) {
            const char16_t* pMessage = MR::getGameMessageDirect(pMessageId);
            const u32 units = ReplaceTagFunction::ReplaceArgs(rDst.data(), static_cast< s32 >(rDst.capacity()), pMessage, sizeof...(Numbers), numbers...);
            if (units >= rDst.capacity()) {
                ReplaceTagFunction::ReplaceArgs(rDst.room(units + 1), static_cast< s32 >(units + 1), pMessage, sizeof...(Numbers), numbers...);
            }
            return rDst.c_str();
        }
    }  // namespace

    const char16_t* makeDateString(Text16& rDst, s32 year, s32 month, s32 day) {
        return replaceWhole(rDst, "System_Date000", year, month, day);
    }

    const char16_t* makeTimeString(Text16& rDst, s32 hour, s32 minute) {
        return replaceWhole(rDst, "System_Time002", hour, minute);
    }

    const char16_t* makeClearTimeString(Text16& rDst, u32 frame) {
        s32 minute;
        s32 second;
        s32 centisecond;

        if (frame >= ::cFramePerHour) {
            minute = 59;
            second = 59;
            centisecond = 99;
        } else {
            const s32 signedFrame = frame;

            minute = signedFrame / ::cFramePerMin;
            second = (signedFrame / ::cFramePerSec) % ::cSecPerMin;
            centisecond = ((signedFrame % ::cFramePerSec) * ::cCentisecPerSec) / ::cFramePerSec;
        }

        return replaceWhole(rDst, "System_Time000", 0, minute, second, centisecond);
    }

    const char16_t* makeMinuteAndSecondString(Text16& rDst, u32 frame) {
        s32 minute;
        s32 second;

        if (frame >= static_cast< u32 >(::cFramePerHour)) {
            minute = 59;
            second = 59;
        } else {
            minute = frame / static_cast< u32 >(::cFramePerMin);
            second = (frame % static_cast< u32 >(::cFramePerMin)) / static_cast< u32 >(::cFramePerSec);
        }

        return replaceWhole(rDst, "System_Time001", 0, minute, second);
    }

    char16_t* addNumberFontTag(char16_t* pDst, int tag) {
        return MR::addNumberFontTag(pDst, u"%d", tag);
    }

    void removeExtensionString(FormattedName& rDst, const char* pPath) {
        // A null path is the empty name.
        if (pPath == nullptr) {
            pPath = "";
        }

        const char* const pExtSeparator = strrchr(pPath, '.');
        const char* const pDirSeparator = strrchr(pPath, '/');

        // No dot at all, or a dot before the last slash or right after it: the whole path is kept.
        if (pExtSeparator == nullptr || pExtSeparator < pDirSeparator || (pDirSeparator != nullptr && pDirSeparator + 1 == pExtSeparator)) {
            rDst.format("%s", pPath);
            return;
        }

        rDst.format("%.*s", static_cast< int >(pExtSeparator - pPath), pPath);
    }

    const char16_t* makeRaceBestTimeString(Text16& rDst, int raceId) {
        return MR::makeClearTimeString(rDst, getRaceBestTime(raceId));
    }

    const char16_t* makeRaceCurrentTimeString(Text16& rDst) {
        return MR::makeClearTimeString(rDst, getRaceCurrentTime());
    }

    // num is the buffer size: the copy is cut to fit and always terminated.
    void copyString(char* pDst, const char* pSrc, u32 num) {
        if (num == 0) {
            return;
        }

        // A null source copies as the empty name.
        if (pSrc == nullptr) {
            pSrc = "";
        }

        strncpy(pDst, pSrc, num - 1);
        pDst[num - 1] = '\0';
    }

    void copyString(char16_t* pDst, const char16_t* pSrc, u32 num) {
        // A null source (a message the text lacks) copies as the empty text.
        MR::copyText(pDst, pSrc != nullptr ? pSrc : u"", num);
    }

    char16_t* addPictureFontCode(char16_t* pDst, int code) {
        pDst[0] = code;
        pDst[1] = '\0';

        return &pDst[1];
    }

    char16_t* addPictureFontTag(char16_t* pDst, int tag) {
        Tag* pTag = reinterpret_cast< Tag* >(pDst);

        pTag->mEscape = cMessageTagEscape;
        pTag->mDataSize = sizeof(Tag);
        pTag->mGroup = MessageTagGroup_Picture;
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
        *pDst++ = cMessageTagEscape;
        TagHeader* pTag = reinterpret_cast< TagHeader* >(pDst);
        pDst += sizeof(TagHeader) / sizeof(char16_t);

        va_list args;
        va_start(args, pFmt);

        int num = MR::formatTextV(pDst, 256, pFmt, args);
        va_end(args);

        pDst += num;
        pTag->mGroup = MessageTagGroup_Font;
        pTag->mTag = 0;
        pTag->mDataSize = num * sizeof(char16_t) + sizeof(TagHeader) + sizeof(char16_t);
        *pDst = '\0';

        return pDst;
    }

    const char* getBasename(const char* pPath) {
        // A null path has the empty name.
        if (pPath == nullptr) {
            return "";
        }

        const char* pBasename = strrchr(pPath, '/');

        if (pBasename == nullptr) {
            pBasename = pPath;
        } else {
            pBasename++;
        }

        return pBasename;
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
        if (pStr1 == nullptr || pStr2 == nullptr) {
            return pStr1 == pStr2;
        }
        for (; *pStr1 == *pStr2; pStr1++, pStr2++) {
            if (*pStr1 == '\0') {
                return true;
            }
        }
        return false;
    }

    bool isEqualStringPrefix(const char* pStr, const char* pPrefix) {
        // A null name has no prefix; a null prefix is none.
        if (pStr == nullptr || pPrefix == nullptr) {
            return pPrefix != nullptr && *pPrefix == '\0';
        }
        for (; *pPrefix != '\0'; pStr++, pPrefix++) {
            if (*pStr != *pPrefix) {
                return false;
            }
        }
        return true;
    }

    bool isEqualStringCase(const char* pStr1, const char* pStr2) {
        // A null name equals only a null name, as in isEqualString.
        if (pStr1 == nullptr || pStr2 == nullptr) {
            return pStr1 == pStr2;
        }
        return ::nebula_stricmp(pStr1, pStr2) == 0;
    }

    bool isEqualSubString(const char* pStr, const char* pSubStr) {
        if (pStr == nullptr || pSubStr == nullptr) {
            return false;
        }
        return strstr(pStr, pSubStr) != nullptr;
    }

    bool isHeadString(const char* pStr, const char* pHead) {
        // Null names as in isEqualStringPrefix.
        if (pStr == nullptr || pHead == nullptr) {
            return pHead != nullptr && *pHead == '\0';
        }
        for (; *pHead != '\0'; pStr++, pHead++) {
            if (*pStr != *pHead) {
                return false;
            }
        }
        return true;
    }

    bool hasStringSpace(const char* pStr) {
        return pStr != nullptr && strchr(pStr, ' ') != nullptr;
    }

    bool isDigitStringTail(const char* pStr, int digitNum) {
        if (pStr == nullptr) {
            return false;
        }
        const size_t length = strlen(pStr);
        if (digitNum > 0 && length < static_cast< size_t >(digitNum)) {
            return false;
        }

        for (int i = 1; i <= digitNum; i++) {
            if (isdigit(static_cast< unsigned char >(pStr[length - i])) == 0) {
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

        return pTag->mGroup == MessageTagGroup_Display && pTag->mBuffer[0] == cMessageTagNextPage;
    }

    int getStringLengthWithMessageTag(const char16_t* pMessage) {
        int length = 0;

        // No message (an id the text lacks) is empty.
        if (pMessage == nullptr) {
            return 0;
        }

        while (*pMessage != '\0') {
            if (*pMessage == cMessageTagEscape) {
                if (isMessageEditorNextTag(pMessage)) {
                    break;
                }

                // A tag of size 0 or 1 unit still moves on one unit, so a bad message cannot loop forever.
                u16 dataSize = reinterpret_cast< const Tag* >(pMessage)->mDataSize;
                if (dataSize < sizeof(u16)) {
                    dataSize = static_cast< u16 >(sizeof(u16));
                }

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
        int value;

        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        const char* p = pSrc;
        if (!scanInt(p, &value)) {
            return;
        }

        *pDst = value;
    }

    void scan8(const char* pSrc, const char* pSubStr, u8* pDst) {
        int value;

        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        const char* p = pSrc;
        if (!scanInt(p, &value)) {
            return;
        }

        *pDst = value;
    }

    void scans16x4(const char* pSrc, const char* pSubStr, s16* pDst) {
        if (strstr(pSrc, pSubStr) == nullptr) {
            return;
        }

        // Values the text does not give keep what the destination held.
        int values[4] = {pDst[0], pDst[1], pDst[2], pDst[3]};
        const char* p = pSrc;
        if (!scanOpen(p)) {
            return;
        }
        // Only the values read are written, as scani leaves pDst on a failed read.
        s32 count = 0;
        while (count < 4 && scanInt(p, &values[count])) {
            count++;
            if (*p != ',') {
                break;
            }
            p++;
        }

        for (s32 i = 0; i < count; i++) {
            pDst[i] = values[i];
        }
    }

    namespace {
        struct InternedName {
            InternedName* mNext;
            u32 mHash;
            char mText[1];
        };

        InternedName* sInterned = nullptr;
    }  // namespace

    const char* internName(const char* pName) {
        // Null is the empty name (no stage yet at boot).
        if (pName == nullptr) {
            pName = "";
        }
        u32 hash = 2166136261u;
        u32 length = 0;
        for (; pName[length] != '\0'; length++) {
            hash = (hash ^ static_cast< u8 >(pName[length])) * 16777619u;
        }
        for (InternedName* p = sInterned; p != nullptr; p = p->mNext) {
            if (p->mHash == hash && isEqualString(p->mText, pName)) {
                return p->mText;
            }
        }
        InternedName* const pNew = static_cast< InternedName* >(
            NebulaArena::alloc(static_cast< u32 >(offsetof(InternedName, mText)) + length + 1, 4, NebulaArena::root()));
        pNew->mNext = sInterned;
        pNew->mHash = hash;
        memcpy(pNew->mText, pName, length + 1);
        sInterned = pNew;
        return pNew->mText;
    }

    FormattedName::FormattedName(const char* pFormat, ...) : mText(mInline) {
        va_list args;
        va_start(args, pFormat);
        vformat(pFormat, args);
        va_end(args);
    }

    void FormattedName::format(const char* pFormat, ...) {
        va_list args;
        va_start(args, pFormat);
        vformat(pFormat, args);
        va_end(args);
    }

    namespace {
        // A text's buffer past its inline room: from the C heap, not the current arena. Text is made on any thread (the
        // mod log words host sentences where they are added, H312-1) and outlives no scene, so it belongs to no arena.
        template < typename T >
        T* textBlock(u32 count) {
            T* const pBlock = static_cast< T* >(::malloc(count * sizeof(T)));
            if (pBlock == nullptr) {
                NEBULA_PANIC("no memory for a text of %u units", count);
            }
            return pBlock;
        }
    }  // namespace

    void FormattedName::vformat(const char* pFormat, va_list args) {
        char* const pOld = mText != mInline ? mText : nullptr;
        va_list again;
        va_copy(again, args);
        mText = mInline;
        const int length = nebula_vsnprintf(mInline, sizeof(mInline), pFormat, args);
        if (length >= static_cast< int >(sizeof(mInline))) {
            mText = textBlock< char >(static_cast< u32 >(length) + 1);
            nebula_vsnprintf(mText, length + 1, pFormat, again);
        }
        va_end(again);
        ::free(pOld);
    }

    char* FormattedName::room(u32 size) {
        if (mText != mInline) {
            ::free(mText);
        }

        mText = size <= sizeof(mInline) ? mInline : textBlock< char >(size);
        mText[0] = '\0';
        return mText;
    }

    FormattedName::~FormattedName() {
        if (mText != mInline) {
            ::free(mText);
        }
    }

    char16_t* Text16::room(u32 units) {
        if (units > mRoom) {
            if (mText != mInline) {
                ::free(mText);
            }
            mText = textBlock< char16_t >(units);
            mRoom = units;
        }
        mText[0] = u'\0';
        return mText;
    }

    char16_t* Text16::grow(u32 used, u32 units) {
        if (units > mRoom) {
            u32 room = mRoom * 2;
            if (room < units) {
                room = units;
            }
            char16_t* const pNew = textBlock< char16_t >(room);
            for (u32 i = 0; i < used && i < mRoom; i++) {
                pNew[i] = mText[i];
            }
            if (mText != mInline) {
                ::free(mText);
            }
            mText = pNew;
            mRoom = room;
        }
        return mText;
    }

    const char16_t* Text16::format(const char16_t* pFormat, va_list args) {
        for (u32 units = mRoom;; units *= 2) {
            va_list copy;
            va_copy(copy, args);
            const int count = MR::formatTextV(room(units), units, pFormat, copy);
            va_end(copy);
            if (count >= 0) {
                return mText;
            }
        }
    }

    Text16::~Text16() {
        if (mText != mInline) {
            ::free(mText);
        }
    }
};  // namespace MR
