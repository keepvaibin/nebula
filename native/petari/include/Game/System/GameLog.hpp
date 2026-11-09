#pragma once

#include <stdarg.h>

// The game's log through the host (nebula_game_api.h log): text gathered into lines, each line sent when it ends.
#ifdef __cplusplus
extern "C" {
#endif
void GameLog(const char* pFormat, ...);
void GameLogV(const char* pFormat, va_list args);
#ifdef __cplusplus
}
#endif
