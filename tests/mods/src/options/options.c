/* H's options conformance mods (services-mods 8), against mods.option and mods.watch_options (native/include/
   luma_mods.h). One source, one case each (-DCASE=n). Run steps call MR::getRandom(0, 4400 + k) and the hook gives 1.
   opt-read      (1) 2253: at k 1 each option at the manifest's defaults: len_<key> (the length asked with a null buf),
                      got_<key> (the length written), same_<key> (the bytes and the NUL are the expected text).
   opt-mistakes  (2) 2255: at k 1 an unknown key, another mod's key, a null key, a short buf (and that nothing was
                      written), a null buf with a size, a buf of exactly the length + 1; watch_options with and without
                      a fn: LUMA_OK. */
#include "../loader/lt.h"
#include <luma_mods.h>

#ifndef CASE
#error CASE
#endif

static const LumaModsService* mods;

__attribute__((unused)) static int32_t same_text(const char* got, const char* want, int64_t n) {
    for (int64_t i = 0; i <= n; i++) { /* the NUL too */
        if (got[i] != want[i]) return 0;
    }
    return 1;
}

__attribute__((unused)) static int64_t length(const char* a) {
    int64_t n = 0;
    while (a[n] != 0) n++;
    return n;
}

/* fact <what>_<key> <v> */
__attribute__((unused)) static void key_fact(const char* what, const char* key, long long v) {
    char name[48];
    char* at = lt_put(name, what);
    *at++ = '_';
    at = lt_put(at, key);
    *at = 0;
    lt_fact(name, v);
}

#if CASE == 1
static void read_one(const char* key, const char* want) {
    char buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 'z';
    key_fact("len", key, mods->option(lt_mod, key, 0, 0));
    const int64_t n = mods->option(lt_mod, key, buf, sizeof(buf));
    key_fact("got", key, n);
    key_fact("same", key, n == length(want) && same_text(buf, want, n));
}
#endif

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4400;
    if (k < 0 || k > 9) return 0;
#if CASE == 1
    if (k == 1) {
        read_one("lives", "three");
        read_one("speed", "1.5");
        read_one("hard", "1");
        read_one("label", "Hi \xE2\x98\x85");
        read_one("jump", "A");
    }
#elif CASE == 2
    if (k == 1) {
        char buf[8];
        for (int i = 0; i < 8; i++) buf[i] = 'z';
        lt_fact("unknown", mods->option(lt_mod, "nope", buf, sizeof(buf)));
        lt_fact("not_own", mods->option(lt_mod, "opt-read/lives", buf, sizeof(buf)));
        lt_fact("null_key", mods->option(lt_mod, 0, buf, sizeof(buf)));
        lt_fact("short", mods->option(lt_mod, "label", buf, 6)); /* "Hi ★" is 6 bytes: 7 with its NUL */
        int32_t untouched = 1;
        for (int i = 0; i < 8; i++) untouched = untouched && buf[i] == 'z';
        lt_fact("untouched", untouched);
        lt_fact("null_buf", mods->option(lt_mod, "label", 0, 4));
        lt_fact("exact", mods->option(lt_mod, "label", buf, 7));
        lt_fact("exact_nul", buf[6] == 0);
        lt_fact("watch", mods->watch_options(lt_mod, 0, 0));
    }
#endif
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
