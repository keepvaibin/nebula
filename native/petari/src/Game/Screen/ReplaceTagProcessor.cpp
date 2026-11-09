#include "Game/Util/TextString.hpp"
#include "Game/Screen/ReplaceTagProcessor.hpp"
#include "Game/Screen/MessageTagSkipTagProcessor.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MessageUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstdio>

namespace ReplaceTagProcessor {
    namespace Impl {
        struct GroupFunctionInfo {
            u8 mGroup;
            u32 (*mFunction)(char16_t*, const MessageEditorMessageTag&);
        };

        const GroupFunctionInfo sGroupFunctionTable[] = {{5, exeLocalizeGroup}, {9, exeRaceTimeGroup}, {3, exePictureGroup}, {0, nullptr}};

        const GroupFunctionInfo* findGroupFunctionInfo(int group) {
            for (const GroupFunctionInfo* pInfo = sGroupFunctionTable; pInfo->mFunction != nullptr; pInfo++) {
                if (pInfo->mGroup == group) {
                    return pInfo;
                }
            }

            return nullptr;
        }
    };  // namespace Impl

    namespace ImplArgs {
        struct GroupFunctionInfo {
            u8 mGroup;
            u32 (*mFunction)(char16_t*, const MessageEditorMessageTag&, va_list, u32);
        };

        const GroupFunctionInfo sGroupFunctionTable[] = {{7, exeStringGroup}, {6, exeNumberGroup}, {0, nullptr}};

        const GroupFunctionInfo* findGroupFunctionInfo(int group) {
            for (const GroupFunctionInfo* pInfo = sGroupFunctionTable; pInfo->mFunction != nullptr; pInfo++) {
                if (pInfo->mGroup == group) {
                    return pInfo;
                }
            }

            return nullptr;
        }
    };  // namespace ImplArgs

    u32 getReplaceRoom(const char16_t* pSrc) {
        u32 room = 1;
        // A mod's galaxy that names no text for itself.
        for (; pSrc != nullptr && *pSrc != 0 && !MR::isMessageEditorNextTag(pSrc);) {
            if (*pSrc == 0x1A) {
                MessageEditorMessageTag tag(pSrc + 1);
                const u32 skip = tag.getSkipLength() + 1;
                if (tag.getGroup() == 5) {
                    room += getLocalizeGroupRoom(tag);
                } else if (tag.getGroup() == 9) {
                    room += getRaceTimeGroupRoom(tag);
                } else {
                    room += skip;
                }

                pSrc += skip;
            } else {
                room++;
                pSrc++;
            }
        }

        return room;
    }

    u32 Replace(char16_t* pDst, const char16_t* pSrc) {
        char16_t* pStart = pDst;
        // A mod's galaxy that names no text for itself.
        if (pSrc == nullptr) {
            *pDst = 0;
            return 0;
        }
        while (*pSrc != 0) {
            if (MR::isMessageEditorNextTag(pSrc)) {
                *pDst = 0;
                return pDst - pStart;
            }

            if (*pSrc == 0x1A) {
                MessageEditorMessageTag tag(pSrc + 1);
                const Impl::GroupFunctionInfo* pInfo = Impl::findGroupFunctionInfo(tag.getGroup());
                if (pInfo != nullptr) {
                    pDst += pInfo->mFunction(pDst, tag);
                } else {
                    MR::copyMemory(pDst, pSrc, (tag.getSkipLength() + 1) * sizeof(char16_t));
                    pDst += tag.getSkipLength() + 1;
                }

                pSrc += tag.getSkipLength() + 1;
            } else {
                *pDst = *pSrc;
                pDst++;
                pSrc++;
            }
        }

        *pDst = *pSrc;
        return pDst - pStart;
    }

