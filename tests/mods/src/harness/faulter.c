/* Harness test mod "faulter": writes through a null pointer inside an after-hook on the player's update, on the
   first call. Test 5: without "faults" the test fails on the guard event (after 1.6), or is "crashed" before it. */
#include <luma.h>

static int32_t fired;

static int32_t after_control(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    if (!fired) {
        fired = 1;
        *(volatile int32_t*)(uintptr_t)0 = 1;
    }
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, after_control),
};

LUMA_MOD(imports, 0, 0);
