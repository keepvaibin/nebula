#pragma once

#include <cstdarg>

class MessageEditorMessageTag;

namespace ReplaceTagProcessor {
    u32 Replace(char16_t* pDst, const char16_t* pSrc);
    u32 ReplaceArgs(char16_t* pDst, const char16_t* pSrc, va_list args);
    u32 exePictureGroup(char16_t* pDst, const MessageEditorMessageTag& rTag);
    u32 exeStringGroup(char16_t* pDst, const MessageEditorMessageTag& rTag, va_list args);
    u32 exeNumberGroup(char16_t* pDst, const MessageEditorMessageTag& rTag, va_list args);
    u32 exeLocalizeGroup(char16_t* pDst, const MessageEditorMessageTag& rTag);
    u32 exeLocalizeGroupPlayerName(char16_t* pDst, u8 form);
    u32 exeRaceTimeGroup(char16_t* pDst, const MessageEditorMessageTag& rTag);
};  // namespace ReplaceTagProcessor

namespace ReplaceTagFunction {
    u32 ReplaceArgs(char16_t* pDst, s32 length, const char16_t* pFormat, ...);
};  // namespace ReplaceTagFunction
