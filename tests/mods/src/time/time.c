/* H's 2.2 conformance mods (services-time 12), against the module's time service 1.0 (native/include/luma_time.h).
   One source, one case each (-DCASE=n):
   tm-basic       (1)  2201: nothing set; the frame watch (below) only.
   tm-r<rate>     (2)  2202, 2203, 2204: set_rate(RATE) at k 1 (-DRATE; ids tm-r2000, tm-r500, tm-r4000, tm-r1001a,
                        tm-r1001b, tm-r999).
   tm-hold        (3)  2205: hold at k 1, release at k 2.
   tm-step        (4)  2206: hold at k 1; step(3) at k 3, the step count kept; at k 4 the steps since; release at k 2.
   tm-nothold     (5)  2207: step(1) twice without holding at k 3.
   tm-hold-a/b    (6)  2208: hold at k 1; at k 3 the one built with -DSTEPPER steps; release at k 2.
   tm-args        (7)  2209: set_rate 0, 16001, 1, 16000, 1000; step 0, 17 (not holding); release (not holding).
   tm-tframes     (8)  2210: a FRAMES timer after 10, once, at k 1: its delay in frames and its value.
   tm-tsteps      (9)  2210: set_rate 4000 at k 1; a STEPS timer after 1 every 1 at k 2: each event's value.
   tm-theld       (10) 2211: hold at k 1; a STEPS timer after 1 at k 2; events counted at k 4 (held) and k 5;
                        release at k 3.
   tm-cancel      (11) 2212: a FRAMES timer after 20 at k 1; cancel at k 2, twice; events counted at k 5.
   tm-limit       (12) 2213: 65 FRAMES timers far off at k 1: how many were made, the 65th's status.
   tm-order-a/b   (13) 2214: k 1 arms; the next frame each mod's on_frame holds and then steps; release at k 2. The
                        higher mod in the list steps (0), the lower is told LUMA_E_BUSY (-14).
   tm-clocks      (14) 2216: monotonic_ns and wall from the harness's other thread and the game thread at k 1.
   tm-struct      (15) 2217: now with a LumaTimeNow of 8 bytes, wall with a LumaTimeWall of 8 bytes.
   Run steps call MR::getRandom(0, 4220 + k) and the hook gives 1. k 0 starts the frame watch, k 6 ends it: each frame
   between, the on_frame handler writes the facts df (frames since the last), ds (steps since the last), ds2 (steps
   in the last two frames), steps, state, rate, holders, sound, calls (on_step calls since the last) and sif (the
   sum of those calls' step_in_frame). */
#include "../loader/lt.h"
#include <luma_harness.h>
#include <luma_time.h>

#ifndef CASE
#error CASE
#endif
#if CASE == 2 && !defined(RATE)
#error RATE
#endif

static const LumaTimeService* tm;
__attribute__((unused)) static const void* harness_table;

/* The frame watch. */
static int32_t watching;
static int32_t seen_frames;
static uint64_t last_frame, last_step, step_before_last;
static int32_t step_calls, step_place_sum;

static void on_step(void* user, const LumaTimeNow* now) {
    (void)user;
    step_calls++;
    step_place_sum += (int32_t)now->step_in_frame;
}

/* 2214: armed at k 1, done on the next on_frame. */
__attribute__((unused)) static int32_t order_armed;

static void on_frame(void* user, const LumaTimeNow* now) {
    (void)user;
#if CASE == 13
    if (order_armed) {
        /* Hold, then step: step answers BUSY once another mod holds too, so the first handler called gets 0 and the
           second LUMA_E_BUSY (`now` holds still for the frame, J22-4; step's check reads the holders live). */
        order_armed = 0;
        lt_fact("hold", tm->hold(lt_mod));
        lt_fact("step", tm->step(lt_mod, 1));
    }
#endif
    if (watching) {
        if (seen_frames >= 1) {
            lt_fact("df", (long long)(now->frame - last_frame));
            lt_fact("ds", (long long)(now->step - last_step));
            lt_fact("calls", step_calls);
            lt_fact("sif", step_place_sum);
        }
        if (seen_frames >= 2) {
            lt_fact("ds2", (long long)(now->step - step_before_last));
        }
        lt_fact("steps", now->steps);
        lt_fact("state", now->state);
        lt_fact("rate", now->rate);
        lt_fact("holders", now->holders);
        lt_fact("sound", now->sound_rate);
        seen_frames++;
    }
    step_before_last = last_step;
    last_frame = now->frame;
    last_step = now->step;
    step_calls = 0;
    step_place_sum = 0;
}

