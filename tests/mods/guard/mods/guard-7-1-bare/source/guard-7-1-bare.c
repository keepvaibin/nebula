/* guard-7-1-bare: guard/guard.c with WHERE=7, FAULT=1, and no unwind data for any of its functions (as a library
   from another toolchain may come): the guard must find the boundary without it (M-027). */
#pragma clang attribute push (__attribute__((nouwtable)), apply_to = hasType(functionType))
#define WHERE 7
#define FAULT 1
#include "../../../../src/guard/guard.c"
#pragma clang attribute pop
