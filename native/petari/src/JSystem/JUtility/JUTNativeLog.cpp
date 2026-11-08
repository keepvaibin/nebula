#include "JSystem/JUtility/JUTNativeLog.hpp"
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

// Each line goes to the host in one call, which writes it whole from any thread; nothing is shared between calls. Every
// caller ends its text with a newline; a last piece without one is a line of its own.
void JUTNativeLogV(const char* pFormat, va_list args) {
    const NebulaHostServices* pHost = nebula_host();
    if (pHost == nullptr || pHost->log == nullptr) {
        return;
    }

    char text[1024];
    const int n = nebula_vsnprintf(text, sizeof(text), pFormat, args);
    const size_t length = n < 0 ? 0 : (static_cast< size_t >(n) < sizeof(text) ? static_cast< size_t >(n) : sizeof(text) - 1);
    size_t start = 0;
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            text[i] = '\0';
            pHost->log(pHost->user, text + start);
            start = i + 1;
        }
    }
    if (start < length) {
        text[length] = '\0';
        pHost->log(pHost->user, text + start);
    }
}

void JUTNativeLog(const char* pFormat, ...) {
    va_list args;
    va_start(args, pFormat);
    JUTNativeLogV(pFormat, args);
    va_end(args);
}
