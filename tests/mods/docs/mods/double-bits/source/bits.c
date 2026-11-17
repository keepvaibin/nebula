#include <luma.h>

/* Runs before the game adds star bits, and doubles how many. */
static int32_t more_bits(LumaCall* call, void* user) {
    (void)user;
    int* count = (int*)call->args[1];
    *count *= 2;
    return 0;
}

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    core->log(mod, "double-bits: on");
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("GameDataFunction::addStockedStarPiece", "void(int)", LUMA_BEFORE, more_bits),
};

LUMA_MOD(imports, start, 0);