/* Timers' events: how many, the last value, and (2210) the frame of the first. */
static int32_t events;
static int64_t first_event_frame = -1;

__attribute__((unused)) static void on_timer(void* user, const LumaEvent* event) {
    (void)user;
    events++;
    if (event->kind != LUMA_TIME_EVENT_TIMER || event->status != LUMA_OK) {
        lt_fact("bad_event", 1);
    }
    lt_fact("tv", (long long)event->value);
    if (first_event_frame < 0) {
        LumaTimeNow now = {.size = sizeof(LumaTimeNow)};
        tm->now(lt_mod, &now);
        first_event_frame = (int64_t)now.frame;
    }
}

__attribute__((unused)) static uint64_t now_step(void) {
    LumaTimeNow now = {.size = sizeof(LumaTimeNow)};
    tm->now(lt_mod, &now);
    return now.step;
}

__attribute__((unused)) static uint64_t now_frame(void) {
    LumaTimeNow now = {.size = sizeof(LumaTimeNow)};
    tm->now(lt_mod, &now);
    return now.frame;
}

#if CASE == 14
static int64_t off_m1 = -100, off_m2 = -100;
static int32_t off_wall = -100;
static LumaTimeWall off_w;
static void off_thread(void* user) {
    (void)user;
    off_m1 = tm->monotonic_ns(lt_mod);
    off_m2 = tm->monotonic_ns(lt_mod);
    off_w.size = sizeof(LumaTimeWall);
    off_wall = tm->wall(lt_mod, &off_w);
}
#endif

__attribute__((unused)) static LumaHandle handle;
__attribute__((unused)) static uint64_t mark;

