/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's time service (contract services-time): frames and world steps, the real clocks, the world clock's
   rate, hold and step, timers, frame and step handlers. Import: LUMA_SERVICE(slot, "time", "1.0"). */
#ifndef LUMA_TIME_H
#define LUMA_TIME_H

#include <stdint.h>

#include "luma_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_TIME_NAME "time"
#define LUMA_TIME_MAJOR 1u
#define LUMA_TIME_MINOR 0u

/* The world clock's rate in thousandths: real time, and the range a mod may set. */
#define LUMA_TIME_REAL 1000u
#define LUMA_TIME_RATE_LEAST 1u
#define LUMA_TIME_RATE_MOST 16000u
/* Steps one frame runs at most (16000 / 1000), and one step call asks at most. */
#define LUMA_TIME_STEPS_MOST 16u
/* Timers one mod has at once. */
#define LUMA_TIME_TIMERS_MOST 64u

/* What the game is doing (LumaTimeNow.state). */
enum {
    LUMA_TIME_BOOT = 0,    /* before the first scene: the boot scene and the system's files */
    LUMA_TIME_LOADING = 1, /* a scene is being built or taken down */
    LUMA_TIME_PLAY = 2,    /* a scene runs */
    LUMA_TIME_MENU = 3,    /* a scene is up and the game holds it itself: its pause menu, the HOME Menu, the settings
                              menu, an error window, the skip's question */
    LUMA_TIME_MOVIE = 4,   /* a movie plays */
    LUMA_TIME_SKIP = 5     /* the cutscene skip runs the scene unseen */
};

/* A timer's clock (timer's `clock`). */
enum {
    LUMA_TIME_FRAMES = 0,
    LUMA_TIME_STEPS = 1,
    LUMA_TIME_NANOSECONDS = 2 /* the monotonic clock */
};

/* LumaEvent.kind of `time`: a timer came due. `handle` is the timer, `value` how many times it came due since its
   last event (more than 1 when its period is shorter than a frame), `status` LUMA_OK. A timer given once is closed
   after its event. */
#define LUMA_TIME_EVENT_TIMER 1u

/* The frame and its world step as the game sees them. Every field holds for the whole frame, except `step` and
   `step_in_frame`, which an on_step handler sees for its own step. */
typedef struct LumaTimeNow {
    uint32_t size;          /* sizeof(LumaTimeNow) as the mod was built */
    uint32_t state;         /* LUMA_TIME_BOOT.. */
    uint64_t frame;         /* frames presented since start (the first is 1) */
    uint64_t step;          /* world steps since start */
    uint32_t steps;         /* the world steps this frame runs (0 held, 1 real time, up to 16) */
    uint32_t step_in_frame; /* in on_step: this step's place in the frame, from 0; elsewhere `steps` */
    uint32_t rate;          /* the world's rate now, thousandths (the product of the mods'; 1000 while loading) */
    uint32_t holders;       /* how many mods hold the world now (0: not held) */
    uint32_t sound_rate;    /* the rate the game's sound follows now, thousandths; 0: the world's sound is held */
    uint32_t reserved;      /* 0 */
    int64_t frame_start_ns; /* the monotonic clock at this frame's start */
    int64_t frame_ns;       /* real time from the previous frame's start to this one's */
} LumaTimeNow;

/* The wall clock. */
typedef struct LumaTimeWall {
    uint32_t size;        /* sizeof(LumaTimeWall) as the mod was built */
    int32_t utc_offset_s; /* local time minus UTC, seconds (the player's time zone and daylight saving now) */
    int64_t utc_ns;       /* nanoseconds since 1970-01-01 00:00 UTC */
    /* Local time, from the two above. */
    int32_t year;         /* e.g. 2026 */
    uint8_t month;        /* 1-12 */
    uint8_t day;          /* 1-31 */
    uint8_t weekday;      /* 0 Sunday - 6 Saturday */
    uint8_t hour;         /* 0-23 */
    uint8_t minute;       /* 0-59 */
    uint8_t second;       /* 0-59 */
    uint16_t millisecond; /* 0-999 */
} LumaTimeWall;

