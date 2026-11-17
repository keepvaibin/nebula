/* tt-bits: a bit-field the game reads (types 1.2, TY-4; abi addition 5). The player's Mario holds
   Mario::MovementStates (unsigned : 1 fields) at Mario.mMovementStates; Mario::isDigitalJump() const returns its
   digitalJump bit. On MR::getRandom(0, 4321), in a stage: the mod finds Mario through MR::getPlayerDemoActor and
   MarioActor.mMario, sets digitalJump with LUMA_BITS_SET and calls isDigitalJump (set1), clears it and calls again
   (set0), then puts the bit back. Facts: set1 1, set0 0, and that every other bit of the eight bytes is as it was
   (others_kept), digitalJump's position in bits (13) and width-checked type text. */
#include "../loader/lt.h"

static const LumaFunction* player;
static const LumaFunction* is_digital_jump;
static int32_t mario_at, states_at, digital_jump;

static int32_t asked(void* mario) {
    void* args[1] = {mario};
    unsigned char result = 0xEE;
    is_digital_jump->invoke(args, &result);
    return result;
}

static int32_t marks(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 4321) return 0;
    void* got = 0;
    void* none[1] = {0};
    player->invoke(none, &got);
    lt_fact("player", got != 0);
    if (got == 0) return 0;
    unsigned char* mario = *(unsigned char**)(void*)((unsigned char*)got + mario_at);
    lt_fact("mario", mario != 0);
    if (mario == 0) return 0;
    unsigned char* states = mario + states_at;
    unsigned char before[8];
    for (int i = 0; i < 8; i++) before[i] = ((volatile unsigned char*)states)[i];
    const uint32_t was = LUMA_BITS_GET(states, digital_jump, 1);
    LUMA_BITS_SET(states, digital_jump, 1, 1);
    lt_fact("set1", asked(mario));
    LUMA_BITS_SET(states, digital_jump, 1, 0);
    lt_fact("set0", asked(mario));
    LUMA_BITS_SET(states, digital_jump, 1, was);
    int kept = 1;
    for (int i = 0; i < 8; i++) kept &= ((volatile unsigned char*)states)[i] == before[i];
    lt_fact("others_kept", kept);
    lt_fact("position", digital_jump);
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(player, "MR::getPlayerDemoActor", "LiveActor *()"),
    LUMA_FUNCTION(is_digital_jump, "Mario::isDigitalJump", "bool() const"),
    LUMA_FIELD(mario_at, "MarioActor.mMario", "Mario *"),
    LUMA_FIELD(states_at, "Mario.mMovementStates", "Mario::MovementStates"),
    LUMA_FIELD(digital_jump, "Mario::MovementStates.digitalJump", "unsigned int : 1"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, marks),
};

LT_START((void)0)
LUMA_MOD(imports, start, 0);
