// SPDX-License-Identifier: GPL-3.0-only
// What the game's unit tests (native/tests/CMakeLists.txt) take from the module's host in its place: a panic says where
// and what, then ends the test with a failure (NebulaModule.c's, in the game, asks the host to stop).
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

void nebula_panic(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    fprintf(stderr, "panic at %s:%d: ", file, line);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    abort();
}
