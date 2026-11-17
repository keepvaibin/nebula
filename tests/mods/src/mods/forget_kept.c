/* 2291a/2291b (contract services-save, "Values kept for a mod: save 1.1"; services-mods 8; mods 1.1 FORGOTTEN).
   CASE 1, fg-lib, a library: on MR::getRandom(0, 4290) it sets "fg-user/kept" (a value kept for fg-user) and its own
   "own"; save.global refuses a key with '/'. On 4291, 4292, 4293 it reads both back (facts <name>_before, _after,
   _loaded: the value, or the status). It watches the mods: each FORGOTTEN event counts, with whether its id is
   "fg-user" and its length.
   CASE 2, fg-user: starts and does nothing; it has no value of its own, so the REMOVED MODS line lists it only for
   what fg-lib keeps for it. */
#include "../loader/lt.h"
#include <luma_mods.h>
#include <luma_save.h>

#ifndef CASE
#error CASE
#endif

#if CASE == 1
static const LumaSaveService* save;
static const LumaSaveGlobalService* global;
static const LumaModsService* mods;
static long long forgotten;

static int same(const char* a, const char* b) {
    while (*a != 0 && *a == *b) {
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static long long get_number(const char* key) {
    uint32_t value = 0;
    const int64_t length = save->get(lt_mod, key, &value, sizeof(value));
    return length == (int64_t)sizeof(value) ? (long long)value : (long long)length;
}

static void read_both(const char* for_fact, const char* own_fact) {
    lt_fact(for_fact, get_number("fg-user/kept"));
    lt_fact(own_fact, get_number("own"));
}

static void on_mods(void* user, const LumaEvent* event) {
    (void)user;
    if (event->kind != LUMA_MODS_EVENT_FORGOTTEN) return;
    forgotten++;
    lt_fact("forgotten_count", forgotten);
    lt_fact("forgotten_user", event->data != 0 && same((const char*)event->data, "fg-user"));
    lt_fact("forgotten_size", (long long)event->data_size);
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t); /* args[0] is the this slot, null for a free function */
    if (k == 4290) {
        uint32_t kept = 0x2291u;
        uint32_t own = 0x2292u;
        lt_fact("set_for", save->set(lt_mod, "fg-user/kept", &kept, sizeof(kept)));
        lt_fact("set_own", save->set(lt_mod, "own", &own, sizeof(own)));
        lt_fact("set_bad_id", save->set(lt_mod, "FG/kept", &kept, sizeof(kept)));
        lt_fact("global_for", global->set(lt_mod, "fg-user/kept", &kept, sizeof(kept)));
    } else if (k == 4291) {
        read_both("for_before", "own_before");
    } else if (k == 4292) {
        read_both("for_after", "own_after");
    } else if (k == 4293) {
        read_both("for_loaded", "own_loaded");
    } else {
        return 0;
    }
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(save, LUMA_SAVE_NAME, "1.1"),
    LUMA_SERVICE(global, LUMA_SAVE_GLOBAL_NAME, "1.0"),
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.1"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({
    forgotten = 0;
    lt_fact("watch", mods->watch_mods(lt_mod, on_mods, 0));
})
LUMA_MOD(imports, start, 0);
#else
LT_START({})
LUMA_MOD_NO_IMPORTS(start, 0);
#endif
