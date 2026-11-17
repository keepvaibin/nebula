/* jl-repeat (test 3123): the same (file, line, code) reported 1,000 times at start: one mod-log entry counted 1000,
   one summary notice at the title, the kernel's own log lines at 1, 10, 100, 1000 and none from the mod log. */
#include "jl.h"

JL_START({
    for (int i = 0; i < 1000; i++) jl_report("jl_repeat.lumi", 12);
})

static const LumaFunction* unused;
static const LumaImport imports[] = {
    LUMA_FUNCTION(unused, "MR::getPlayerLeft", "s32()"),
};
LUMA_MOD(imports, start, 0);
