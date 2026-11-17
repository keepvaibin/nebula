/* needs-call (6801): a mod calls game functions that read scene objects with no test that they are there (M2, the
   needs rows), first at boot frame 1 (no scene, no player, no camera) and again in play. At boot each call is not
   made (Call.NotNow in the mod's log) and gives its result type's empty value; in play each is made. The game never
   faults: before M2 the first call ended it (guard rule 3, a fault in the game's code).
   Facts: "boot_called" (how many calls were made at frame 1, one after each), "boot_pos" (1: getPlayerPos gave a
   pointer), "boot_dead" (isPlayerDead's result), "boot_survived"; run step MR::getRandom(0, 6801) in play:
   "play_pos" (1: a pointer), "play_survived". */
#include "../loader/lt.h"
#include <luma_time.h>

static const LumaTimeService* tm;
static const LumaFunction* validate_sensor;
static const LumaFunction* inc_life;
static const LumaFunction* inc_coin;
static const LumaFunction* player_pos;
static const LumaFunction* player_dead;
static const LumaFunction* cam_pos;
static int32_t frames;

/* key = when + what, ended. */
static const char* key_of(char* key, const char* when, const char* what) {
    *lt_put(lt_put(key, when), what) = 0;
    return key;
}

/* Calls each function once; facts under `when`: "<when>_called" after each, then the results. */
static void call_all(const char* when) {
    char key[32];
    key_of(key, when, "_called");
    int32_t called = 0;

    void* none[1] = {0};
    validate_sensor->invoke(none, 0);
    lt_fact(key, ++called);

    uint32_t one = 1;
    void* life[2] = {0, &one};
    inc_life->invoke(life, 0);
    lt_fact(key, ++called);

    int32_t coins = 1;
    void* coin[2] = {0, &coins};
    inc_coin->invoke(coin, 0);
    lt_fact(key, ++called);

    void* pos = (void*)1;
    player_pos->invoke(none, &pos);
    lt_fact(key, ++called);

    _Bool dead = 1;
    player_dead->invoke(none, &dead);
    lt_fact(key, ++called);

    float cam[3] = {1.0f, 1.0f, 1.0f};
    cam_pos->invoke(none, cam);
    lt_fact(key, ++called);

    lt_fact(key_of(key, when, "_pos"), pos != 0);
    lt_fact(key_of(key, when, "_dead"), dead);
    lt_fact(key_of(key, when, "_cam_zero"), cam[0] == 0.0f && cam[1] == 0.0f && cam[2] == 0.0f);
    lt_fact(key_of(key, when, "_survived"), 1);
}

static void tick(void* user, const LumaTimeNow* now) {
    (void)user;
    (void)now;
    if (++frames == 1) {
        call_all("boot");
    }
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 6801) return 0;
    call_all("play");
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(tm, LUMA_TIME_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
    LUMA_FUNCTION(validate_sensor, "MarioAccess::validateSensor", "void()"),
    LUMA_FUNCTION(inc_life, "MR::incPlayerLife", "void(u32)"),
    LUMA_FUNCTION(inc_coin, "MR::incCoin", "void(int)"),
    LUMA_FUNCTION(player_pos, "MR::getPlayerPos", "Vec3f *()"),
    LUMA_FUNCTION(player_dead, "MR::isPlayerDead", "bool()"),
    LUMA_FUNCTION(cam_pos, "MR::getCamPos", "const Vec3f()"),
};

LT_START({ lt_fact("on_frame", tm->on_frame(lt_mod, tick, 0)); })
LUMA_MOD(imports, start, 0);
