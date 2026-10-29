#pragma once

#include <revolution/types.h>
#include <stdarg.h>

// A fatal error the game reports (a broken archive, a bad sequence): the host's report (nebula_game_api.h fatal)
// with the file, line and message. The console's crash screen is the host's crash report natively.
class JUTException {
public:
    static void panic_f_va(char const*, int, char const*, va_list);
    static void panic_f(char const*, int, char const*, ...) ATTRIBUTE_PRINTF(3, 4);
    static void panic(const char* file, int line, const char* msg) {
        panic_f(file, line, "%s", msg);
    }
};