static int32_t trigger(LumaCall* call, void* user) {
    (void)user;
    const int32_t k = LUMA_ARG(call, 2, int32_t) - 4220;
    if (k < 0 || k > 9) return 0;
    if (k == 0) watching = 1;
    if (k == 6) watching = 0;
#if CASE == 2
    if (k == 1) lt_fact("set", tm->set_rate(lt_mod, RATE));
#elif CASE == 3
    if (k == 1) lt_fact("hold", tm->hold(lt_mod));
    if (k == 2) lt_fact("release", tm->release(lt_mod));
#elif CASE == 4
    if (k == 1) lt_fact("hold", tm->hold(lt_mod));
    if (k == 3) {
        mark = now_step();
        lt_fact("step", tm->step(lt_mod, 3));
    }
    if (k == 4) lt_fact("after_step", (long long)(now_step() - mark));
    if (k == 2) lt_fact("release", tm->release(lt_mod));
#elif CASE == 5
    if (k == 3) {
        lt_fact("step_1", tm->step(lt_mod, 1));
        lt_fact("step_2", tm->step(lt_mod, 1));
    }
#elif CASE == 6
    if (k == 1) lt_fact("hold", tm->hold(lt_mod));
#ifdef STEPPER
    if (k == 3) lt_fact("step", tm->step(lt_mod, 1));
#endif
    if (k == 2) lt_fact("release", tm->release(lt_mod));
#elif CASE == 7
    if (k == 1) {
        lt_fact("rate_0", tm->set_rate(lt_mod, 0));
        lt_fact("rate_16001", tm->set_rate(lt_mod, 16001));
        lt_fact("rate_1", tm->set_rate(lt_mod, 1));
        lt_fact("rate_16000", tm->set_rate(lt_mod, 16000));
        lt_fact("rate_1000", tm->set_rate(lt_mod, 1000));
        lt_fact("step_0", tm->step(lt_mod, 0));
        lt_fact("step_17", tm->step(lt_mod, 17));
        lt_fact("release", tm->release(lt_mod));
    }
#elif CASE == 8
    if (k == 1) {
        mark = now_frame();
        lt_fact("timer", tm->timer(lt_mod, LUMA_TIME_FRAMES, 10, 0, on_timer, 0, &handle));
    }
    if (k == 4) {
        lt_fact("events", events);
        lt_fact("delay", first_event_frame < 0 ? -1 : (long long)((uint64_t)first_event_frame - mark));
    }
#elif CASE == 9
    if (k == 1) lt_fact("set", tm->set_rate(lt_mod, 4000));
    if (k == 2) lt_fact("timer", tm->timer(lt_mod, LUMA_TIME_STEPS, 1, 1, on_timer, 0, &handle));
#elif CASE == 10
    if (k == 1) lt_fact("hold", tm->hold(lt_mod));
    if (k == 2) lt_fact("timer", tm->timer(lt_mod, LUMA_TIME_STEPS, 1, 0, on_timer, 0, &handle));
    if (k == 4) lt_fact("events_held", events);
    if (k == 3) lt_fact("release", tm->release(lt_mod));
    if (k == 5) lt_fact("events_after", events);
#elif CASE == 11
    if (k == 1) lt_fact("timer", tm->timer(lt_mod, LUMA_TIME_FRAMES, 20, 0, on_timer, 0, &handle));
    if (k == 2) {
        lt_fact("cancel_1", tm->cancel(lt_mod, handle));
        lt_fact("cancel_2", tm->cancel(lt_mod, handle));
    }
    if (k == 5) lt_fact("events", events);
#elif CASE == 12
    if (k == 1) {
        int32_t made = 0;
        int32_t last = 0;
        for (int32_t i = 0; i < 65; i++) {
            LumaHandle h = 0;
            last = tm->timer(lt_mod, LUMA_TIME_FRAMES, 100000, 0, on_timer, 0, &h);
            if (last == LUMA_PENDING && h != 0) made++;
        }
        lt_fact("made", made);
        lt_fact("last", last);
    }
#elif CASE == 13
    if (k == 1) order_armed = 1;
    if (k == 2) lt_fact("release", tm->release(lt_mod));
#elif CASE == 14
    if (k == 1) {
        const LumaHarness* harness = (const LumaHarness*)harness_table;
        lt_fact("made", harness != 0 && harness->size >= sizeof(LumaHarness) ? harness->other_thread(off_thread, 0) : 0);
        lt_fact("off_m_least", off_m1 >= 1);
        lt_fact("off_m_order", off_m2 >= off_m1);
        lt_fact("off_wall", off_wall);
        lt_fact("off_year", off_w.year);
        const int64_t m1 = tm->monotonic_ns(lt_mod);
        const int64_t m2 = tm->monotonic_ns(lt_mod);
        lt_fact("m_least", m1 >= 1);
        lt_fact("m_order", m2 >= m1 && m1 >= off_m2);
        LumaTimeWall w = {.size = sizeof(LumaTimeWall)};
        lt_fact("wall", tm->wall(lt_mod, &w));
        lt_fact("offset", w.utc_offset_s);
        lt_fact("year", w.year);
        lt_fact("month", w.month);
        lt_fact("day", w.day);
        lt_fact("weekday", w.weekday);
        lt_fact("hour", w.hour);
        /* Deterministic mode: UTC is the host's fixed calendar (from 2000) with offset 0, so the local fields are UTC's. */
        lt_fact("utc_s", (long long)(w.utc_ns / 1000000000ll));
    }
#elif CASE == 15
    if (k == 1) {
        LumaTimeNow small_now = {.size = 8};
        lt_fact("now_8", tm->now(lt_mod, &small_now));
        LumaTimeWall small_wall = {.size = 8};
        lt_fact("wall_8", tm->wall(lt_mod, &small_wall));
        LumaTimeNow whole = {.size = sizeof(LumaTimeNow)};
        lt_fact("now_whole", tm->now(lt_mod, &whole));
    }
#endif
    LUMA_RESULT(call, int32_t) = 1;
    return 0;
}

static const LumaImport imports[] = {
    LUMA_SERVICE(tm, LUMA_TIME_NAME, "1.0"),
#if CASE == 14
    LUMA_SERVICE(harness_table, LUMA_HARNESS_NAME, "1.1"),
#endif
    LUMA_HOOK_ON("MR::getRandom", "s32(s32, s32)", LUMA_AFTER, trigger),
};

LT_START({
    lt_fact("on_frame", tm->on_frame(mod, on_frame, 0));
    lt_fact("on_step", tm->on_step(mod, on_step, 0));
})
/* A stop, so a take-out proves it runs (reload.md 3: takeout.<id> "stopped" is true once its stop ran). */
static void stop(void) {}

LUMA_MOD(imports, start, stop);
