/* lt-memfront, lt-memback, lt-memkeep: a mod that writes outside a block of its memory (M-003). Each block has guard
   bytes on either side that the kernel checks when the block is freed and when the mod is taken out; a write there is
   said once (MEMORY in the log, Memory.WrittenBefore or Memory.WrittenAfter in the mod log) and the mod is turned off,
   as after a fault the guard caught. The game goes on, and what the kernel knows of the blocks is not in the mod's
   reach, so every block is freed whole.
   LT_MEM_CASE 1: one byte in front of a block (an index of -1), then free. 2: one byte past the end, then free.
   3: one byte in front, kept: found when the harness takes the mod out. */
#include "lt.h"

LT_START({
    unsigned char* a = (unsigned char*)core->alloc(mod, 32, 16);
    unsigned char* b = (unsigned char*)core->alloc(mod, 24, 8);
    lt_fact("got", a != 0 && b != 0);
    if (a != 0 && b != 0) {
        if (LT_MEM_CASE == 2) {
            a[32] = 0x55;
        } else {
            a[-1] = 0x55;
        }
        if (LT_MEM_CASE != 3) {
            core->free(mod, a);
        }
        core->free(mod, b);
        lt_fact("went_on", 1);
    }
})
LUMA_MOD_NO_IMPORTS(start, 0);
