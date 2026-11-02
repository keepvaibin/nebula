/* The game's narrow formatting: the host's, with a null string printing as nothing (the game builds archive paths with
   optional parts; the host's C library prints "(null)"). */
#ifndef NEBULA_FORMAT_H
#define NEBULA_FORMAT_H

#include <stdarg.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((format(printf, 3, 0))) int nebula_vsnprintf(char* pDst, size_t length, const char* pFormat, va_list args);
__attribute__((format(printf, 3, 4))) int nebula_snprintf(char* pDst, size_t length, const char* pFormat, ...);
__attribute__((format(printf, 2, 3))) int nebula_sprintf(char* pDst, const char* pFormat, ...);
__attribute__((format(printf, 2, 0))) int nebula_vsprintf(char* pDst, const char* pFormat, va_list args);

#ifdef __cplusplus
}
#endif

#endif
