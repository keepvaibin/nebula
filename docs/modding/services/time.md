# Service: time

## More rules

### 1. What is decided here, in one list

1. `time` 1.0, no permission, provided by the game module (`Game/System/LumaTime.cpp`), catalogued once signed.
2. Two counts: frames (pictures presented, `NebulaFrameClock::count()`, one per pass of `GameSystem::frameLoop`)
   and steps (world steps: runs of the game's scene update). `step` counts the scene updates that ran: with no mod
   using `time`, one per frame while a scene runs and the game does not hold it.
3. The world clock: each mod may set a rate (thousandths: 1000 is real time, 1 to 16000) and a hold. The
   world's rate is the exact product of every mod's rate, rounded down once and clamped to 1..16000, so the list's
   order never changes it; the world is held while any mod holds it. A mod that holds alone may step it: 1 to 16 steps, all in the next frame.
4. What the world clock moves: the scene (`GameSystem::updateSceneController`'s scene update: play, the game's own
   menus inside the scene, the scene's layouts, the camera, the system wipe's fade, which runs inside the scene's
   branch: a held fade stays as under the HOME Menu) and the controls the scene reads. What it never moves: the
   frame itself (pacing, drawing), the system (settings menu, HOME Menu, error windows, Nebula's own screens,
   including those that live in the scene: the mod-log notice, `NebulaModLogNotice`, once a frame), events and
   `on_frame` handlers. A held world is drawn every frame, as under the HOME Menu. `calcAnim` runs once after each
   step, and once in a frame with none (it moves no animation: a held world's matrices stay).
5. The world clock is 1000 and not held before the first scene, while a scene loads, a movie plays or the cutscene
   skip runs; what mods set applies again when that ends (the state says which). While the game holds the scene
   itself (HOME Menu, settings menu, system warning, skip question) no step runs.
6. Sound follows as a tape does: at a rate from 125 to 4000 the sound engine's music, sequences and voices run at
   that speed and pitch; held, or outside 125..4000, the world's sound is held as under the pause menu (music paused,
   the game's effects silent, the system's play on). Movies' sound is never touched.
7. Pacing does not change: frames are presented at the display's rate whatever the world clock; a frame with two
   steps takes the work of two; a frame with none draws the held world. The host's interpolation sees held frames as
   still.
8. Timers fire as events at the frame's start (services-registry 6.1) after a count of frames, steps or real
   nanoseconds, once or repeating; 64 a mod.
9. `on_frame` (each frame, after every mod's events, list order) and `on_step` (each world step, before the scene's
   update, list order): one of each per mod.
10. The player has the last word: the Mods page shows each mod's clock (`Time: held`, `Time: 2x`) and its
    "Let go" clears that mod's rate, hold and owed steps from the next frame, as its `mod_gone` does, with a mod-log
    Note (`Time.LetGo`). The mod sees it in `now` and may hold again later.
11. Real clocks: monotonic nanoseconds (any thread, the host's steady clock) and the wall clock (UTC nanoseconds and
    the local offset). In the harness's deterministic mode both follow the frames, so every run is the same.

### 2. The header: `native/include/luma_time.h`

```c
/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore's time service (docs/modding/services/time.md): frames and world steps, the real clocks, the world clock's
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
typedef enum LumaTimeEvent {
    LUMA_TIME_EVENT_TIMER = 1 /* data: none; value: the times it came due since its last event */
} LumaTimeEvent;

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
```

### 3. The frame, step by step

`GameSystem::frameLoop` (today: wait, `update`, `calcAnim`, the skip's extra steps, draw) becomes:

1. `NebulaFrameClock::sleepUntilFrame()` (the host's pacing, unchanged).
2. `luma::time::beginFrame()` decides this frame's steps `n` (section 4). No mod has set anything: `n = 1`, one load.
3. `update()` as today. Inside it, after `luma::frame()`, `luma::services::frameStart()` first posts the due timers'
   events (section 6), then gives every mod's events, then runs every `on_frame` handler in list order. Then
   `updateSceneController`: when the scene would update (no HOME Menu, warning, settings menu or
   skip question) and `n = 0`, the scene is held as under the HOME Menu; `isSceneHeld` (the game's own holds) does
   not include it, so the clock's hold never reads as MENU (a movie is MOVIE, n = 1, and never meets it). When
   it updates: every `on_step` handler in list order, then `mSceneController->updateScene()`. The scene's first
   update after a load runs even in a frame with `n = 0` (as under the HOME Menu) and counts as a step: `step` may
   grow by 1 in a frame whose `steps` is 0.
4. `calcAnim()`.
5. For each further step (`n - 1` times): `mObjHolder->update()` (the controls and the pointer, so a press is seen
   by one step, not repeated), `updateNerve()` (the scene controller: `on_step`, the scene's update, the audio
   system's movement), `calcAnim()`. The frame's own parts (the restart process, scene change requests, events,
   `on_frame`, the settings menu, the HOME Menu, the skip) run once a frame, in step 3 only. A scene change a later
   step asks for is taken at the next frame's first step.
6. The skip's extra steps, as today (the world clock is 1000 while it runs, section 5).
7. Draw, as today: once, whatever `n`.

### 4. The world clock

- Each mod's record holds `rate` (1000 at begin) and `hold` (0). `set_rate`, `hold`, `release` and `step` write them for the
  next frame; nothing changes inside a frame.
- The world's rate: with `k` mods whose rate is not 1000, `rate = (r1 * r2 * ... * rk) / 1000^(k-1)`, the
  product exact (a small unsigned big number, 32-bit limbs) and divided once, rounding down; then clamped to
  1..16000. It is computed when a mod's rate changes or a mod goes, not every frame. No list order enters it.
  `holders` = the mods holding.
- Not held: `acc += rate; n = acc / 1000; acc -= n * 1000`. `acc` starts at 0 and is kept across rate changes, so
  500 gives 0, 1, 0, 1 and 1500 gives 1, 2, 1, 2 from the frame it is set. `n` is at most 16.
- Held: `n` = the steps owed (step calls since the last frame), then 0 owed; `acc` is kept.
- The controls are read every frame (the system's screens need them). In a frame with `n = 0` because of a rate
  below 1000 (not a hold, not the game's own hold), the presses and releases of the buttons and the stick's
  directions are carried to the next step's read, so no tap is lost at any rate; a frame held by a mod or
  by the game carries none (a press that closed a mod's own screen or the HOME Menu never reaches the scene).
- State BOOT, LOADING, MOVIE or SKIP: `n = 1`, `acc` kept, owed steps dropped (step returned LUMA_E_BUSY there
  anyway).
- State MENU because the game holds the scene itself (`GameSystem::mWasSceneHeld`: HOME Menu, settings menu, system
  warning, skip question): no step runs (`n = 0`, the scene held by the game as today); `acc` and owed steps are
  kept; `steps` is 0 and `step` does not grow. The scene's own pause menu and galaxy map are MENU too but run
  in the scene, so they follow the clock. The hold is the previous frame's (the game decides this frame's inside the
  first update): an extra step runs only while the game does not hold the scene (`frameLoop` checks
  `mWasSceneHeld` before each), and `step` counts only scene updates that ran, so a frame in which a menu opens or
  closes is still exact.
- A mod taken out (its `mod_gone`): its rate and hold go, its owed steps are dropped, from the next frame.
- Determinism: integers only; the same mods, list and inputs give the same steps on every machine and platform.

### 5. State

From the game, at `beginFrame`: BOOT while the boot scene runs (`SceneKind_Boot`); LOADING while the scene
controller's nerve is not `Normal` (built, destroyed, waiting for draw); MOVIE while `MR::isActiveMoviePlayer()`;
SKIP while `NebulaCutsceneSkip::isSkipping()`; MENU while the scene is held by the game (the settings menu's or the
skip's freeze, the HOME Menu, a system warning) or the scene's own pause menu or galaxy map is up; PLAY otherwise.
The checks are in that order; the first that holds is the state.

### 6. Timers

- A timer counts its clock from the call: frames from the frame of the call, steps from the step count then, the
  monotonic clock from the call's reading. Due: `now >= start + after`, then every `every`.
- Checked at `frameStart`, before the event pass, only when the mod has timers (one count per mod: no mod with a
  timer, nothing read). A due timer's event is posted to the mod's queue with `value` = times due since its last
  event, so it arrives in this frame's pass in post order with the mod's other events.
- Steps timers do not move while the world is held; frames and nanoseconds timers do.
- A timer given once closes after its event; `cancel` closes any; `mod_gone` closes all of the mod's (no event).

### 7. Sound

- `luma::time` hands the sound engine one number a frame (an atomic, written at `beginFrame`): the sound rate.
  `sound_rate = 1000` in BOOT, LOADING, MOVIE and SKIP; 0 when held or when the world's rate is below 125 or above 4000;
  the world's rate otherwise.
- `SoundEngine::tick` (the host's audio clock, 400 ticks a second): at 1000, as today. At another rate `s` (125 to
  4000), the engine's logic (voices' updates, the tracks, the streams' and calls' jobs) runs `k` times per tick
  (`sacc += s; k = sacc / 1000; sacc -= k * 1000`) and the voices' playback rate is multiplied by `s / 1000`
  (`setVoiceRate`); the output stays one tick: music, sequences, effects and streams play at that speed and pitch,
  as a tape does. The commands the game wrote since the last tick are applied once.
- 0: `AudSystem` holds the world's sound as the pause menu does (music paused, the game's effects at the pause
  menu's volume set, the system's play on) and takes it back when the rate returns; it composes with the settings
  menu's own hold (the later of the two to end restores).
- Movies' sound (`movie_audio`) is the host's and never changes.

### 8. No cost with no mod

- `beginFrame`: one load (`sAnyControl`: some mod has a rate other than 1000, a hold, or owed steps) and `n = 1`.
- `frameStart`'s time passes: two loads (LumaServices' two hook pointers, set by `time` only while some mod has a
  timer or a frame handler).
- `beginStep`: an increment and one load (the step handlers' count).
- `updateSceneController`: the held test is part of `isSceneUpdate` (one bool).
- `SoundEngine::tick`: one relaxed load and a compare with 1000.

### 9. Messages (codes 120-139)

| Code | Row | English | Args |
|---|---|---|---|
| 120 | `Time.NotHeld` | {mod}: {where} works only while the mod holds the game; it did nothing. | where |
| 121 | `Time.LetGo` | {mod}: you set the game back to its normal speed. | (none) |

Every other mistake uses the registry's rows: `Service.Argument` (rate, count, clock, after, fn, out), `Service.Limit`
(64 timers, 16 owed steps), `Service.Handle`, `Service.Thread`, `Service.StructSize` (the struct of the first minor
whole: LumaTimeNow 64 bytes, LumaTimeWall 32). `Time.LetGo` is a Note in the mod log (the player's act, not the mod's
mistake): source CODE, code 121, level NOTE, row `Time.LetGo` through the host's `galaxy::modlog::add_row`, as
services-system's 140 and 141, written by the Mods page's code after `luma_time_let_go` returns 1.

### 10. LumaScript

`time.now()`, `time.set_rate(500)`, `time.hold()`/`time.release()`, `time.step(n)`, `time.timer(...)`,
`time.cancel(h)`; a task's `wait` is the timer (`wait N frames` counts frames, `wait N seconds` world steps at 60 a
second); `on frame` is lumart's frame pass; `on step` compiles onto `on_step` and may not wait; `on time.timer` is
refused. Registry 6.1 already puts lumart's tasks in the time pass: they run in `frameStart` after `on_frame` handlers
of mods above them in the list and before those below (list order, one pass).

### 11. Files

- New: `native/include/luma_time.h`; `native/petari/include/Game/System/LumaTime.hpp`,
  `native/petari/src/Game/System/LumaTime.cpp` (the service, the clock, timers, handlers).
- Changed: `GameSystem.cpp` (`frameLoop` steps 2 and 5, `updateSceneController`'s hold and `on_step`),
  `SoundEngine.cpp/.hpp` (the tape), `SoundVoices.cpp` (`setVoiceRate`'s factor), `AudSystem.cpp/.hpp` (the world's
  hold), `LumaServices.cpp/.hpp` (catalog row, provideOwn, the time pass at `frameStart`), `SoundVoices.hpp`
  (`updateSlots`, `mix`, `setRateScale`), `GameSystemSceneController.cpp/.hpp` (`isNerveNormal`, the LOADING test),
  `GameScene.cpp/.hpp` (`isExecPauseMenuOrGalaxyMap`, the MENU test), `NebulaText.tsv` (rows 120, 121).
- Host: `luma_services_host.cpp` gives the module the UTC clock: `provide_all` calls the module export
  `luma_services_facts` once, after `luma_services_open` and before the first mod, with `LumaServicesFacts { size;
  int64_t (*utc_ns)(void); }` from the new `native/include/luma_services_facts.h` (not for mods;
  `luma_service_provider.h` unchanged). A module without the export is logged; deterministic mode does not call the
  clock: UTC is the calendar's local time from 2000 with offset 0.
- For the Mods page (6.2): module exports by mod id, any thread: `int32_t luma_time_clock_of(const char* id, uint32_t*
  rate, int32_t* holds)` (1 known, 0 not; read from a copy of each clock's id, rate and hold kept under a sequence
  count) and `int32_t luma_time_let_go(const char* id)` (1 asked, 0 not known or 8 already waiting; the next frame's
  start clears the mod's rate, hold and owed steps; the page writes the `Time.LetGo` Note on 1).

### 12. Tests (deterministic mode)

| Test | Proves |
|---|---|
| 2201 | `now`: frame grows by 1 a frame, step by 1 a frame with nothing set, state PLAY in a stage |
| 2202 | rate 2000: 2 steps a frame (`steps` 2, `step` +2); `on_step` called twice a frame, `step_in_frame` 0, 1 |
| 2203 | rate 500: steps 0, 1, 0, 1; `sound_rate` 500 |
| 2204 | two mods at 500 and 4000: world 2000; three at 1001, 1001, 999: 1000 in every list order |
| 2205 | hold: `step` stops, `frame` grows, `holders` 1, `sound_rate` 0; release: steps again |
| 2206 | hold then step(3): one frame of 3 steps, then 0 again |
| 2207 | step without holding: LUMA_E_ARGUMENT, Time.NotHeld in the log once |
| 2208 | two mods hold; one steps: LUMA_E_BUSY, nothing logged |
| 2209 | set_rate 0 and 16001, step 0 and 17: each LUMA_E_ARGUMENT, reported; release without hold: LUMA_OK |
| 2210 | timer FRAMES after 10: one event 10 frames later, value 1; repeating every 1 with rate 4000 on STEPS: value 4 |
| 2211 | timer STEPS while held: no event; after release: due |
| 2212 | cancel before due: no event; cancel again: LUMA_E_HANDLE |
| 2213 | 65th timer: LUMA_E_LIMIT, Service.Limit with 64 |
| 2214 | `on_frame` in list order: two mods, the higher's first |
| 2215 | the mod that held is taken out (1.11's stop): the world runs again the next frame (needs 1.11) |
| 2216 | `monotonic_ns` from another thread: at least 1, not decreasing; `wall`: offset 0, utc from the calendar |
| 2217 | `now` with a struct of 8 bytes: Service.StructSize with 8 and 64; `wall` with 8: with 8 and 32 |
| 2218 | Let go pressed on the Mods page (keys: `{"settings": "mods"}` (the page by name; a number is a harness-error), `space`, `down`, `space`, 60 frames apart): the mod's `now` shows `holders` 0 the next frame; the fact `modlog` {mod, source code, level note, code 121} |

Sound's tape (section 7) has no fact of its own beyond `sound_rate`;
