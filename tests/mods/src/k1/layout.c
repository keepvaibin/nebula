/* Two mods whose LumaModInfo.runtime_layout differ, both non-zero: the one that calls the other's function is refused
   by name (L513) before its signature is compared; the one it needs loads and starts.
   k1-layout-lib  (-DCASE=1): runtime layout 1, exports k1.layout.
   k1-layout-user (-DCASE=2): runtime layout 2, needs k1-layout-lib, imports k1-layout-lib/k1.layout with the export's
                              own signature (so only the layout differs). */
#include "../loader/lt.h"

#ifndef CASE
#error CASE
#endif

#define SIG "lumi1:00000000000000b1"

#if CASE == 1
static int32_t layout(void) {
    return 1;
}

static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("k1.layout", SIG, layout),
};

LT_START((void)0)
LUMA_RUNTIME
LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, 0, 0, start, 0, luma_begin, luma_finish,
                                                 exports, sizeof(exports) / sizeof(exports[0]), 0, 0, 1};
#else
typedef int32_t (*Layout)(void);
static const void* fn;

static const LumaImport imports[] = {
    LUMA_MOD_FUNCTION(fn, "k1-layout-lib/k1.layout", SIG),
};

LT_START(lt_fact("layout", fn != 0 ? ((Layout)fn)() : 0))
LUMA_RUNTIME
LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, imports, sizeof(imports) / sizeof(imports[0]),
                                                 start, 0, luma_begin, luma_finish, 0, 0, 0, 0, 2};
#endif