    u32 ReplaceArgs(char16_t* pDst, const char16_t* pSrc, va_list args, u32 argCount) {
        char16_t* pStart = pDst;
        while (*pSrc != 0) {
            if (MR::isMessageEditorNextTag(pSrc)) {
                *pDst = 0;
                return pDst - pStart;
            }

            if (*pSrc == 0x1A) {
                MessageEditorMessageTag tag(pSrc + 1);
                const ImplArgs::GroupFunctionInfo* pInfo = ImplArgs::findGroupFunctionInfo(tag.getGroup());
                if (pInfo != nullptr) {
                    pDst += pInfo->mFunction(pDst, tag, args, argCount);
                } else {
                    MR::copyMemory(pDst, pSrc, (tag.getSkipLength() + 1) * sizeof(char16_t));
                    pDst += tag.getSkipLength() + 1;
                }

                pSrc += tag.getSkipLength() + 1;
            } else {
                *pDst = *pSrc;
                pDst++;
                pSrc++;
            }
        }

        *pDst = *pSrc;
        return pDst - pStart;
    }

    u32 exePictureGroup(char16_t* pDst, const MessageEditorMessageTag& rTag) {
        struct PictureTag {
            u8 mLength;
            u8 mGroup;
            u16 mTag;
        } tag;
        // A picture tag longer than the 4 bytes of the struct (an edited message) keeps only those 4.
        const u32 tagLength = rTag.getTagLength() < sizeof(PictureTag) ? rTag.getTagLength() : sizeof(PictureTag);
        tag.mLength = tagLength + 2;
        tag.mGroup = rTag.getGroup();
        tag.mTag = rTag.getTag();
        if (static_cast< char16_t >(rTag.getTag() + 0x30) == 0x5B) {
            if (MR::isPlayerLuigi()) {
                tag.mTag = 0x1C;
            } else {
                tag.mTag = 0x12;
            }
        }

        *pDst++ = 0x1A;
        MR::copyMemory(pDst, &tag, tagLength);
        return tagLength / 2 + 1;
    }

    namespace {
        // The string argument a string tag names.
        const char16_t* findStringArg(const MessageEditorMessageTag& rTag, va_list args, u32 argCount) {
            va_list copy;
            const char16_t* pString = nullptr;
            // A tag naming an argument the caller did not pass is the empty text.
            if (rTag.getParam32(1) >= argCount) {
                return nullptr;
            }
            va_copy(copy, args);
            for (u32 i = 0; i <= rTag.getParam32(1); i++) {
                pString = va_arg(copy, const char16_t*);
            }
            va_end(copy);
            return pString;
        }

        u32 stringUnits(const char16_t* pString) {
            u32 units = 0;
            if (pString != nullptr) {
                while (pString[units] != 0) {
                    units++;
                }
            }
            return units;
        }
    }  // namespace

    u32 getStringGroupRoom(const MessageEditorMessageTag& rTag, va_list args, u32 argCount) {
        return stringUnits(findStringArg(rTag, args, argCount));
    }

    // The whole argument, however long.
    u32 exeStringGroup(char16_t* pDst, const MessageEditorMessageTag& rTag, va_list args, u32 argCount) {
        const char16_t* pString = findStringArg(rTag, args, argCount);
        const u32 units = stringUnits(pString);
        for (u32 i = 0; i < units; i++) {
            pDst[i] = pString[i];
        }
        pDst[units] = 0;
        return units;
    }

    u32 exeNumberGroup(char16_t* pDst, const MessageEditorMessageTag& rTag, va_list args, u32 argCount) {
        va_list copy;
        int number = 0;
        // A tag naming an argument the caller did not pass is the empty text.
        if (rTag.getParam32(1) >= argCount) {
            *pDst = 0;
            return 0;
        }
        va_copy(copy, args);
        for (u32 i = 0; i <= rTag.getParam32(1); i++) {
            number = va_arg(copy, int);
        }
        va_end(copy);

        // The room getReplaceRoom and ReplaceTagFunction::ReplaceArgs set aside for a number tag: an s32 is at most 11
        // characters, "-2147483648", and the terminator.
        const size_t room = 12;
        switch (rTag.getTag()) {
        case 5:
            return MR::formatText(pDst, room, u"%02d", number);
        case 6:
            return MR::formatText(pDst, room, u"%03d", number);
        case 7:
            return MR::formatText(pDst, room, u"%04d", number);
        case 8:
            return MR::formatText(pDst, room, u"%05d", number);
        case 9:
            return MR::formatText(pDst, room, u"%06d", number);
        default:
            return MR::formatText(pDst, room, u"%d", number);
        }
    }

