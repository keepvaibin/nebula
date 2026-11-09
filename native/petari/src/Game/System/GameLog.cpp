#include "Game/System/GameLog.hpp"
#include <nebula_game_api.h>

extern "C" const NebulaHostServices* nebula_host(void);

// Each line goes to the host in one call, which writes it whole from any thread; nothing is shared between calls. Every
// caller ends its text with a newline; a last piece without one is a line of its own. Text of any length: up to 1023
// characters on the stack, longer in the host's heap (never the game's, so the arena can log its own state).
void GameLogV(const char* pFormat, va_list args) {
    const NebulaHostServices* pHost = nebula_host();
    if (pHost == nullptr || pHost->log == nullptr) {
        return;
    }

    char text[1024];
    va_list again;
    va_copy(again, args);
    const int n = nebula_vsnprintf(text, sizeof(text), pFormat, args);
    char* pText = text;
    size_t length = n < 0 ? 0 : static_cast< size_t >(n);
    if (length >= sizeof(text)) {
        pText = pHost->heap_alloc != nullptr ? static_cast< char* >(pHost->heap_alloc(pHost->user, length + 1)) : nullptr;
        if (pText != nullptr) {
            nebula_vsnprintf(pText, length + 1, pFormat, again);
        } else {
            pText = text;
            length = sizeof(text) - 1;
        }
    }
    va_end(again);

    size_t start = 0;
    for (size_t i = 0; i < length; i++) {
        if (pText[i] == '\n') {
            pText[i] = '\0';
            pHost->log(pHost->user, pText + start);
            start = i + 1;
        }
    }
    if (start < length) {
        pText[length] = '\0';
        pHost->log(pHost->user, pText + start);
    }
    if (pText != text) {
        pHost->heap_free(pHost->user, pText);
    }
}

void GameLog(const char* pFormat, ...) {
    va_list args;
    va_start(args, pFormat);
    GameLogV(pFormat, args);
    va_end(args);
}
