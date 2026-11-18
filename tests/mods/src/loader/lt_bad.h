/* Pointers a mod gets wrong, for the bad-pointer tests (M-029): a service given one answers LUMA_E_ARGUMENT, reports
   it, and the game and the mod go on. */
#ifndef LT_BAD_H
#define LT_BAD_H
#include <stdint.h>

/* The first page: mapped by no system. */
#define LT_UNMAPPED ((void*)(uintptr_t)0x1000)
/* The mod's read-only data: 1024 bytes whose size field says 1024, so a struct's size check passes and the write is
   what fails; read as a text, "" (its first byte is 0). */
static const uint32_t lt_read_only[256] = {1024u};
#define LT_READ_ONLY ((void*)(uintptr_t)lt_read_only)
/* Data, not code: a function there is in no code of the mod's. */
#define LT_DATA_FN ((void*)(uintptr_t)lt_read_only)

#endif
