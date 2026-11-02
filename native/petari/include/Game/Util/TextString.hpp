#pragma once

// The game's text is UTF-16: these are what the host's C library gives narrow strings.

#include <cstdarg>
#include <cstddef>

namespace MR {
    size_t textLength(const char16_t* pText);
    // pSrc's first count characters, the rest of the count zero.
    char16_t* copyText(char16_t* pDst, const char16_t* pSrc, size_t count);

    // Writes at most `length` characters with the terminator; the characters written without it, or -1 when it did not
    // fit (pDst then ends at its last character). Conversions: %d %i %u %x %X %c %s (narrow), %ls (text), %%, with the
    // flags 0 - and a width; at anything else after a %, the rest of the format is copied as it stands and ends the text.
    // pDst may also be an argument at the same position (the game appends to a text that way).
    int formatText(char16_t* pDst, size_t length, const char16_t* pFormat, ...);
    int formatTextV(char16_t* pDst, size_t length, const char16_t* pFormat, va_list args);
}  // namespace MR
