#pragma once

#include <revolution.h>

namespace JUTAssertion {
    void create();
    u32 flush_subroutine();
    void flushMessage();
    void flushMessage_dbPrint();
    void setVisible(bool);
    void setMessageCount(int);
    void changeDevice(u32);
    void changeDisplayTime(u32 time);

    u32 getSDevice();
    void showAssert_f(u32 device, const char* file, int line, const char* msg, ...) ATTRIBUTE_PRINTF(4, 5);
#ifdef TARGET_PC
    void showAssert_f_va(u32 device, const char* file, int line, const char* msg, va_list args);
#else
    void showAssert_f_va(u32 device, const char* file, int line, const char* msg, __va_list args);
#endif
    void setWarningMessage_f(u32 device, char* file, int line, const char* fmt, ...) ATTRIBUTE_PRINTF(4, 5);
    void setWarningMessage_f_va(u32 device, const char* file, int line, const char* msg, va_list args);
    void setLogMessage_f(u32 device, char* file, int line, const char* fmt, ...) ATTRIBUTE_PRINTF(4, 5);
    void setConfirmMessage(u32 param_1, char* file, int line, bool param_4, const char* msg);

    inline void showAssert(u32 device, const char* file, int line, const char* msg) {
        showAssert_f(device, file, line, "%s", msg);
    }

    inline void setWarningMessage(u32 device, char* file, int line, const char* msg) {
        setWarningMessage_f(device, file, line, "%s", msg);
    }
};  // namespace JUTAssertion
