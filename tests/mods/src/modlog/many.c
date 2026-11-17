/* jl-many<WHO> (test 3125): COUNT distinct reports at start (jl_many.lumi lines 1 to COUNT, default 300): past the
   log's 256 a mod they are counted in one ModLog.TooMany, never kept. */
#include "jl.h"

#ifndef COUNT
#define COUNT 300
#endif

JL_START({
    for (uint32_t line = 1; line <= COUNT; line++) jl_report("jl_many.lumi", line);
})

static const LumaFunction* unused;
static const LumaImport imports[] = {
    LUMA_FUNCTION(unused, "MR::getPlayerLeft", "s32()"),
};
LUMA_MOD(imports, start, 0);
