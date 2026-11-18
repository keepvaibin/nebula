/* 2255-bad-pointers (M-029; contract mods): mods-badptr gives mods, mods.ids and mods.install every pointer they read,
   write or call wrong (lt_bad.h) at k 1: each LUMA_E_ARGUMENT, reported, nothing written; then a good find of its own
   id ("find_good", "find_order" its order is 1 or more). Run steps call MR::getRandom(0, 4250 + k); the hook gives 1. */
#include "../loader/lt.h"
#include "../loader/lt_bad.h"
#include <luma_mods.h>

static const LumaModsService* mods;
static const LumaModsIdsService* ids;
static const LumaModsInstallService* install;

static void on_event(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4250;
    if (k != 1) return 0;
    LumaModsInfo info = {.size = sizeof(LumaModsInfo)};
    LumaHandle none = 0;

    uint32_t number = 7u;
    char buf[8];
    lt_fact("at_out_unmapped", mods->at(lt_mod, 0, (LumaModsInfo*)LT_UNMAPPED));
    lt_fact("at_out_read_only", mods->at(lt_mod, 0, (LumaModsInfo*)LT_READ_ONLY));
    lt_fact("find_id_unmapped", mods->find(lt_mod, (const char*)LT_UNMAPPED, &info));
    lt_fact("of_mod_out_read_only", mods->of_mod(lt_mod, (uint64_t)(uintptr_t)lt_mod, (LumaModsInfo*)LT_READ_ONLY));
    lt_fact("option_key_unmapped", mods->option(lt_mod, (const char*)LT_UNMAPPED, buf, sizeof(buf)));
    lt_fact("watch_options_fn_data", mods->watch_options(lt_mod, (LumaEventFn)(uintptr_t)LT_DATA_FN, 0));
    lt_fact("watch_mods_fn_game", mods->watch_mods(lt_mod, (LumaEventFn)(uintptr_t)mods->count, 0));
    lt_fact("kit_names_length_read_only", mods->kit_names(lt_mod, 0, "x", 0, 0, (uint64_t*)LT_READ_ONLY));
    lt_fact("needing_count_read_only", mods->needing(lt_mod, 0, 0, (uint32_t*)LT_READ_ONLY));
    lt_fact("ids_name_unmapped", ids->get(lt_mod, LUMA_MODS_SPACE_MESSAGE, (const char*)LT_UNMAPPED, &number));
    lt_fact("ids_out_read_only", ids->get(lt_mod, LUMA_MODS_SPACE_MESSAGE, "breath", (uint32_t*)LT_READ_ONLY));
    lt_fact("install_path_unmapped", install->request(lt_mod, (const char*)LT_UNMAPPED, on_event, 0, &none));
    lt_fact("install_fn_data", install->request(lt_mod, "packs/x.luma", (LumaEventFn)(uintptr_t)LT_DATA_FN, 0, &none));
    lt_fact("install_out_read_only", install->request(lt_mod, "packs/x.luma", on_event, 0, (LumaHandle*)LT_READ_ONLY));
    lt_fact("none_written", none == 0 && number == 7u && info.order == 0);
    lt_fact("find_good", mods->find(lt_mod, "mods-badptr", &info));
    lt_fact("find_order", info.order >= 1);
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0"),
    LUMA_SERVICE(ids, LUMA_MODS_IDS_NAME, "1.0"),
    LUMA_SERVICE(install, LUMA_MODS_INSTALL_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({})
LUMA_MOD(imports, start, 0);
