/* lt-rep1 / lt-rep2 (-DWHO=1 or 2): a replace of MR::getPlayerLeft s32() giving WHO * 100 (loader contract 1). The
   higher mod in the list holds the replace; the lower is refused with L805 naming the higher, and stays off whole. */
#include "lt.h"

#ifndef WHO
#error WHO
#endif

static int32_t replace(LumaCall* call, void* user) {
    (void)user;
    LUMA_RESULT(call, int32_t) = WHO * 100;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MR::getPlayerLeft", "s32()", LUMA_REPLACE, replace),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
