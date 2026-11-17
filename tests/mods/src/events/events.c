/* The events service (6901): the game's events told to the mods that watch them at the next frame's start.
   -DCASE=1, mod ev-watch: watches STAGE_START, COIN and ACTOR_APPEAR from its start. Facts: "watch_bit0",
   "watch_bit30" (watch with bit 0, with bit 30: -1 each), "watch_stop_unwatched" (kinds 0 before watching: 0),
   "watch" (0). STAGE_START: "stage_name" (1: the name is EggStarGalaxy), "stage_mission" (the value). ACTOR_APPEAR:
   "appear_actor" (1, once: an event with an object), "other_thread" (written for an event raised off the game
   thread: never, as stages load on the game thread). Its after hook on MR::getRandom(0, 6901) notes the frame and calls MR::incCoin(7). COIN: "coin"
   (the value), "coin_later" (frames from the raise to the event: 1), "coin_status" (0), "coin_actor" (0); at the
   first COIN its function calls MR::incCoin(1) itself (code at the frame's start may do anything), which is told at
   the next frame: a second "coin" of 1.
   -DCASE=2, mod ev-stop: watches COIN; at its first, "coin" (the value) and it stops watching (kinds 0): never a
   second "coin".
   -DCASE=3, mod ev-fault: watches COIN; its function writes through a null pointer: the guard takes it out, the
   others go on. */
#include "../loader/lt.h"
#include <luma_events.h>
#include <luma_time.h>

static const LumaEventsService* ev;
static const LumaTimeService* tm;

__attribute__((unused)) static uint64_t frame_now(void) {
    LumaTimeNow now;
    now.size = sizeof(now);
    return tm->now(lt_mod, &now) == LUMA_OK ? now.frame : 0;
}

#if CASE == 1
static const LumaFunction* inc_coin;
static uint64_t raised_frame;
static int32_t coins_told;
static int32_t appear_told;
static int32_t other_told;

static void add_coins(int32_t count) {
    void* args[2] = {0, &count};
    inc_coin->invoke(args, 0);
}

static int32_t same(const char* a, const char* b) {
    while (*a != 0 && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void told(void* user, const LumaEvent* event) {
    (void)user;
    const LumaEventsGame* game = (const LumaEventsGame*)event->data;
    if (event->kind == LUMA_EVENTS_EVENT_STAGE_START) {
        lt_fact("stage_name", same(game->name, "EggStarGalaxy") && game->name_size == 13u);
        lt_fact("stage_mission", game->value);
    } else if (event->kind == LUMA_EVENTS_EVENT_ACTOR_APPEAR) {
        if (game->actor != 0 && appear_told++ == 0) lt_fact("appear_actor", 1);
        if ((game->flags & LUMA_EVENTS_OTHER_THREAD) != 0 && other_told++ == 0) lt_fact("other_thread", 1);
    } else if (event->kind == LUMA_EVENTS_EVENT_COIN) {
        lt_fact("coin", game->value);
        lt_fact("coin_status", event->status);
        lt_fact("coin_actor", (long long)game->actor);
        if (coins_told++ == 0) {
            lt_fact("coin_later", (long long)(frame_now() - raised_frame));
            add_coins(1);
        }
    }
}

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    if (LUMA_ARG(call, 2, int32_t) != 6901) return 0;
    raised_frame = frame_now();
    add_coins(7);
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(ev, LUMA_EVENTS_NAME, "1.0"),
    LUMA_SERVICE(tm, LUMA_TIME_NAME, "1.0"),
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
    LUMA_FUNCTION(inc_coin, "MR::incCoin", "void(int)"),
};

LT_START({
    lt_fact("watch_bit0", ev->watch(lt_mod, 1u, told, 0));
    lt_fact("watch_bit30", ev->watch(lt_mod, 1u << 30, told, 0));
    lt_fact("watch_stop_unwatched", ev->watch(lt_mod, 0, told, 0));
    lt_fact("watch", ev->watch(lt_mod, (1u << LUMA_EVENTS_EVENT_STAGE_START) | (1u << LUMA_EVENTS_EVENT_COIN) |
                                           (1u << LUMA_EVENTS_EVENT_ACTOR_APPEAR),
                               told, 0));
})
LUMA_MOD(imports, start, 0);
#elif CASE == 2
static void told(void* user, const LumaEvent* event) {
    (void)user;
    lt_fact("coin", ((const LumaEventsGame*)event->data)->value);
    lt_fact("stopped", ev->watch(lt_mod, 0, 0, 0));
}

static const LumaImport imports[] = {
    LUMA_SERVICE(ev, LUMA_EVENTS_NAME, "1.0"),
    LUMA_SERVICE(tm, LUMA_TIME_NAME, "1.0"),
};

LT_START({ lt_fact("watch", ev->watch(lt_mod, 1u << LUMA_EVENTS_EVENT_COIN, told, 0)); })
LUMA_MOD(imports, start, 0);
#else
static void told(void* user, const LumaEvent* event) {
    (void)user;
    (void)event;
    lt_fact("entered", 1);
    int32_t* volatile nowhere = 0;
    *nowhere = 1;
    lt_fact("survived", 1);
}

static const LumaImport imports[] = {
    LUMA_SERVICE(ev, LUMA_EVENTS_NAME, "1.0"),
    LUMA_SERVICE(tm, LUMA_TIME_NAME, "1.0"),
};

LT_START({ lt_fact("watch", ev->watch(lt_mod, 1u << LUMA_EVENTS_EVENT_COIN, told, 0)); })
LUMA_MOD(imports, start, 0);
#endif
