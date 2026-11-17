/* Harness test mod "spinwatch": counts the player's ground spins (MarioActor::trySpinPunch returning true) and
   writes the count as the fact "spins" at start (0) and after every spin. Test 4: the case that misled the first
   Gold Flower runs (a spin that never happened because the save had no spin permission). */
#include <luma.h>

static const LumaCore* game;
static const LumaMod* self;
static int32_t spins;

static void put_fact(int32_t value) {
    char line[32] = "fact spins ";
    char digits[12];
    int32_t count = 0;
    char* at = line + 11;
    uint32_t left = (uint32_t)value;
    do {
        digits[count++] = (char)('0' + left % 10u);
        left /= 10u;
    } while (left != 0u);
    while (count > 0) {
        *at++ = digits[--count];
    }
    *at = 0;
    game->log(self, line);
}

static int32_t after_spin(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_RESULT(call, uint8_t) != 0) {
        spins++;
        put_fact(spins);
    }
    return 0;
}

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    game = core;
    self = mod;
    spins = 0;
    put_fact(0);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_HOOK_ON("MarioActor::trySpinPunch", "bool()", LUMA_AFTER, after_spin),
};

LUMA_MOD(imports, start, 0);
