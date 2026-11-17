/* H's services-mods conformance mods (contract services-mods 3), against the host's mods services 1.0
   (native/include/luma_mods.h). One source, one case each (-DCASE=n); the generator's names come in by -D too.
   mods-a       (1) the list (k 1), mods.ids (k 2, 3, 4, 7), a small struct (k 5), mods.install (k 6), of_mod (k 9).
   mods-b       (2) another mod's numbers (k 8): `of` mods-a's keys, `get` of a name meeting mods-a's KA.
   mods-denied  (2) the same code, its code consent not given (NOT_STARTED in mods-a's list).
   mods-refused (3) imports `mods` 1.9 (newer than the provider's 1.0): L505, REFUSED in mods-a's list.
   Run steps call MR::getRandom(0, 4250 + k) and the hook gives 1. Texts are given as their fnv1a32 (the generator
   computes the same over the UTF-8 bytes); numbers as they are. */
#include "../loader/lt.h"
#include <luma_mods.h>

#ifndef CASE
#error CASE
#endif
#if CASE == 1 && (!defined(C1) || !defined(C2) || !defined(KA))
#error C1, C2, KA: mods-a's names, from make_tests.py
#endif
#if CASE == 2 && !defined(KB)
#error KB: mods-b's name, from make_tests.py
#endif

__attribute__((unused)) static const LumaModsService* mods;
__attribute__((unused)) static const LumaModsIdsService* ids;
__attribute__((unused)) static const LumaModsInstallService* install;

__attribute__((unused)) static long long text_hash(const char* text) {
    uint32_t hash = 2166136261u;
    for (const unsigned char* p = (const unsigned char*)text; *p != 0; p++) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return (long long)hash;
}

/* A fact key `<head><n>` without a formatter. */
__attribute__((unused)) static const char* key_n(char* out, const char* head, uint32_t n) {
    int i = 0;
    while (head[i] != 0) {
        out[i] = head[i];
        i++;
    }
    out[i++] = (char)('0' + n % 10u);
    out[i] = 0;
    return out;
}

__attribute__((unused)) static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    lt_fact("event", event->kind); /* none may come: install answers LUMA_E_NOT_FOUND (no such file in its data folder) */
}

__attribute__((unused)) static void find_one(const char* fact, const char* state_fact, const char* id) {
    LumaModsInfo info = {.size = sizeof(LumaModsInfo)};
    const int32_t status = mods->find(lt_mod, id, &info);
    lt_fact(fact, status);
    if (status == LUMA_OK) {
        lt_fact(state_fact, info.state);
    }
}

