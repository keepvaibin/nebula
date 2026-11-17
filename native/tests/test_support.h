// SPDX-License-Identifier: GPL-3.0-only
// test_support.c: a test catches an expected panic with NEBULA_TEST_PANICS(call): true when the call panicked, its text in
// nebula_test_panic_text.
#pragma once

#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif

extern jmp_buf* nebula_test_panic_trap;
extern char nebula_test_panic_text[512];

#ifdef __cplusplus
}
#endif

// The call must have no object with a destructor between it and the panic: the jump skips them.
#define NEBULA_TEST_PANICS(call)                                                                                              \
    ([&]() -> bool {                                                                                                          \
        jmp_buf trap;                                                                                                         \
        nebula_test_panic_text[0] = '\0';                                                                                     \
        if (setjmp(trap) != 0) {                                                                                              \
            return true;                                                                                                      \
        }                                                                                                                     \
        nebula_test_panic_trap = &trap;                                                                                       \
        call;                                                                                                                 \
        nebula_test_panic_trap = nullptr;                                                                                     \
        return false;                                                                                                         \
    }())
