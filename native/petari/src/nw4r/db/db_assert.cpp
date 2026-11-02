#include "nw4r/db/assert.h"
#include "Game/System/GameThreads.hpp"
#include <JSystem/JUtility/JUTNativeLog.hpp>
#include "nw4r/db/console.h"
#include "nw4r/db/mapFile.h"
#include <cstdarg>
#include "Game/System/NebulaFrameClock.hpp"

extern "C" __attribute__((noreturn)) void nebula_fatal(const char* format, ...);

namespace nw4r {
    namespace db {
        static ConsoleHandle sAssertionConsole = NULL;

        static void Assertion_Printf_(const char* fmt, ...) {
            va_list vlist;
            va_start(vlist, fmt);
            if (sAssertionConsole) {
            } else {
                JUTNativeLogV(fmt, vlist);
            }
            va_end(vlist);
        }

        static bool ShowMapInfoSubroutine_(u32 address, bool preCRFlag) {
            if (address < 0x80000000 || 0x82ffffff < address) {
                return false;
            }

            {
                enum { STR_LEN = 260 };
                u8 strBuf[STR_LEN];
                bool result;

                result = MapFile_QuerySymbol(address, strBuf, STR_LEN);
                if (result) {
                    if (preCRFlag) {
                        Assertion_Printf_("\n");
                    }
                    Assertion_Printf_("%s\n", strBuf);
                    return true;
                } else {
                    return false;
                }
            }
        }

        static void ShowStack_(u32 sp) {
            // The PowerPC back chain has no native counterpart.
            (void)sp;
            return;
        }

        __declspec(weak) void VPanic(const char* file, int line, const char* fmt, va_list vlist, bool halt) {
            u32 stackPointer = 0;
            (void)GameThreads::excludeAudio();


            ShowStack_(stackPointer);

            if (sAssertionConsole) {
                Console_Printf(sAssertionConsole, "%s:%d Panic:", file, line);
                Console_Printf(sAssertionConsole, "\n");

                (void)Console_ShowLatestLine(sAssertionConsole);
                (void)Console_SetVisible(sAssertionConsole, true);
                Console_DrawDirect(sAssertionConsole);
            } else {
                JUTNativeLog("%s:%d Panic:", file, line);
                JUTNativeLogV(fmt, vlist);
                JUTNativeLog("\n");
            }

            if (halt) {
                nebula_fatal("%s:%d: NW4R panic", file, line);
            }
        }

        __declspec(weak) void Panic(const char* file, int line, const char* fmt, ...) {
            va_list vlist;
            va_start(vlist, fmt);
            nw4r::db::VPanic(file, line, fmt, vlist, true);
            va_end(vlist);
        }

    };  // namespace db
};  // namespace nw4r
