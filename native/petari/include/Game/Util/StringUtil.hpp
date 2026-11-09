#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>
#include <cstdarg>

namespace MR {
    class FormattedName;

    // pParent/pChild.
    void addFilePrefix(FormattedName& rDst, const char* pChild, const char* pParent);

    class Text16;

    // The date or time in the played language, whole, into rDst; returns its text.
    const char16_t* makeDateString(Text16& rDst, s32 year, s32 month, s32 day);
    const char16_t* makeTimeString(Text16& rDst, s32 hour, s32 minute);
    // Minutes, seconds and centiseconds of a step count, whole, into rDst; each returns its text.
    const char16_t* makeClearTimeString(Text16& rDst, u32 frame);
    const char16_t* makeMinuteAndSecondString(Text16& rDst, u32 frame);
    const char16_t* makeRaceBestTimeString(Text16& rDst, int raceId);
    const char16_t* makeRaceCurrentTimeString(Text16& rDst);

    char16_t* addNumberFontTag(char16_t* pDst, int tag);
    char16_t* addNumberFontTag(char16_t* pDst, const char16_t* pFmt, ...);
    char16_t* addPictureFontCode(char16_t* pDst, int code);
    char16_t* addPictureFontTag(char16_t* pDst, int tag);
    // Returns the new terminator.
    char16_t* addNewLine(char16_t* pDst);

    void removeExtensionString(FormattedName& rDst, const char* pPath);
    const char* getBasename(const char* pPath);

    // At most num - 1 characters, always terminated.
    void copyString(char* pDst, const char* pSrc, u32 num);
    void copyString(char16_t* pDst, const char16_t* pSrc, u32 num);
    // num counts the terminator; returns the characters written without it.
    int convertUTF16ToASCII(char* pDst, const char16_t* pSrc, s32 num);

    int strcasecmp(const char* pStr1, const char* pStr2);
    // Byte order: below zero when pStr1 comes first, zero when equal.
    int compareString(const char* pStr1, const char* pStr2);
    bool isEqualString(const char* pStr1, const char* pStr2);
    // Whether pStr starts with all of pPrefix.
    bool isEqualStringPrefix(const char* pStr, const char* pPrefix);
    bool isEqualStringCase(const char* pStr1, const char* pStr2);
    // Whether pStr contains pSubStr.
    bool isEqualSubString(const char* pStr, const char* pSubStr);
    // Whether pStr starts with pHead.
    bool isHeadString(const char* pStr, const char* pHead);
    bool hasStringSpace(const char* pStr);
    // Whether the last digitNum characters are digits.
    bool isDigitStringTail(const char* pStr, int digitNum);
    bool isExistString(const char* pStr, const char* const* ppStr, u32 strNum);
    bool isNullOrEmptyString(const char* pStr);

    bool isMessageEditorNextTag(const char16_t*);
    int getStringLengthWithMessageTag(const char16_t*);

    // When pSrc contains pSubStr, the number at the start of pSrc; pDst is left as it is otherwise.
    void scan32(const char* pSrc, const char* pSubStr, s32* pDst);
    void scan16(const char* pSrc, const char* pSubStr, u16* pDst);
    void scan8(const char* pSrc, const char* pSubStr, u8* pDst);
    void scans16x4(const char* pSrc, const char* pSubStr, s16* pDst);

    // One copy of each distinct name for the process's life (in the root arena), for names held across scenes.
    const char* internName(const char* pName);

    // A string formatted to any length: up to 255 bytes in itself, longer in the current arena, freed with it.
    class FormattedName {
    public:
        FormattedName() : mText(mInline) {
            mInline[0] = '\0';
        }
        __attribute__((format(printf, 2, 3))) explicit FormattedName(const char* pFormat, ...);
        ~FormattedName();

        // Formats again; the arguments must not read this name.
        __attribute__((format(printf, 2, 3))) void format(const char* pFormat, ...);
        __attribute__((format(printf, 2, 0))) void vformat(const char* pFormat, va_list args);
        // Writable storage of `size` bytes for a caller that fills the text itself; the previous text is gone.
        char* room(u32 size);

        FormattedName(const FormattedName&) = delete;
        FormattedName& operator=(const FormattedName&) = delete;

        const char* c_str() const {
            return mText;
        }

    private:
        char* mText;
        char mInline[256];
    };

    // UTF-16 text with no length limit: 128 code units in itself, past that the heap.
    class Text16 {
    public:
        Text16() : mText(mInline), mRoom(cInline) {
            mInline[0] = u'\0';
        }
        ~Text16();

        Text16(const Text16&) = delete;
        Text16& operator=(const Text16&) = delete;

        // Writable storage of `units` code units, the terminator included; the previous text is gone.
        char16_t* room(u32 units);
        // Room for `units` code units in all, the first `used` kept.
        char16_t* grow(u32 used, u32 units);
        // The formatted text (MR::formatTextV's conversions), whole: the room grows until it fits.
        const char16_t* format(const char16_t* pFormat, va_list args);

        const char16_t* c_str() const {
            return mText;
        }

        char16_t* data() {
            return mText;
        }

        u32 capacity() const {
            return mRoom;
        }

    private:
        static const u32 cInline = 128;
        char16_t* mText;
        u32 mRoom;
        char16_t mInline[cInline];
    };
};  // namespace MR
