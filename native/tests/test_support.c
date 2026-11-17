// SPDX-License-Identifier: GPL-3.0-only
// What the game's unit tests (native/tests/CMakeLists.txt) take from the module's host in its place: a panic says where
// and what, then ends the test with exit code 70 (NebulaModule.c's, in the game, asks the host to stop). A test that
// expects a panic sets nebula_test_panic_trap: the panic's text goes to nebula_test_panic_text and it jumps there.
#include "test_support.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

jmp_buf* nebula_test_panic_trap;
char nebula_test_panic_text[512];

void nebula_panic(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (nebula_test_panic_trap != NULL) {
        jmp_buf* const trap = nebula_test_panic_trap;
        nebula_test_panic_trap = NULL;
        vsnprintf(nebula_test_panic_text, sizeof(nebula_test_panic_text), format, args);
        va_end(args);
        longjmp(*trap, 1);
    }
    fprintf(stderr, "panic at %s:%d: ", file, line);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    fflush(stderr);
    // Not abort(): on Windows it can open an error report window, and a hidden run has no one to close it.
    _Exit(70);
}
