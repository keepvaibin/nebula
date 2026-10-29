#include "JSystem/JUtility/JUTException.hpp"
#include <nebula_game_api.h>
#include <stdio.h>

extern "C" const NebulaHostServices* nebula_host(void);

void JUTException::panic_f_va(const char* file, int line, const char* format, va_list args) {
    char message[256];
    char text[320];
    vsnprintf(message, sizeof(message), format, args);
    snprintf(text, sizeof(text), "%s:%d: %s", file, line, message);
    const NebulaHostServices* pHost = nebula_host();
    pHost->fatal(pHost->user, text);
}

void JUTException::panic_f(const char* file, int line, const char* format, ...) {
    va_list args;
    va_start(args, format);
    panic_f_va(file, line, format, args);
    va_end(args);
}
