/* SPDX-License-Identifier: GPL-3.0-only */
/* The need walk (docs/modding/concepts/when-a-mod-fails.md, "A fault in another mod's code"): which mod the guard
   turns off for a fault it caught. The kernel runs it at every caught fault (LumaGuard.cpp), the host's unit test on
   chains of its own making (luma_guard_walk_tests.cpp).

   mods[0..count): the mods whose code the fault's call chain passed through, innermost first: mods[0] the mod whose
   code faulted, each next one the mod whose code called the one before it, the boundary's mod last. `gap`: the index
   after which the frames were not followed one by one (frames dropped or not readable), so the step from mods[gap] to
   mods[gap + 1] asks whether the caller needs it directly or through the mods it needs; `count` or more when there is
   none. needs(a, b): mod a needs mod b (a calls b's functions: its kind 6 imports). needs_through(a, b): a needs b,
   directly or through mods it needs. A null mod (code the kernel cannot name) needs nothing.

   Start at the mod whose code faulted; while the mod that called it needs it, move to that caller. The answer is the
   index of the mod the walk ended on. A library a mod called and that faulted: the caller. A mod's code a library
   called back (an `on_frame` it was given) that faulted: that mod, since the library does not need it. */
#ifndef LUMA_GUARD_WALK_H
#define LUMA_GUARD_WALK_H

#ifdef __cplusplus
template <class Mod, class Needs, class NeedsThrough>
unsigned luma_guard_walk(const Mod* mods, unsigned count, unsigned gap, Needs needs, NeedsThrough needs_through) {
    unsigned at = 0;
    for (unsigned i = 0; i + 1 < count; ++i) {
        const Mod caller = mods[i + 1];
        if (caller == mods[at]) {
            at = i + 1;  // the same mod's code again (it called itself through code the chain does not name)
            continue;
        }
        if (caller == Mod() || !(i == gap ? needs_through(caller, mods[at]) : needs(caller, mods[at]))) {
            break;
        }
        at = i + 1;
    }
    return at;
}
#endif

#endif
