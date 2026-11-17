/* jl-thread (test 3128): the same (file, line, code) reported from a thread that is not the game thread and from the
   game thread in the same frame. The test calls MR::getRandom(0, 4350): this mod's after-hook reports on the game
   thread, then runs a report on a thread of the harness's own (harness 1.1 other_thread; the game thread waits).
   The mod log keeps one entry, count 2. */
#include "jl.h"
#include <luma_harness.h>

static const void* harness_table;
static volatile int32_t off_game = 1;

static void on_other_thread(void* user) {
    (void)user;
    off_game = jl_core->game_thread();
    jl_report("jl_thread.lumi", 7);
}

static int32_t both(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4350) return 0;
    jl_report("jl_thread.lumi", 7);
    const LumaHarness* harness = (const LumaHarness*)harness_table;
    int32_t made = 0;
    if (harness != 0 && harness->size >= sizeof(LumaHarness)) made = harness->other_thread(on_other_thread, 0);
    jl_fact("made", made);
    jl_fact("off_game", off_game);
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(harness_table, LUMA_HARNESS_NAME, "1.1"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, both),
};

JL_START({})
LUMA_MOD(imports, start, 0);