__attribute__((unused)) static void get_one(const char* fact, const char* space, const char* name) {
    uint32_t number = 0;
    const int32_t status = ids->get(lt_mod, space, name, &number);
    lt_fact(fact, status == LUMA_OK ? (long long)number : (long long)status);
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4250;
    if (k < 0 || k > 9) return 0;
#if CASE == 1
    if (k == 1) {
        const uint32_t count = mods->count(lt_mod);
        lt_fact("count", count);
        char key[16];
        for (uint32_t i = 0; i < count && i < 10u; i++) {
            LumaModsInfo info = {.size = sizeof(LumaModsInfo)};
            lt_fact(key_n(key, "at", i), mods->at(lt_mod, i, &info));
            lt_fact(key_n(key, "order", i), info.order);
            lt_fact(key_n(key, "kind", i), info.kind);
            lt_fact(key_n(key, "state", i), info.state);
            lt_fact(key_n(key, "code", i), info.has_code);
            lt_fact(key_n(key, "reserved", i), info.reserved);
            lt_fact(key_n(key, "id", i), text_hash(info.id));
            lt_fact(key_n(key, "name", i), text_hash(info.name));
            lt_fact(key_n(key, "version", i), text_hash(info.version));
        }
        LumaModsInfo past = {.size = sizeof(LumaModsInfo)};
        lt_fact("at_past", mods->at(lt_mod, count, &past));
        find_one("find_refused", "state_refused", "mods-refused");
        find_one("find_denied", "state_denied", "mods-denied");
        find_one("find_self", "state_self", "mods-a");
        find_one("find_none", "state_none", "mods-zz");
    } else if (k == 2) {
        get_one("breath1", LUMA_MODS_SPACE_MESSAGE, "breath");
        get_one("breath2", LUMA_MODS_SPACE_MESSAGE, "breath");
        get_one("sbreath", LUMA_MODS_SPACE_SENSOR, "breath");
    } else if (k == 3) {
        get_one("c1", LUMA_MODS_SPACE_MESSAGE, C1);
        get_one("c2", LUMA_MODS_SPACE_MESSAGE, C2);
        get_one("ka", LUMA_MODS_SPACE_MESSAGE, KA);
    } else if (k == 4) {
        uint32_t number = 0;
        lt_fact("bad_space", ids->get(lt_mod, "switch", "breath", &number));
        lt_fact("bad_name", ids->get(lt_mod, LUMA_MODS_SPACE_MESSAGE, "Bad Name", &number));
        lt_fact("bad_mod", ids->of(lt_mod, LUMA_MODS_SPACE_MESSAGE, "Mods/A", "breath", &number));
        lt_fact("null_out", ids->get(lt_mod, LUMA_MODS_SPACE_MESSAGE, "breath", 0));
    } else if (k == 5) {
        LumaModsInfo info = {.size = 8};
        lt_fact("at_8", mods->at(lt_mod, 0, &info));
    } else if (k == 6) {
        LumaHandle handle = 0;
        lt_fact("install", install->request(lt_mod, "packs/extra.luma", on_event, 0, &handle));
        lt_fact("install_up", install->request(lt_mod, "../extra.luma", on_event, 0, &handle));
        lt_fact("install_kind", install->request(lt_mod, "packs/extra.zip", on_event, 0, &handle));
        lt_fact("install_cancel", install->cancel(lt_mod, 0x1234u));
    } else if (k == 7) {
        get_one("r_c2", LUMA_MODS_SPACE_MESSAGE, C2);
        get_one("r_c1", LUMA_MODS_SPACE_MESSAGE, C1);
        get_one("r_breath", LUMA_MODS_SPACE_MESSAGE, "breath");
    } else if (k == 9) {
        /* of_mod: itself by its caller() number, and numbers that are no mod. */
        LumaModsInfo own = {.size = sizeof(LumaModsInfo)};
        LumaModsInfo found = {.size = sizeof(LumaModsInfo)};
        lt_fact("of_self", mods->of_mod(lt_mod, (uint64_t)(uintptr_t)lt_mod, &own));
        lt_fact("of_self_same", mods->find(lt_mod, "mods-a", &found) == LUMA_OK && own.order == found.order &&
                                    own.id[0] == 'm' && own.state == found.state);
        lt_fact("of_zero", mods->of_mod(lt_mod, 0u, &own));
        lt_fact("of_other_number", mods->of_mod(lt_mod, 0x1234u, &own));
        lt_fact("of_null_out", mods->of_mod(lt_mod, (uint64_t)(uintptr_t)lt_mod, 0));
    }
#elif CASE == 2
    if (k == 8) {
        uint32_t number = 0;
        const int32_t status = ids->of(lt_mod, LUMA_MODS_SPACE_MESSAGE, "mods-a", "breath", &number);
        lt_fact("of_breath", status == LUMA_OK ? (long long)number : (long long)status);
        lt_fact("of_never", ids->of(lt_mod, LUMA_MODS_SPACE_MESSAGE, "mods-a", "never", &number));
        get_one("kb", LUMA_MODS_SPACE_MESSAGE, KB);
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
#if CASE == 1
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0"),
    LUMA_SERVICE(ids, LUMA_MODS_IDS_NAME, "1.0"),
    LUMA_SERVICE(install, LUMA_MODS_INSTALL_NAME, "1.0"),
#elif CASE == 2
    LUMA_SERVICE(ids, LUMA_MODS_IDS_NAME, "1.0"),
#elif CASE == 3
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.9"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
