/* lt-api0: a luma_mod that says api 0 (not built with luma.h's LUMA_MOD): refused, its start never runs
   (Validator.Lib.NotLumaH: "its luma_mod says api 0"). */
#include "lt.h"

LT_START((void)0)
LUMA_RUNTIME
/* By field name: luma.h may add fields at the end (D3-3 did), which stay zero. */
LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {
    .size = sizeof(LumaModInfo), .api = 0, .start = start, .begin = luma_begin, .finish = luma_finish, .runtime_layout = 0};
