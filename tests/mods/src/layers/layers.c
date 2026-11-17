/* The code-layer mods (luma.h `layers`, loader contract 11b). Each has the code layers "a" and "b" (luma.json
   `"when": {"code": true}`), whose own assets are a.txt and b.txt; at start a mod says which of them the content has:
   facts a_on and b_on (assets.find of "<id>:a.txt" and "<id>:b.txt" is LUMA_OK), and asked, how often this process
   called its `layers` (once a start; the facts are the last process's).
   ly-choose     (-DCASE=1): chooses "a".
   ly-option     (-DCASE=2): chooses the value of its option "pick" (a or b), read with mods.option in `layers`.
   ly-unknown    (-DCASE=3): chooses "a" and "zzz", which is not one of its code layers.
   ly-none       (-DCASE=4): has no `layers`: none of its code layers is on.
   ly-fault      (-DCASE=5): faults in `layers`: refused as a fault in its start.
   ly-begin      (-DCASE=6): faults in a global constructor (its begin): refused as a fault in its start.
   ly-lib-fault  (-DCASE=7): a library whose `layers` faults; ly-dep-fault needs it.
   ly-dep-fault  (-DCASE=8): needs ly-lib-fault and binds its ly.ready: left off, the mod it needs did not begin.
   ly-lib-no     (-DCASE=9): a library that chooses "a" and whose start refuses (1); ly-dep-no needs it.
   ly-dep-no     (-DCASE=10): needs ly-lib-no: begun, then dropped when ly-lib-no did not start.
   ly-unstable   (-DCASE=11): chooses the layer the content was not cooked with ("b" when a.txt is there, else "a"),
                              so every answer differs from the content's: one start-again, then Layers.Unstable. */
#include "../loader/lt.h"
#include <luma_assets.h>
#include <luma_mods.h>

#if !defined(CASE) || !defined(SELF)
#error CASE and SELF (the mod's id, a string)
#endif

static const LumaAssetsService* assets;
#if CASE == 2
static const LumaModsService* mods;
#endif
#if CASE == 8 || CASE == 10
static const void* ready_fn;
#endif
static int32_t asked;
__attribute__((unused)) static int32_t* volatile nowhere;

__attribute__((unused)) static int32_t is_ready(void) {
    return 1;
}

/* LUMA_OK when the mod's own asset `name` is in the content. */
static int32_t own(const LumaMod* mod, const char* name) {
    LumaAssetInfo info;
    unsigned char* p = (unsigned char*)&info;
    for (uint32_t i = 0; i < sizeof(info); i++) p[i] = 0;
    info.size = sizeof(info);
    char path[96];
    char* at = lt_put(path, SELF ":");
    at = lt_put(at, name);
    *at = 0;
    return assets->find(mod, path, 0, &info);
}

/* Writes the names (each with its NUL) when they fit; returns the bytes they need. */
__attribute__((unused)) static int64_t answer(char* buf, uint64_t size, const char* names, uint64_t bytes) {
    if (buf != 0 && size >= bytes) {
        for (uint64_t i = 0; i < bytes; i++) buf[i] = names[i];
    }
    return (int64_t)bytes;
}

__attribute__((unused)) static int64_t layers(const LumaCore* core, const LumaMod* mod, char* buf, uint64_t size) {
    lt_core = core;
    lt_mod = mod;
    (void)size;
    if (buf == 0) asked++;
#if CASE == 1 || CASE == 9
    return answer(buf, size, "a", 2);
#elif CASE == 2
    char pick[16];
    const int64_t length = mods->option(mod, "pick", pick, sizeof(pick));
    if (length <= 0 || length >= (int64_t)sizeof(pick)) return 0;
    return answer(buf, size, pick, (uint64_t)length + 1u);
#elif CASE == 3
    return answer(buf, size, "a\0zzz", 6);
#elif CASE == 5 || CASE == 7
    lt_fact("entered", 1);
    *nowhere = 1;
    lt_fact("survived", 1);
    return 0;
#elif CASE == 11
    return own(mod, "a.txt") == LUMA_OK ? answer(buf, size, "b", 2) : answer(buf, size, "a", 2);
#else
    return 0;
#endif
}

#if CASE == 6
__attribute__((constructor)) static void construct(void) {
    *nowhere = 1;
}
#endif

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    lt_core = core;
    lt_mod = mod;
    lt_fact("asked", asked);
    lt_fact("a_on", own(mod, "a.txt") == LUMA_OK);
    lt_fact("b_on", own(mod, "b.txt") == LUMA_OK);
#if CASE == 8 || CASE == 10
    lt_fact("bound", ready_fn != 0);
#endif
    lt_fact("started", 1);
#if CASE == 9
    return 1;
#else
    return 0;
#endif
}

#if CASE == 7 || CASE == 9
static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("ly.ready", "lumi1:00000000000000b1", is_ready),
};
#endif

static const LumaImport imports[] = {
    LUMA_SERVICE(assets, LUMA_ASSETS_NAME, "1.0"),
#if CASE == 2
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0"),
#elif CASE == 8
    LUMA_MOD_FUNCTION(ready_fn, "ly-lib-fault/ly.ready", "lumi1:00000000000000b1"),
#elif CASE == 10
    LUMA_MOD_FUNCTION(ready_fn, "ly-lib-no/ly.ready", "lumi1:00000000000000b1"),
#endif
};

LUMA_RUNTIME
#if CASE == 4
LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, imports, sizeof(imports) / sizeof(imports[0]), start, 0, luma_begin, luma_finish, 0, 0, 0, 0, 0};
#elif CASE == 7 || CASE == 9
LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, imports, sizeof(imports) / sizeof(imports[0]), start, 0, luma_begin, luma_finish, exports, sizeof(exports) / sizeof(exports[0]), 0, layers, 0};
#else
LUMA_C LUMA_EXPORT const LumaModInfo luma_mod = {sizeof(LumaModInfo), LUMA_API, imports, sizeof(imports) / sizeof(imports[0]), start, 0, luma_begin, luma_finish, 0, 0, 0, layers, 0};
#endif
