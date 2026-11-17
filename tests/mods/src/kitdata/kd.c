/* F's R-6 conformance mods (contract services-mods section 9: kit data), one source, CASE:
   1 kd-lib   the library. At the first frame in the stage: needing() (one mod, kd-user), kit_names of it for module
              "player.form" ("a" and "b"), the length asked first, a short buffer TOO_LARGE; kit_read of "a" (its 7
              bytes '{"x":1}'), of "c" (NOT_FOUND); the refusals: a module with a path part, a name with one, a number
              that is no mod (DENIED, Mods.KitNotNeeded), its own kit (itself: OK, none).
   2 kd-user  needs kd-lib; ships kit/player.form/a.json and b.json (kd-lib gives their schema,
              code/kd-lib.kit/player.form/1.json). At its start reads its own kit: names length 4. Also kd-user-bare,
              which needs kd-bare, a library that gives no schema: refused at load (L702), its start never runs.
   3 kd-other needs nothing; at the first frame its needing() count is 0 (kd-user needs kd-lib, not it). Also kd-bare:
              its one user refused, its count is 0. */
#include "../loader/lt.h"
#include <luma_mods.h>

#ifndef CASE
#error CASE
#endif

static const LumaModsService* mods;

__attribute__((unused)) static long long same_bytes(const char* a, const char* b, unsigned long long n) {
    for (unsigned long long i = 0; i < n; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

#if CASE == 1
static long long frames_seen;

static int32_t after(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (++frames_seen != 1) return 0;
    uint64_t users[4] = {0, 0, 0, 0};
    uint32_t count = 99;
    lt_fact("needing_status", mods->needing(lt_mod, users, 4, &count));
    lt_fact("needing_count", count);
    lt_fact("needing_null_refused", mods->needing(lt_mod, 0, 1, &count) == LUMA_E_ARGUMENT);
    if (count < 1) return 0;
    const uint64_t of = users[0];
    uint64_t length = 99;
    lt_fact("names_length_status", mods->kit_names(lt_mod, of, "player.form", 0, 0, &length));
    lt_fact("names_length", (long long)length);
    char small[2];
    uint64_t needed = 0;
    lt_fact("names_short", mods->kit_names(lt_mod, of, "player.form", small, sizeof(small), &needed) == LUMA_E_TOO_LARGE &&
                               needed == 4);
    char names[16] = {0};
    lt_fact("names_status", mods->kit_names(lt_mod, of, "player.form", names, sizeof(names), &length));
    lt_fact("names_ok", length == 4 && same_bytes(names, "a\0b\0", 4));
    lt_fact("names_other_module", mods->kit_names(lt_mod, of, "actor", names, sizeof(names), &length) == LUMA_OK &&
                                      length == 0);
    char bytes[32];
    lt_fact("read_status", mods->kit_read(lt_mod, of, "player.form", "a", bytes, sizeof(bytes), &length));
    lt_fact("read_ok", length == 7 && same_bytes(bytes, "{\"x\":1}", 7));
    lt_fact("read_again", mods->kit_read(lt_mod, of, "player.form", "a", bytes, sizeof(bytes), &length) == LUMA_OK &&
                              length == 7);
    lt_fact("read_missing", mods->kit_read(lt_mod, of, "player.form", "c", bytes, sizeof(bytes), &length));
    lt_fact("module_path_refused", mods->kit_names(lt_mod, of, "../x", names, sizeof(names), &length) == LUMA_E_ARGUMENT);
    lt_fact("name_path_refused", mods->kit_read(lt_mod, of, "player.form", "a/b", bytes, sizeof(bytes), &length) ==
                                     LUMA_E_ARGUMENT);
    lt_fact("dot_name_refused", mods->kit_read(lt_mod, of, "player.form", ".a", bytes, sizeof(bytes), &length) ==
                                    LUMA_E_ARGUMENT);
    lt_fact("no_mod_denied", mods->kit_names(lt_mod, 12345, "player.form", names, sizeof(names), &length) ==
                                 LUMA_E_DENIED);
    lt_fact("own_ok", mods->kit_names(lt_mod, (uint64_t)(uintptr_t)lt_mod, "player.form", names, sizeof(names), &length) ==
                          LUMA_OK && length == 0);
    lt_fact("null_length_refused", mods->kit_names(lt_mod, of, "player.form", names, sizeof(names), 0) == LUMA_E_ARGUMENT);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0"),
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after),
};
LT_START((void)mod)
#elif CASE == 2
static const LumaImport imports[] = {LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0")};

static void begin(const LumaMod* mod) {
    uint64_t length = 99;
    lt_fact("own_status", mods->kit_names(mod, (uint64_t)(uintptr_t)mod, "player.form", 0, 0, &length));
    lt_fact("own_length", (long long)length);
}
LT_START(begin(mod))
#else
static long long frames_seen;

static int32_t after(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (++frames_seen != 1) return 0;
    uint32_t count = 99;
    lt_fact("needing_status", mods->needing(lt_mod, 0, 0, &count));
    lt_fact("needing_count", count);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0"),
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after),
};
LT_START((void)mod)
#endif
LUMA_MOD(imports, start, 0);