    u32 exeLocalizeGroup(char16_t* pDst, const MessageEditorMessageTag& rTag) {
        switch (rTag.getTag()) {
        case 0:
            return exeLocalizeGroupPlayerName(pDst, rTag.getParam8(0));
        }

        return 0;
    }

    namespace {
        const char16_t* findPlayerName(u8 form) {
            MR::FormattedName name(MR::isPlayerLuigi() ? "System_PlayerName1%02d" : "System_PlayerName0%02d", form);
            return MR::getGameMessageDirect(name.c_str());
        }
    }  // namespace

    u32 getLocalizeGroupRoom(const MessageEditorMessageTag& rTag) {
        switch (rTag.getTag()) {
        case 0:
            return static_cast< u32 >(MR::getStringLengthWithMessageTag(findPlayerName(rTag.getParam8(0)))) + 1;
        }

        return 1;
    }

    u32 exeLocalizeGroupPlayerName(char16_t* pDst, u8 form) {
        const char16_t* pMessage = findPlayerName(form);
        int length = MR::getStringLengthWithMessageTag(pMessage);
        MR::copyString(pDst, pMessage, length + 1);
        return length;
    }

    namespace {
        // Tag 5 is the race being run, any other the best time of the race it numbers.
        const char16_t* makeRaceTime(MR::Text16& rText, const MessageEditorMessageTag& rTag) {
            return rTag.getTag() == 5 ? MR::makeRaceCurrentTimeString(rText) : MR::makeRaceBestTimeString(rText, rTag.getTag());
        }
    }  // namespace

    u32 getRaceTimeGroupRoom(const MessageEditorMessageTag& rTag) {
        MR::Text16 text;
        return stringUnits(makeRaceTime(text, rTag));
    }

    // The whole time, however the language writes it.
    u32 exeRaceTimeGroup(char16_t* pDst, const MessageEditorMessageTag& rTag) {
        MR::Text16 text;
        const char16_t* pTime = makeRaceTime(text, rTag);
        const u32 units = stringUnits(pTime);
        for (u32 i = 0; i < units; i++) {
            pDst[i] = pTime[i];
        }
        pDst[units] = 0;
        return units;
    }
};  // namespace ReplaceTagProcessor

u32 ReplaceTagFunction::ReplaceArgs(char16_t* pDst, s32 length, const char16_t* pFormat, u32 argCount, ...) {
    // No format (a message id the text lacks) is the empty text.
    if (pFormat == nullptr) {
        pFormat = u"";
    }

    va_list args;
    va_start(args, argCount);
    // Room for the replaced text: a number tag gives at most 12 units, a string tag its argument's length, any other
    // tag keeps its length.
    u32 room = 1;
    for (const char16_t* pText = pFormat; *pText != 0 && !MR::isMessageEditorNextTag(pText);) {
        if (*pText == 0x1A) {
            MessageEditorMessageTag tag(pText + 1);
            const u32 skip = tag.getSkipLength() + 1;
            if (tag.getGroup() == 7) {
                room += ReplaceTagProcessor::getStringGroupRoom(tag, args, argCount);
            } else {
                room += tag.getGroup() == 6 ? 12 : skip;
            }
            pText += skip;
        } else {
            room++;
            pText++;
        }
    }

    char16_t stack[0x200];
    MR::Text16 heap;
    char16_t* const buffer = room <= 0x200 ? stack : heap.room(room);
    const u32 result = ReplaceTagProcessor::ReplaceArgs(buffer, pFormat, args, argCount);
    va_end(args);
    if (pDst != nullptr && length > 0) {
        if (result >= static_cast< u32 >(length)) {
            buffer[length - 1] = 0;
        }
        MR::copyString(pDst, buffer, length);
    }
    return result;
}