/* A frame or step handler: `now` is valid during the call only. */
typedef void (*LumaTimeFn)(void* user, const LumaTimeNow* now);

typedef struct LumaTimeService {
    LumaServiceTable base;
    /* 1.0. Game thread. Fills `out` (out->size bytes, whole fields). LUMA_OK. A null out: LUMA_E_ARGUMENT; a size below
       1.0's struct (64): LUMA_E_ARGUMENT (Service.StructSize). Both reported. */
    int32_t (*now)(const LumaMod* mod, LumaTimeNow* out);
    /* 1.0. Any thread. The monotonic clock in nanoseconds (at least 1), or LUMA_E_ARGUMENT for a null or unknown mod,
       LUMA_E_CANCELLED for a mod being taken out. Deterministic mode: the frames' clock. */
    int64_t (*monotonic_ns)(const LumaMod* mod);
    /* 1.0. Any thread. The wall clock now. LUMA_OK. A null out, or a size below 1.0's struct (32): as `now`. */
    int32_t (*wall)(const LumaMod* mod, LumaTimeWall* out);
    /* 1.0. Game thread. This mod's factor of the world's rate, from the next frame: 1 to 16000 thousandths (1000 takes
       it back). Outside: LUMA_E_ARGUMENT, reported (Service.Argument "rate"). */
    int32_t (*set_rate)(const LumaMod* mod, uint32_t rate);
    /* 1.0. Game thread. This mod holds the world from the next frame (holding twice is holding). LUMA_OK. */
    int32_t (*hold)(const LumaMod* mod);
    /* 1.0. Game thread. This mod lets the world go from the next frame, and its owed steps are dropped (not holding:
       nothing to do). LUMA_OK. */
    int32_t (*release)(const LumaMod* mod);
    /* 1.0. Game thread. `count` more world steps in the next frame (1 to 16 a call, at most 16 owed). LUMA_OK.
       This mod does not hold the world: LUMA_E_ARGUMENT, reported (Time.NotHeld). Another mod holds it too, or the
       state is BOOT, LOADING, MOVIE or SKIP: LUMA_E_BUSY (an answer, not reported). `count` 0 or over 16: LUMA_E_ARGUMENT,
       reported; more than 16 owed: LUMA_E_LIMIT, reported. */
    int32_t (*step)(const LumaMod* mod, uint32_t count);
    /* 1.0. Game thread. A timer on `clock` (LUMA_TIME_FRAMES, STEPS, NANOSECONDS): due `after` units from now (at
       least 1), then every `every` units (0: once). LUMA_PENDING with the timer in *out; its events go to fn.
       A null fn or out, a zero `after`, an unknown clock: LUMA_E_ARGUMENT; a 65th timer: LUMA_E_LIMIT; both
       reported. */
    int32_t (*timer)(const LumaMod* mod, uint32_t clock, uint64_t after, uint64_t every, LumaEventFn fn, void* user,
                     LumaHandle* out);
    /* 1.0. Game thread. Ends a timer: no event for it is given after this returns (registry 4.8). LUMA_OK; a handle
       that is not this mod's open timer: LUMA_E_HANDLE, reported. */
    int32_t (*cancel)(const LumaMod* mod, LumaHandle timer);
    /* 1.0. Game thread. This mod's frame handler, from the next frame (a null fn removes it). LUMA_OK. */
    int32_t (*on_frame)(const LumaMod* mod, LumaTimeFn fn, void* user);
    /* 1.0. Game thread. This mod's step handler, from the next step (a null fn removes it). LUMA_OK. */
    int32_t (*on_step)(const LumaMod* mod, LumaTimeFn fn, void* user);
} LumaTimeService;

#ifdef __cplusplus
}
#endif

#endif
