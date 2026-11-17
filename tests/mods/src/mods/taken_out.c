/* 2290-state-taken-out (contract mods: LumaModsInfo.state follows a take-out). CASE 1, mt-reader: in start, the state
   of mt-gone ("start_found", "start_state": RUNNING 0); it watches the mods ("watch"), and at mt-gone's STOPPED event
   reads it again ("stopped_found", "stopped_state": TAKEN_OUT 4). CASE 2, mt-gone: starts and does nothing; the run
   takes it out. */
#include "../loader/lt.h"
#include <luma_mods.h>

#ifndef CASE
#error CASE
#endif

#if CASE == 1
static const LumaModsService* mods;

static void read_state(const char* found_name, const char* state_name) {
    LumaModsInfo info;
    info.size = sizeof(info);
    const int32_t found = mods->find(lt_mod, "mt-gone", &info);
    lt_fact(found_name, found);
    lt_fact(state_name, found == LUMA_OK ? (long long)info.state : -1);
}

static void on_stopped(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
    read_state("stopped_found", "stopped_state");
}

static const LumaImport imports[] = {
    LUMA_SERVICE(mods, LUMA_MODS_NAME, "1.0"),
};

LT_START({
    read_state("start_found", "start_state");
    lt_fact("watch", mods->watch_mods(lt_mod, on_stopped, 0));
})
LUMA_MOD(imports, start, 0);
#else
LT_START({})
LUMA_MOD_NO_IMPORTS(start, 0);
#endif
