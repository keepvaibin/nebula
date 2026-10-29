#include "JSystem/JUtility/JUTNativeLog.hpp"
#include <nebula_game_api.h>
#include <stdio.h>

extern "C" const NebulaHostServices* nebula_host(void);

namespace {
    char sLine[1024];
    size_t sLength = 0;
    bool sLock = false;  // held with the compiler's atomic test-and-set

    void emit() {
        const NebulaHostServices* pHost = nebula_host();
        sLine[sLength] = '\0';
        if (pHost != nullptr && pHost->log != nullptr) {
            pHost->log(pHost->user, sLine);
        } else {
            fputs(sLine, stderr);
            fputc('\n', stderr);
        }
        sLength = 0;
    }
}  // namespace

void JUTNativeLogV(const char* pFormat, va_list args) {
    char text[1024];
    const int n = vsnprintf(text, sizeof(text), pFormat, args);
    const size_t length = n < 0 ? 0 : (static_cast< size_t >(n) < sizeof(text) ? static_cast< size_t >(n) : sizeof(text) - 1);
    while (__atomic_test_and_set(&sLock, __ATOMIC_ACQUIRE)) {
    }
    for (size_t i = 0; i < length; i++) {
        if (text[i] == '\n') {
            emit();
            continue;
        }
        if (sLength == sizeof(sLine) - 1) {
            emit();
        }
        sLine[sLength++] = text[i];
    }
    __atomic_clear(&sLock, __ATOMIC_RELEASE);
}

void JUTNativeLog(const char* pFormat, ...) {
    va_list args;
    va_start(args, pFormat);
    JUTNativeLogV(pFormat, args);
    va_end(args);
}
