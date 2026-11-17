/* 2254-watch-stopped (contract mods: watch_mods, LUMA_MODS_EVENT_STOPPED). CASE 1, mw-watcher: watch_mods in start
   ("watch"); each STOPPED event writes "stopped_kind" (the event's kind), "stopped_status", "stopped_named" (1 when its
   data is the stopped mod's id "mw-stops" with a NUL and data_size its length) and "stopped_count". CASE 2, mw-stops:
   starts and does nothing; the run takes it out. */
#include "../loader/lt.h"
#include <luma_mods.h>

#ifndef CASE
#error CASE
#endif

#if CASE == 1
static const LumaModsService* mods;
static int32_t count;

static void on_stopped(void* user, const LumaEvent* event) {
    (void)user;
    static const char kId[] = "mw-stops";
    const char* data = (const char*)event->data;
    int named = data != 0 && event->data_size == sizeof(kId) - 1u;
    for (uint32_t i = 0; named && i < sizeof(kId); i++) {
        named = data[i] == kId[i];
    }
    count++;
    lt_fact("stopped_kind", (long long)event->kind);
    lt_fact("stopped_status", event->status);
    lt_fact("stopped_named", named);
    lt_fact("stopped_count", count);
}

static const LumaImport imports[] = {
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0"),
};

LT_START(lt_fact("watch", mods->watch_mods(lt_mod, on_stopped, 0)))
LUMA_MOD(imports, start, 0);
#else
LT_START({})
LUMA_MOD_NO_IMPORTS(start, 0);
#endif
