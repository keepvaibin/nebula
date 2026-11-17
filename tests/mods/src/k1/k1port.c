/* The kernel freeze review's number option mod: one option "port", a range of 65535 values (1 to 65535 by 1, default
   16834), as `lumalink add option port --kind number --min 1 --max 65535` writes it. At MR::getRandom(0, 4462) it reads
   the option through mods.option and writes:
     status    what mods.option returned (the text's length, or an error)
     port      the text read as a whole number (-1 when it is not one)
     len       the length asked with a null buf */
#include "../loader/lt.h"
#include <luma_mods.h>

static const LumaModsService* mods;

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4462) return 0;
    char buf[32];
    const int64_t n = mods->option(lt_mod, "port", buf, sizeof(buf));
    lt_fact("status", n);
    long long value = n > 0 ? 0 : -1;
    for (int64_t i = 0; i < n; i++) {
        if (buf[i] < '0' || buf[i] > '9') {
            value = -1;
            break;
        }
        value = value * 10 + (buf[i] - '0');
    }
    lt_fact("port", value);
    lt_fact("len", mods->option(lt_mod, "port", 0, 0));
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
