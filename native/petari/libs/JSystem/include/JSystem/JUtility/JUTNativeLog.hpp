#pragma once

#include <revolution/types.h>
#include <stdarg.h>

// The game's log through the host (nebula_game_api.h log): text gathered into lines, each line sent when it ends.
#ifdef __cplusplus
extern "C" {
#endif
void JUTNativeLog(const char* pFormat, ...);
void JUTNativeLogV(const char* pFormat, va_list args);
#ifdef __cplusplus
}
#endif
