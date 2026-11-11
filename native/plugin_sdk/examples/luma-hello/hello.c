/* The smallest LumaCore mod that uses each part: a function called by name, a field read by name, and the three
   kinds of hook. Star bits count double, and the log says where the player stands once a second. */
#include <luma.h>

static const LumaCore* game;
static const LumaMod* self;

static const LumaFunction* get_player_pos;   /* Vec3f* MR::getPlayerPos() */
static const LumaFunction* get_star_bits;    /* s32 MR::getStarPieceNum() */
static const LumaFunction* add_star_bits;    /* void MR::addStarPiece(int) */
static int32_t actor_position;               /* LiveActor.mPosition */

static uint32_t frames;

static void put_number(char** at, int32_t value) {
    char digits[12];
    int32_t count = 0;
    uint32_t left = value < 0 ? (uint32_t)(-(int64_t)value) : (uint32_t)value;
    if (value < 0) {
        *(*at)++ = '-';
    }
    do {
        digits[count++] = (char)('0' + left % 10u);
        left /= 10u;
    } while (left != 0u);
    while (count > 0) {
        *(*at)++ = digits[--count];
    }
}

static void say(const char* text, int32_t a, int32_t b, int32_t c) {
    char line[160];
    char* at = line;
    while (*text != 0) {
        *at++ = *text++;
    }
    *at++ = ' ';
    put_number(&at, a);
    *at++ = ' ';
    put_number(&at, b);
    *at++ = ' ';
    put_number(&at, c);
    *at = 0;
    game->log(self, line);
}

static int32_t star_bits(void) {
    int32_t count = 0;
    void* args[] = {0};
    get_star_bits->invoke(args, &count);
    return count;
}

/* Before the stage starts. */
static int32_t stage_start(LumaCall* call, void* user) {
    (void)call;
    (void)user;
    frames = 0;
    say("stage start, star bits", star_bits(), 0, 0);
    return 0;
}

/* Around MR::addStarPiece: the amount doubled, then the game's own code. */
static int32_t double_star_bits(LumaCall* call, void* user) {
    (void)user;
    LUMA_ARG(call, 1, int32_t) *= 2;
    game->proceed(call);
    return 0;
}

/* After the player's update, every frame. */
static int32_t player_frame(LumaCall* call, void* user) {
    (void)user;
    frames++;
    if (frames % 60u == 0u) {
        const float* by_field = (const float*)((const char*)LUMA_THIS(call, void) + actor_position);
        const float* by_call = 0;
        void* args[] = {0};
        get_player_pos->invoke(args, &by_call);
        say("player at", (int32_t)by_field[0], (int32_t)by_field[1], (int32_t)by_field[2]);
        say("the same by call", (int32_t)by_call[0], (int32_t)by_call[1], (int32_t)by_call[2]);
    }
    if (frames == 120u) {
        const int32_t was = star_bits();
        int32_t amount = 5;
        void* args[] = {0, &amount};
        add_star_bits->invoke(args, 0);
        say("five star bits asked for; before, after, hooked", was, star_bits(), *add_star_bits->hooked);
    }
    return 0;
}

static int32_t start(const LumaCore* core, const LumaMod* mod) {
    game = core;
    self = mod;
    game->log(self, "started");
    return 0;
}

static const LumaImport imports[] = {
    LUMA_FUNCTION(get_player_pos, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_FUNCTION(get_star_bits, "MR::getStarPieceNum", "s32()"),
    LUMA_FUNCTION(add_star_bits, "MR::addStarPiece", "void(int)"),
    LUMA_FIELD(actor_position, "LiveActor.mPosition", "TVec3f"),
    LUMA_HOOK_ON("GameScene::start", "void()", LUMA_BEFORE, stage_start),
    LUMA_HOOK_ON("MR::addStarPiece", "void(int)", LUMA_WRAP, double_star_bits),
    LUMA_HOOK_ON("MarioActor::control", "void()", LUMA_AFTER, player_frame),
};

LUMA_MOD(imports, start, 0);
