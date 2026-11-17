/* lt-thread: a hook runs on the thread that calls its function, and game_thread() tells which (loader contract 4,
   Z-3). An after on CameraLocalUtil::getNormalizedOr counts calls on and off the game thread; off it, hook and unhook
   must refuse with -3 and change nothing. MR::getRandom(0, 4248) on the game thread calls the function once itself,
   then once on a thread of the harness's own (harness 1.1, other_thread: the game thread waits until it ends), and
   gives the facts. A harness run ticks the sound on the game thread, so the game itself calls nothing off it. */
#include "lt.h"
#include <luma_harness.h>

typedef struct {
    float x, y, z;
} V3;

static const LumaFunction* random2;
static const LumaFunction* normalized;
static const void* harness_table;
static volatile int32_t on_count, off_count, off_hook = 1, off_unhook = 1, thread_game = 1;

static int32_t extra(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    return 0;
}

static int32_t counted(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (lt_core->game_thread()) {
        __atomic_add_fetch(&on_count, 1, __ATOMIC_RELAXED);
    } else if (__atomic_add_fetch(&off_count, 1, __ATOMIC_RELAXED) == 1) {
        off_hook = lt_core->hook(lt_mod, random2, LUMA_BEFORE, extra, 0);
        off_unhook = lt_core->unhook(lt_mod, random2, LUMA_BEFORE, extra, 0);
    }
    return 0;
}

/* getNormalizedOr((3, 0, 0), (0, 1, 0)); a reference parameter's argument is the vector's address (luma.h). */
static void call_normalized(void) {
    V3 v = {3.0f, 0.0f, 0.0f};
    V3 fallback = {0.0f, 1.0f, 0.0f};
    V3 out;
    void* args[3] = {0, (void*)&v, (void*)&fallback};
    normalized->invoke(args, &out);
}

static void on_other_thread(void* user) {
    (void)user;
    thread_game = lt_core->game_thread();
    call_normalized();
}

static int32_t report(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4248) return 0;
    const LumaHarness* harness = (const LumaHarness*)harness_table;
    call_normalized();
    int32_t made = 0;
    if (harness != 0 && harness->size >= sizeof(LumaHarness)) made = harness->other_thread(on_other_thread, 0);
    lt_fact("here", lt_core->game_thread());
    lt_fact("made", made);
    lt_fact("thread_game", thread_game);
    lt_fact("on", __atomic_load_n(&on_count, __ATOMIC_RELAXED));
    lt_fact("off", __atomic_load_n(&off_count, __ATOMIC_RELAXED));
    lt_fact("off_hook", off_hook);
    lt_fact("off_unhook", off_unhook);
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(random2, "MR::getRandom", "s32(s32, s32)"),
    LUMA_FUNCTION(normalized, "CameraLocalUtil::getNormalizedOr", "Vec3f(const Vec3f &, const Vec3f &)"),
    LUMA_SERVICE(harness_table, LUMA_HARNESS_NAME, "1.1"),
    LUMA_HOOK_ON("CameraLocalUtil::getNormalizedOr", "Vec3f(const Vec3f &, const Vec3f &)", LUMA_AFTER, counted),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, report),
};

LT_START(lt_fact("start_thread", core->game_thread()))
LUMA_MOD(imports, start, 0);
