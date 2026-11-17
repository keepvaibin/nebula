/* The guard's fault in each boundary kind (contract guard.md 1 and 4; F's ask of 2026-11-11 22:10). Built once per
   case with -DWHERE=<1..9> -DFAULT=<1..3> (guard_facts.h): mod id guard-<where>-<fault>, as build_guard_mods.sh names
   it.

   WHERE 1 before, 2 after, 3 wrap, 4 replace on MarioActor::control (every frame of play); 5 start; 6 stop; 7 a time
   service timer's event (frames, 2 after start); 8 a made class's `made` (class GuardShell: a JetTurtle made in place
   of every Kuribo a stage places); 9 that class's override of LiveActor::calcAnim.

   Facts: "started 1" once start is past (not in case 5, whose start faults); "entered 1" then the fault; "survived 1"
   must never appear; "again 1" if a boundary of the mod runs after its fault (it must not: the mod is off). Expected:
   the log's "[mod guard-W-F] GUARD:" line, a GUARD entry in the mod log with the row Guard.Caught, the take-out at the
   next frame (takeout facts: the harness's), and the game going on. Case 5: start refused (Validator.Lib.StartFaulted).
   Case 6: the fault comes when the mod is taken out (the harness's take-out or the session's end). */
#include "guard_facts.h"
#include <luma_time.h>

#ifndef WHERE
#error "WHERE is 1 to 9"
#endif

static int32_t done;

/* Once: the first run faults; any later run of a boundary of the mod is a fault of the guard. */
static void once(void) {
    if (done) {
        fact("again", 1);
        return;
    }
    done = 1;
    fault_here();
}

#if WHERE >= 1 && WHERE <= 4
static int32_t hook(LumaCall* call, void* user) {
    (void)user;
    once();
#if WHERE == 3 || WHERE == 4
    /* Reached only when there was no fault: the guard proceeds for a wrap or replace that faulted before it did. */
    g_core->proceed(call);
#else
    (void)call;
#endif
    return 0;
}
#endif

#if WHERE == 7
static const LumaTimeService* time_service;
static LumaTimeTimer timer;
static void due(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
    once();
}
#endif

#if WHERE == 8 || WHERE == 9
static int32_t shell_class = -1;
static void made(void* object, void* data, void* user) {
    (void)object;
    (void)data;
    (void)user;
#if WHERE == 8
    once();
#endif
}
static int32_t on_calc(LumaCall* call, void* user) {
    (void)user;
#if WHERE == 9
    once();
#endif
    g_core->proceed(call);
    return 0;
}
static const LumaOverride overrides[] = {
    {"LiveActor::calcAnim", "void()", on_calc, 0},
};
/* Designated: builds on luma.h with and without the later fields (F's `gone`), whatever their order. */
static const LumaClassDef shell_def = {
    .size = sizeof(LumaClassDef),
    .name = "GuardShell",
    .base = "JetTurtle",
    .replaces = "Kuribo",
    .made = made,
    .overrides = overrides,
    .override_count = sizeof(overrides) / sizeof(overrides[0]),
};
#endif

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    g_core = core;
    g_mod = mod;
#if WHERE == 5
    once();
#endif
#if WHERE == 7
    time_service = (const LumaTimeService*)core->service(mod, LUMA_TIME_NAME, LUMA_TIME_MAJOR, LUMA_TIME_MINOR);
    if (!time_service || time_service->timer(mod, LUMA_TIME_FRAMES, 2, 0, due, 0, &timer) != LUMA_PENDING) {
        fact("timer", 0);
        return 1;
    }
#endif
#if WHERE == 8 || WHERE == 9
    shell_class = core->define_class(mod, &shell_def);
    fact("defined", shell_class >= 0);
    if (shell_class < 0) {
        return 1;
    }
#endif
    fact("started", 1);
    return 0;
}

static void stop(void) {
#if WHERE == 6
    once();
#else
    fact("stopped", 1);
#endif
}

#if WHERE == 1
static const LumaImport imports[] = {LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_BEFORE, hook)};
#elif WHERE == 2
static const LumaImport imports[] = {LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, hook)};
#elif WHERE == 3
static const LumaImport imports[] = {LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_WRAP, hook)};
#elif WHERE == 4
static const LumaImport imports[] = {LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_REPLACE, hook)};
#endif

#if WHERE >= 1 && WHERE <= 4
LUMA_MOD(imports, start, stop);
#else
LUMA_MOD_NO_IMPORTS(start, stop);
#endif
