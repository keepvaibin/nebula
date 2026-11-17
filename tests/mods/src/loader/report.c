/* lt-report: report (loader contract 6, D3-1). One place reported 10 times: said once in the game's log, then again
   at its tenth ("seen 10 times"). A script code with its args in English. A kernel code (Hook.OutOfMemory). A code
   that is no row: said with the code, never dropped. 300 different lines: past the 256 the Mods page keeps, counted
   and said. Line 0: said without a place. */
#include "lt.h"

LT_START({
    const char* fn[1] = {"lt.example"};
    for (int i = 0; i < 10; i++) core->report(mod, "lt_report.lumi", 12, 901, fn, 1);
    const char* size[1] = {"12345"};
    core->report(mod, "lt_report.lumi", 20, 1, size, 1);
    const char* none[1] = {"x"};
    core->report(mod, "lt_report.lumi", 30, 77, none, 1);
    core->report(mod, "lt_report.lumi", 0, 901, fn, 1);
    for (uint32_t line = 1000; line < 1300; line++) core->report(mod, "lt_many.lumi", line, 901, fn, 1);
})

static const LumaFunction* unused;
static const LumaImport imports[] = {
    LUMA_FUNCTION(unused, "MR::getPlayerLeft", "s32()"),
};
LUMA_MOD(imports, start, 0);
