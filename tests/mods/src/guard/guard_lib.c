/* A fault in a library mod's code, reached by another mod's call (contract guard.md 2): caught at the calling mod's
   boundary like a fault in its own code; the calling mod is off and taken out at the next frame, the game goes on.
   -DCASE=1, mod guard-lib: exports "gl.fault" (writes through a null pointer) and "gl.fine".
   -DCASE=2, mod guard-lib-user: needs guard-lib; an after hook on MR::getRandom(0, 1643) calls gl.fine (fact
   "fine" 7), then gl.fault: facts "entered" 1 before it, "survived" never. A second call (MR::getRandom(0, 1644))
   finds the mod taken out: no fact "again".
   -DCASE=4, mod guard-lib-user2: as guard-lib-user, at MR::getRandom(0, 1648) (guard 1648: a second caller).
   -DCASE=3, mod guard-lib-other: needs guard-lib; an after hook on MR::getRandom(0, 1644), after guard-lib-user is
   out, calls gl.fine: fact "fine" 7, the library still serves its other users. */
#include "../loader/lt.h"

#if CASE == 1
static int32_t fine(int32_t a) {
    return a + 6;
}

static int32_t fault(int32_t a) {
    int32_t* volatile nowhere = 0;
    *nowhere = a;
    return a;
}

static const LumaExport exports[] = {
    LUMA_EXPORT_FUNCTION("gl.fault", "lumi1:0000000000001643", fault),
    LUMA_EXPORT_FUNCTION("gl.fine", "lumi1:0000000000001643", fine),
};

static const LumaFunction* random_fn;  /* bound, never called: a mod has at least one import */

static const LumaImport imports[] = {
    LUMA_FUNCTION(random_fn, "MR::getRandom", "s32(s32, s32)"),
};

LT_START((void)0)
LUMA_LIBRARY_MOD(imports, start, 0, exports, 0);
#elif CASE == 3
static const void* fine_fn;

static int32_t after(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) == 1644) lt_fact("fine", ((int32_t (*)(int32_t))fine_fn)(1));
    return 0;
}

static const LumaImport imports[] = {
    LUMA_MOD_FUNCTION(fine_fn, "guard-lib/gl.fine", "lumi1:0000000000001643"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
#else
/* CASE 2 (guard-lib-user) acts at 1643; CASE 4 (guard-lib-user2), the second caller of 1648, at 1648. */
#if CASE == 4
#define MINE 1648
#else
#define MINE 1643
#endif
static const void* fault_fn;
static const void* fine_fn;

static int32_t after(LumaCall* call, void* user) {
    (void)user;
    const int32_t which = LUMA_ARG(call, 2, int32_t);
    if (which == 1644 && MINE == 1643) {
        lt_fact("again", 1);
        return 0;
    }
    if (which != MINE) return 0;
    lt_fact("fine", ((int32_t (*)(int32_t))fine_fn)(1));
    lt_fact("entered", 1);
    lt_fact("survived", ((int32_t (*)(int32_t))fault_fn)(1));
    return 0;
}

static const LumaImport imports[] = {
    LUMA_MOD_FUNCTION(fault_fn, "guard-lib/gl.fault", "lumi1:0000000000001643"),
    LUMA_MOD_FUNCTION(fine_fn, "guard-lib/gl.fine", "lumi1:0000000000001643"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, after),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
#endif
