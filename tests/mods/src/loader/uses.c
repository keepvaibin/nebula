/* Mods that import lt-lib's functions (loader D3-3, kind 6). Each manifest needs lt-lib.
   lt-uses       (-DCASE=1): binds lt-lib/lt.add, calls it at start (fact sum 5); a before on MR::getRandom(0, 4253)
                             appends digit 1. Also lt.ready: fact lib_ready, 1 only if lt-lib's start ran first.
   lt-uses-fail  (-DCASE=2): binds them and lt.remember, gives lt-lib its own LumaMod, then its start refuses: lt-lib
                             is told dependent_gone, naming this mod.
   lt-uses-none  (-DCASE=3): lt-lib/lt.nothing: refused, L505 Name.ModFunction.
   lt-uses-sig   (-DCASE=4): lt-lib/lt.add with another signature: refused, L505 Name.FunctionSig.
   lt-uses-opt   (-DCASE=5): lt-lib/lt.nothing, optional: on, the slot null. */
#include "lt.h"

#ifndef CASE
#error CASE
#endif

typedef int32_t (*Add)(int32_t, int32_t);
typedef int32_t (*Ready)(void);
typedef void (*Remember)(const LumaMod*);
static const void* fn;
static const void* ready_fn;
#if CASE == 2
static const void* remember_fn;
#endif

static int32_t before(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == 4253) LUMA_ARG(call, 1, int32_t) = LUMA_ARG(call, 1, int32_t) * 10 + 1;
    return 0;
}

#if CASE == 1 || CASE == 2
#define ROW LUMA_MOD_FUNCTION(fn, "lt-lib/lt.add", "lumi1:00000000000000a1")
#elif CASE == 3
#define ROW LUMA_MOD_FUNCTION(fn, "lt-lib/lt.nothing", "lumi1:00000000000000a1")
#elif CASE == 4
#define ROW LUMA_MOD_FUNCTION(fn, "lt-lib/lt.add", "lumi1:00000000000000ff")
#else
#define ROW {LUMA_IMPORT_MOD_FUNCTION, LUMA_OPTIONAL, "lt-lib/lt.nothing", "lumi1:00000000000000a1", &fn, 0, 0, 0}
#endif

static const LumaImport imports[] = {
    ROW,
#if CASE == 1 || CASE == 2
    LUMA_MOD_FUNCTION(ready_fn, "lt-lib/lt.ready", "lumi1:00000000000000a3"),
#endif
#if CASE == 2
    LUMA_MOD_FUNCTION(remember_fn, "lt-lib/lt.remember", "lumi1:00000000000000a4"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_BEFORE, before),
};

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    lt_core = core;
    lt_mod = mod;
    lt_fact("slot", fn != 0);
    if (fn != 0) lt_fact("sum", ((Add)fn)(2, 3));
    if (ready_fn != 0) lt_fact("lib_ready", ((Ready)ready_fn)());
#if CASE == 2
    ((Remember)remember_fn)(mod);
    return 7;
#else
    lt_fact("started", 1);
    return 0;
#endif
}

LUMA_MOD(imports, start, 0);
