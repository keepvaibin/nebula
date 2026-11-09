#pragma once

#include <cstdarg>

class MessageEditorMessageTag;

namespace ReplaceTagProcessor {
    // The code units Replace writes for pSrc, with the terminator.
    u32 getReplaceRoom(const char16_t* pSrc);
    u32 Replace(char16_t* pDst, const char16_t* pSrc);
    u32 ReplaceArgs(char16_t* pDst, const char16_t* pSrc, va_list args);
    u32 exePictureGroup(char16_t* pDst, const MessageEditorMessageTag& rTag);
    u32 exeStringGroup(char16_t* pDst, const MessageEditorMessageTag& rTag, va_list args);
    // The code units exeStringGroup writes for the tag, without the terminator.
    u32 getStringGroupRoom(const MessageEditorMessageTag& rTag, va_list args);
    u32 exeNumberGroup(char16_t* pDst, const MessageEditorMessageTag& rTag, va_list args);
    u32 exeLocalizeGroup(char16_t* pDst, const MessageEditorMessageTag& rTag);
    // The code units exeLocalizeGroup writes for the tag, with the terminator.
    u32 getLocalizeGroupRoom(const MessageEditorMessageTag& rTag);
    u32 exeLocalizeGroupPlayerName(char16_t* pDst, u8 form);
    u32 exeRaceTimeGroup(char16_t* pDst, const MessageEditorMessageTag& rTag);
    // The code units exeRaceTimeGroup writes for the tag, without the terminator.
    u32 getRaceTimeGroupRoom(const MessageEditorMessageTag& rTag);
};  // namespace ReplaceTagProcessor

namespace ReplaceTagFunction {
    // Writes pFormat with its tags replaced into pDst (length units with the terminator, cut to fit) and returns the
    // whole text's units without the terminator, cut or not. A null pDst only measures.
    u32 ReplaceArgs(char16_t* pDst, s32 length, const char16_t* pFormat, ...);
};  // namespace ReplaceTagFunction
