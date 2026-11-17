# E. Frame pacing and in-between pictures, as built (2026-11-17)

Tree: D:\NebulaWork\MarioGalaxyRecomp\Nebula-Clean, branch clean/work, working tree as found (uncommitted changes included). Read only; nothing was built or run.

Paths below are relative to that tree unless absolute. "verified in code" (V) means the cited lines were read. "suspected" (S) names the read or run that would settle it.

(Coverage table: at the end of the file, section "Coverage".)

## 0. Uncommitted change in render_core (git diff)

- render_core.h:90-94: `pipeline_progress()` now also returns `pipelines_.warm()` as a third field. V.
- render_core.cpp start_interp_sequence (:756-757) and interp_rate_with (:1121-1122): the rule for putting in-between pictures on vblanks changed. V.
  - Before: `on_vblanks = cfg.vsync_interval != 0u && vblank_grid(...)`.
  - Now: `on_vblanks = pictures_on_refreshes(renderer_.swap_queue()) && (cfg.vsync_interval != 0u || std::isfinite(rate)) && vblank_grid(...)`.
  - Effect: with VSync off, a swap queue that cannot tear (Vulkan MAILBOX) and a finite rate, slots now go on vblanks too. A tearing queue (D3D12, D3D11 and Metal with VSync off; Vulkan IMMEDIATE) never puts slots on vblanks. D3D12, D3D11 and Metal report EveryPicture with VSync on, so VSync on still puts slots on vblanks there. An Unlimited rate with VSync off keeps no slots (comment updated to say so).
- The same uncommitted set touches more pacing files (git diff, read): V.
  - present_slots.h adds `SwapQueue` {EveryPicture, NewestPicture, Tearing}, `presents_timed` (EveryPicture only) and `pictures_on_refreshes` (all but Tearing). Each renderer header adds `swap_queue()` (renderer_d3d12.h:652-655 in the diff; D3D11, Metal, Vulkan headers likewise).
  - renderer_vk.cpp: the present pacer is skipped under interpolation (:2214-2216), and Android display timing is used only when `presents_timed(swap_queue())`, i.e. FIFO, not MAILBOX (:2293-2298).
  - renderer_mtl.mm: the present pacer is skipped under interpolation (:733-735).
  - present_slots_tests.cpp: +45 lines, `check_swap_queues` and the newest-picture queue simulation (J.2).
  - render_config.h/.cpp: `apply_benchmark_standard` removed (not pacing).
  - Other changed files (host_main.cpp +31/−3, host_window_sdl.cpp, host_window_apple.mm, GameActivity.java, input files) were read only in the parts cited below; their diffs were not reviewed line by line (S: `git diff` of those files would show whether any pacing line moved).

## A. Clocks and threads

### A.1 Threads

| Thread | Created at | What it does | Priority |
|---|---|---|---|
| Game thread | host_main (svc calls from the game) | update, calcAnim, draw list build, waits in svc_frame_wait | default |
| "Nebula Render" | render_core.cpp:1515-1542 render_thread_main | render_loop: walks lists, makes in-between pictures, presents | THREAD_PRIORITY_ABOVE_NORMAL on Windows, EcoQoS opt-out, 1 ms timer resolution (V) |
| "Nebula Vblank" (Windows) | vblank_clock_win32.cpp | IDXGIOutput::WaitForVBlank on the window's monitor, least-squares period over up to 256 wakes (32 least), phase filter 1/8, 3 off-grid wakes (> period/4) reset, pauses > 50 ms ignored, sleeps when not asked for 2 s | TIME_CRITICAL (V) |
| "Nebula Vsync" (Android) | platform/sdl/display_clock.cpp | Choreographer callbacks: API 33 postVsyncCallback (preferred timeline's expected presentation time), else postFrameCallback64; registerRefreshRateCallback; re-posts forever | default (V) |
| Display link (Apple) | platform/apple/display_clock_apple.mm | macOS CVDisplayLink (never stopped, follows the window's screen); iOS CADisplayLink preferredFrameRateRange(max,max,max) | system (V) |
| Telemetry publisher (Windows) | frame_telemetry.cpp | publishes a 32-word shared-memory packet once a second | default (V) |

### A.2 Game step

- Fixed step: `kGameStepsPerSecond = 60`, `kGameStepNs = 16,666,666` (runtime/include/galaxy/game_step.h:10-12). V.
- Loop order, GameSystem.cpp:182-206 (V):
  1. `sleepUntilFrame` → svc frame_wait (NebulaFrameClock.cpp).
  2. `update` (input read here: PlayerInputs.cpp:93).
  3. `calcAnim`.
  4. extra world steps (luma::time) and cutscene-skip steps.
  5. `beginFrame` → svc frame_begun: the host presents the newest finished picture (host_main.cpp:3069-3134).
  6. `drawFrame` → svc submit_frame: list N goes to the render thread (host_main.cpp:2647-2659, `submit_frame(false,false)`).
- Host clock: `std::chrono::steady_clock` (host_main.cpp:213). V.
- Boot: period 2 ms for the first 600 frames (kBootPeriod, kBootFrames, host_main.cpp:975-1035 region). V.
- Fixed-step test mode: NEBULA_EXT_FIXED_STEPS sets deterministic_mode (host_main.cpp:990-995). V.

### A.3 What paces the step: svc_frame_wait (host_main.cpp:1103-1179, V)

| Host | Wait | Next start |
|---|---|---|
| SDL (Android, Linux, SDL builds) | `sdl::wait_display_frame`, else PreciseWait to `next` | on a vsync lock: `next = now + period` |
| Windows native host | PreciseWait to `next` | `next_frame_start` (FramePhase, host_main.cpp:1063-1072) |
| Others | PreciseWait | `next += period`; when late `next = now + period` |

All paths then call perf hints and `set_step_moment(due_qpc)`. The comment at host_main.cpp:1099-1102 says the step runs at 60 Hz; on Windows and on SDL with a vsync lock it follows the display (see below). V.

#### Windows: FramePhase (frame_phase.cpp:24-47, frame_phase.h, V)

- `display_refresh()` (host_main.cpp:1037-1060) reads DwmGetCompositionTimingInfo every 30th call. It is called once per game step, so this is every 30 steps, not every 30 pictures. V.
- `refreshes_in_step` (frame_phase.cpp:17-22) requires two DWM readings that agree within period/20.
- `refreshes_per_frame` (frame_phase.cpp:9-15): `count = round(step/refresh)`; 0 unless `|count·refresh − step|·100 ≤ step` (1%).
- `next_start`:
  - work = `g_last_present_ns − previous step start` (host_main.cpp:1063-1072). g_last_present_ns (present_clock.h) is written in note_picture_shown, only for real pictures (render_core.cpp:2521-2565).
  - budget starts at 8 ms, rises at once to work + 1.5 ms, falls by 1/256 a frame, capped at period − 1.5 ms.
  - work above budget → plain host clock for 600 frames.
  - plain when there is no refresh reading, while plain, or when budget > 0.75·period.
  - otherwise step = count·refresh period, and the phase moves toward `vblank − budget` by at most ±0.5 ms a frame.
- Result: on a locked display the game runs at count·refresh, so at 59.94 Hz the game runs at 59.94 steps/s.

#### Android: Choreographer (platform/sdl/display_clock.cpp:119-160, V)

- Falls back to the host clock if no vsync arrived for 250 ms.
- Locks only when `|step/interval − round| ≤ 0.03` and round ≥ 1.
- Target = the first vsync after `last + period − interval/2` (from now if it fell behind); clock_nanosleep TIMER_ABSTIME.
- Locked step period = round(vsyncs)·interval, so the game speed follows the display within 3%.
- The interval is the stated refresh period when registerRefreshRateCallback gave one; else smoothed 7/8, reset on a jump above 1/8.

#### Apple: display link (display_clock_apple.mm, V)

- Same 3% lock rule as Android; mach_wait_until.
- note_vsync smoothing 1/8; a gap above 3 intervals resets.
- With present-stats on: a lateness log every 600 waits.

#### Linux (SDL, non-Android, non-Apple)

- Every display_clock function returns false or 0 (platform/sdl/display_clock.cpp:193-211). V.
- So the step always runs on the host clock (PreciseWait): exactly 60 steps/s, never tied to the display. V.

#### PreciseWait (precise_wait.h, 147 lines, V)

- Margin: least 100 µs, most 2000 µs, guard 100 µs, first 1000 µs. Rises at once to the latest late wake + guard, falls 1/16 a wake.
- Windows: high-resolution waitable timer; Linux: PR_SET_TIMERSLACK 1; then a spin with pause/yield.

### A.4 Decision tree: game speed

```
Windows native host (D3D12, D3D11, Vulkan on Windows):
  DWM reading present, two readings agree (period/20), refreshes_per_frame within 1%,
  budget <= 0.75 step, no budget miss in the last 600 frames
    -> step = count * refresh period (follows the display)
  else -> plain 60 Hz host clock
Android:
  vsync seen in the last 250 ms and |step/interval - round| <= 0.03
    -> step = round * interval (follows the display)
  else -> host clock 60
Apple: same as Android (display link).
Linux SDL: host clock 60, always.
```

### A.5 Per-rate table (refresh rate → game speed; and in-between picture slots)

"Slot" = the spacing of in-between pictures with VSync on and a vblank grid, from `vblanks_per_slot` (render_core.cpp:143-149): `max(1, ceil(refresh/rate − 0.02))`, and if `refresh/slot < 61`, `floor(refresh/61)`. Rate = present_frame_rate (render_config.h:205-210) = max_fps, with VSync on capped at the refresh. Values derived from the cited code; V for the rule, the arithmetic is mine.

| Refresh | Ratio to 60 | Windows step | Android/Apple step | Linux step | In-between (max_fps ≥ refresh, VSync on) |
|---|---|---|---|---|---|
| 48 | 0.8 | host 60 | host 60 | 60 | none (rate ≤ 61) |
| 50 | 0.833 | host 60 | host 60 | 60 | none |
| 59.94 | 0.999 | locked, 59.94 steps/s | locked, 59.94 | 60 | none |
| 60 | 1 | locked 60 | locked 60 | 60 | none |
| 72 | 1.2 | host 60 | host 60 | 60 | 1 per step requested; slots every 1 vblank (72 Hz) |
| 75 | 1.25 | host 60 | host 60 | 60 | 1; slot 1 vblank |
| 90 | 1.5 | host 60 | host 60 | 60 | 1; slot 1 vblank |
| 100 | 1.67 | host 60 | host 60 | 60 | 1; slot 1 vblank |
| 119.88 | 1.998 | locked, 59.94 | locked, 59.94 | 60 | 1; slot 1 vblank |
| 120 | 2 | locked 60 (2 refreshes) | locked 60 | 60 | 1; slot 1 vblank |
| 144 | 2.4 | host 60 | host 60 | 60 | 2; slot 1 vblank, t step 0.4167 |
| 165 | 2.75 | host 60 | host 60 | 60 | 2; slot 1 vblank, t step 0.3636 |
| 240 | 4 | locked 60 (4) | locked 60 | 60 | 3; slot 1 vblank |
| 360 | 6 | locked 60 (6) | locked 60 | 60 | 5; slot 1 vblank |
| VRR | n/a | DWM grid fails `refreshes_in_step` → host 60 (S: depends on DWM readings under VRR) | the step locks to Choreographer's or the display link's stated period; the renderer's VRR detection (renderer_vk.cpp:2718-2727, renderer_mtl.mm:998-1007) does not reach the step clock (S) | 60 | see A.6 |

Notes:
- "In-between per step" is `interpolated_frames_per_game_frame(rate)` = `ceil(rate/60 − 1e-3) − 1`, 0 when rate ≤ 61, unbounded when the rate is infinite (render_config.h:219-224). V. The GPU and cadence governors can lower it (D.4).
- At 120 Hz with max_fps 120 on a 144 Hz display: `vblanks_per_slot = ceil(144/120 − 0.02) = 2` → slots at 72 Hz, not 120. V (arithmetic from :143-149).
- Linux: no vblank grid (A.3), so slots use the free QPC clock at 1/rate even with VSync on. V.

### A.6 Variable refresh (VRR)

- Detected: `next_variable_refresh` (renderer_common.cpp:290-399 region, V) turns VRR on when the measured vblank rate is below 0.8× the mode rate and off when it is ≥ 0.95× (hysteresis).
- Callers: D3D12 refresh_display_rate (renderer_d3d12.cpp:3882-3920), Vulkan (renderer_vk.cpp:2718-2727), D3D11 (renderer_d3d11.cpp:890-899), Metal (renderer_mtl.mm:998-1007). V.
- D3D12 and D3D11 require tearing support for VRR to count (V). 
- When on: display_refresh_hz = `variable_refresh_cap` = mode − mode²/3600 (e.g. 144 → 138.24) (renderer_common.h, V); `paced_present_rate` (render_config.h:367-377) uses it; PresentPacer (renderer_common) waits up to 2 ms toward last + 1/fps, only with VSync off and a finite cap. Interpolation passes 0 to the pacer (no pacing). V.
- Gap: the detector needs the measured vblank rate to fall below 0.8× the mode. When the game presents near the mode rate (e.g. 2 in-between pictures at 144 Hz → 140+ pictures/s), the vblank thread's measured rate stays near the mode and VRR is not detected. S: settle with a VRR monitor run logging `variable refresh` state at 144 Hz with interpolation on.
- Gap: with VSync on under VRR, interpolation slots follow the vblank grid, and on a VRR panel the vblanks follow the presents. The slot clock and the present clock then feed each other. S: a VRR run with the frame timeline (VBlank vs PresentEnd events).
- Android and Apple: no VRR path beyond the refresh reading; Apple display_grid always returns false (display_clock_apple.mm:224-234). V.

### A.7 Render thread loop (render_core.cpp, V)

- Producer (game thread) `render_frame` (:551-640): copies the chunk; blocks while non-present chunks reach `render_queue_depth = 2` (render_config.h:286), up to 8 once one chunk has run 20 ms (`kRenderStallNs`, `kStalledRenderQueueDepth`, render_core.h:1054-1055). Time blocked goes to `game_wait_qpc_`.
- `render_loop` (:1694-1956): hidden or background window → the sequence is superseded, occlusion probe every 100 ms; waits on `queue_cv` until 1.5 ms before the next slot, then PreciseWait to the slot. The comment at :1695 says "Called with queue_mutex_ held"; the function takes the lock itself (comment wrong).
- `idle_present` (:1751-1752 region) is computed from `frame_rate_decouple`, which render_config.cpp:595 and :670 force to false on every load: dead branch (B P-001 confirmed).
- A list chunk with a SHOW pass and interpolation on waits (`interp_list_waiting_`, :2615-2631). The present-only chunk that `svc_frame_begun` of the next step sends starts its sequence (`run_pending_interp_start`, :730-741).
- With interpolation off, a list presents at the end of its own walk (`frame_end_present`, :2650-2653); the next step's present-only chunk is a duplicate and is skipped (:2655-2660).

## B. Settings

### B.1 config.ini keys (render_config.cpp, render_config.h, V)

| Key | Range and default | Effect on pacing |
|---|---|---|
| `vsync_interval` | 0..2 (render_config.cpp:188, :305); default 1 on Apple, 0 elsewhere (render_config.h:20-130 region) | 0 = tearing or MAILBOX; 1 = FIFO / sync interval 1; 2 = sync interval 2 (D3D12/D3D11 `min(2, cfg)`). Forced to 1 when `present_waits_for_refresh()` (render_config.cpp:429-431): Metal on iOS (renderer_mtl.mm:302), Vulkan surfaces with neither MAILBOX nor IMMEDIATE (renderer_vk.cpp:998). |
| `max_fps` | "unlimited" → +inf; 0 → the display's rate; else ≥ 1 (render_config.cpp:113-122, :306-307, kMinFrameRate = 1, render_config.h:23); default 60 (render_config.h:53) | `present_frame_rate` (render_config.h:205-210): max_fps, or max(refresh, 60) for 0; with VSync on, min(rate, refresh). |
| `frame_generation` | 0..5 (:219-220, :323); 1 DLSS, 2 FSR 3 | While a vendor generator runs, `presented_max_fps` is 60 and native interpolation is off. |
| `frame_rate_decouple` | read, then forced false (:594-595, :670) | none (dead) |
| `dynamic_resolution` | bool, default false; the performance preset sets true and max_fps 0 (:616-636) | governor I.1 |
| benchmark | `apply_benchmark_presenting`: VSync 0, frame_generation 0, max_fps unlimited (:638-642) | no GPU budget for generated pictures (render_core.cpp:887-894) |

Reload: `reload_render_config_if_changed` (render_config.cpp:644-680) rereads the file when its write time changes; renderers call `poll_render_config_file` in begin_frame (B P-002). The render thread reads a thread-local copy per call (`get_render_config_chosen`, :415-425). V.

Interpolation on/off is not a key. It is `interpolated_frames_per_game_frame(present_frame_rate(...)) != 0` per renderer (renderer_d3d12.cpp:4839-4848; renderer_d3d11.cpp:640-644; renderer_vk_frame.cpp:3919; renderer_mtl_draw.mm:1123), decided again for every list (render_core.cpp:2589-2601). V for D3D12 and D3D11; the Vulkan and Metal bodies were located by grep, not read (S).

### B.2 Settings rows (V)

| Row | Where | Values |
|---|---|---|
| VSYNC | settings_display_page.cpp:96-105 | ON / OFF; fixed ON with "THIS DISPLAY ALWAYS WAITS FOR ITS REFRESH" when `present_waits_for_refresh`. The row offers only 0 and 1; 2 is reachable only by editing config.ini. |
| INTERPOLATION | settings_graphics_page.cpp:191-243 | OFF / NATIVE / DLSS FRAME GENERATION / FSR FRAME GENERATION. Hidden when only OFF exists (display ≤ 61 Hz). Switching sets max_fps 0 (on) or 60 (off) (:221). |
| FRAME RATE LIMIT (interpolation off) | :247-253 | 30 or 60 only (`kHalfStepRate`, `kStepRate`). |
| TARGET FRAME RATE (interpolation on) | :254-277 | `interpolation_targets` (settings_model.cpp:84-121): 75, 90, 100, 120, 144, 165, 200, 240, 280, 300, 360, 480, 500, 540, 600, 720, 1000 below the refresh (180 only for fixed DLSS multiples), then "MATCH REFRESH" (0), then UNLIMITED when VSync is off and not fixed. |

- `normalize_interpolation` (settings_model.cpp:140-168): off → 30 or 60; on with no targets → off; a target not offered → the highest offered at or below it. V.
- FSR frame generation offers only 120 (settings_model.cpp:89-92) and needs a refresh ≥ 119 Hz (:132). V.
- 72 Hz is not in the list; a 72 Hz display reaches it only as MATCH REFRESH. V.
- Caps come from `settings_frame_rate_caps` (native_settings.cpp:445-451): refresh from the chosen monitor, `unlimited = vsync_interval == 0 && !vsync_fixed`. V.

### B.3 Android display mode requests (V)

- On start, `applyDisplayRate(savedFrameRate())` (GameActivity.java:199-215, :271): for max_fps ≤ 61 the mode is left to the system; otherwise `preferredDisplayModeId` = the slowest mode at or above the rate, or the fastest for 0 and unlimited (DisplayRate.java:33-50). savedFrameRate reads config.ini and returns 60 on a missing file (:180-193).
- Surface vote (host_window_sdl.cpp:360-368): max_fps ≤ 61 → `ANativeWindow_setFrameRate(rate, FIXED_SOURCE)`; else `setFrameRateWithChangeStrategy(rate or panel fastest, DEFAULT, CHANGE_FRAME_RATE_ALWAYS)`. Applied when the setting changes (`sync_display_rate`, :375-384+).
- Android 15+: touch boost off (GameActivity.java:264-266).
- Apple and Windows request no display mode (grep for setFrameRate, preferredDisplayModeId, preferredFrameRate found only the Android sites and the CADisplayLink range in display_clock_apple.mm).

## C. Presentation per renderer and platform

| | D3D12 (Windows) | D3D11 (Windows) | Vulkan (Windows, Linux, Android) | Metal (macOS, iOS) |
|---|---|---|---|---|
| Swap chain | FLIP_DISCARD, 3 buffers (renderer_d3d12.h:1184), FRAME_LATENCY_WAITABLE_OBJECT, ALLOW_TEARING when supported (not on the FSR swap chain), SetMaximumFrameLatency(1) | kSwapChainBuffers, same flags (renderer_d3d11.cpp:276-297) | VSync on: FIFO, minImageCount + 1. Off: MAILBOX (3), else IMMEDIATE (3), else FIFO (renderer_vk.cpp:999-1014) | maximumDrawableCount 3; macOS displaySyncEnabled = VSync; iOS always synced (renderer_mtl.mm:298-307) |
| swap_queue | VSync → EveryPicture, else Tearing (renderer_d3d12.h:653-656) | same | FIFO → EveryPicture, MAILBOX → NewestPicture, else Tearing (renderer_vk.h:450-461) | same as D3D12 |
| Wait before present | latency object, 100 ms bound, only when VSync ≠ 0 (renderer_d3d12.cpp:4166-4379) | same (renderer_d3d11.cpp:669-674) | VK_KHR_present_wait on FIFO, not under frame generation: waits for the last present, or the one before the last when the picture has a present moment (renderer_vk.cpp:2239-2254) | `nextDrawable` wait; if it waited > 1.5 ms (kDrawableWaitMs, renderer_mtl.h:846) with VSync, the picture is dropped, never twice in a row (renderer_mtl.mm:1169-1194) |
| Sync interval | `present_sync_interval` (renderer_d3d12.cpp:4144-4164): with VSync 1 and no VRR, 2..4 when refresh/rate is within 1.5 %; a game picture without generated ones uses rate min(60, rate) | same (renderer_d3d11.cpp:621-638) | FIFO; FIFO_RELAXED per present when > 1.25 present periods late and the surface allows switching (renderer_vk.cpp:1058-1088, :2264-2278) | none |
| Present time | none (`set_present_moment` no-op, renderer_d3d12.h:658) | none (renderer_d3d11.h:318) | VK_GOOGLE_display_timing `desiredPresentTime = slot − vsync/2`, only with EveryPicture (FIFO), present step ≥ 15/16 vsync, and a grid (renderer_vk.cpp:2279-2327); feedback every 4th present (:2407-2409) into PresentSlots.observe (:2443-2446) | none ("never at a time asked for", renderer_mtl.mm:768-769) |
| Pacer (VSync off, finite cap) | PresentPacer, off under interpolation (renderer_d3d12.cpp:4198) | :667 | :2214-2216 | :733-735 |
| Low-latency API | Reflex sleep only under DLSS frame generation (renderer_d3d12.cpp:3622) | none | Streamline hooks only | none |
| Display time read | DXGI GetFrameStatistics only with timeline or present-stats diagnostics | none | past presentation timing on Android | `presentedTime` only with present-stats |

Platform clocks for slots (`vblank_grid`):
- Windows (all three renderers): the "Nebula Vblank" thread (vblank_clock_win32.cpp). V.
- Android: Choreographer expected presentation grid (display_clock.cpp:162-175). V.
- Linux: none (display_clock.cpp:193-211): slots use the free QPC clock at 1/rate, pictures go into FIFO, and the beat against the display is not handled. V.
- Apple: `vblank_grid` via host_window_apple.mm:1310-1349 from the display link's vsync time; `display_grid` (presentation times) always false (display_clock_apple.mm:224-234). V.

Only Vulkan on Android places pictures at a requested display time. Every other path presents "as soon as possible" after the render thread's slot wait. V.

## D. Scheduling of in-between pictures

### D.1 Sequence (render_core.cpp, V)

1. `start_interp_sequence` (:743-871) at the next step's present-only chunk:
   - `rate = max(61, present_frame_rate(...))` (:747-748).
   - `on_vblanks` = not Tearing, and (VSync on or rate finite), and a vblank grid (:756-757; uncommitted change, section 0).
   - On vblanks the rate is fitted to whole vblanks: `vblanks_per_slot` (:143-149), rate = refresh/slots (:759-766), again after the governors (:777-779).
   - `generated = interp_gpu_governor(rate)` (:776).
   - Slot period: vblanks·vblank period, or 1/rate, or 0 when unbounded (:793-796).
   - `locked` (lead 60/rate) only off vblanks with a finite rate within 2 % of a multiple of 60 (:797-798, :848).
   - Arrival = step moment (svc_frame_wait's due time) + delay. Delay = streaming 97th percentile of lateness + 0.2 ms, ≤ half a step, moving by +2 % / −1 % of a step per step (:809-833). Without a moment: smoothed prediction (:834-845).
   - First slot: on vblanks, the vblank nearest where the clock was due; unbounded: now; locked: the arrival; free: restart after a gap > 1 period (:849-865).
2. `advance_interp_sequence` (:1198-1270): `t = max(0, (slot − arrival)·60 + lead)`. A stall skips missed slots (`interp_late_slots_`). t < 0.999 and generated ≠ 0 → generated picture with `set_present_moment(slot)`; else the real picture at t = 1 and the sequence ends. "No count bounds them" (:1225-1226).
3. The next step's start supersedes the sequence (`supersede_interp_sequence`, :1160-1171): the list is walked once more at t = 1, unseen, so later reads (targets, temporal history) see its own time.

### D.2 What this gives (derived from D.1)

- On vblanks, `lead = 0` always (`locked` requires `!on_vblanks`). t is linear in slot time. The slot with t ≥ 1 for list N lies after the arrival of N+1, and N+1's sequence starts at its "seen" time, which is before its arrival whenever the lateness is below the delay (97 % by construction). So on vblanks the real picture of a step is shown only when the next step is late. Every presented picture is a blend; every step costs one extra unseen walk. This holds at 120 and 240 Hz too, not only at rates that are not multiples of 60. The code comment at :1254 ("Rates that are not multiples of 60 rarely present real frames") understates it. V for the rule; S for the frequency: settle with `--diagnostics present-stats` at 120 Hz VSync on (`superseded=` vs `sequences=` in `[interp-stats]`, :1319-1377).
- Off vblanks with a locked rate (VSync off on a tearing queue at 120/240; Linux at 120/240 with any VSync): k generated at i/(k+1), then the real picture on time. V.
- Off vblanks, not locked (Linux at 144/165; VSync off and tearing at 144/165): free QPC clock at 1/rate, lead 0, superseded at the next arrival. V.

### D.3 t patterns (arithmetic from the cited rules)

| Case | Slot period | t step per slot | Pattern |
|---|---|---|---|
| 144 Hz, VSync on (Windows, Android with grid) | 1 vblank, 6.94 ms | 0.4167 | e.g. 0.20, 0.62, (1.03 superseded) → next sequence 0.03, 0.45, 0.87, ...; 2 or 3 pictures per step, 2.4 on average; phase drifts by 0.4·step each step |
| 165 Hz, VSync on | 1 vblank, 6.06 ms | 0.3636 | 2 or 3 per step, 2.75 average |
| 120 Hz target on a 144 Hz display | 2 vblanks (`ceil(1.2 − 0.02) = 2`), 13.9 ms | 0.833 | 72 pictures/s, not 120 |
| 120 Hz, VSync on | 1 vblank | 0.5 | two blends per step, phase fixed by the delay; real picture only after a late step |
| 120 Hz, VSync off, tearing | 8.33 ms, locked, lead 0.5 | 0.5 | 0.5 (generated), 1.0 (real) |
| VSync off, MAILBOX, finite rate (Vulkan) | on vblanks since the uncommitted change | as VSync on | as VSync on |
| Unlimited, VSync off | none (`interp_slot_unbounded_`) | t = (now − arrival)·60 at each draw | pictures back to back; t increments depend on walk time |
| Unlimited with VSync on | `on_vblanks` true when VSync is on (:757), rate = refresh | 1 vblank | as "MATCH REFRESH" |

### D.4 Governors (V)

- GPU governor `interp_gpu_governor` (:878-933): budget 0.9 × 16.67 ms per step. Every 30 sequences after 1 s warm-up: `fit = floor(budget / generated_ms) − 1` (− 2 with a temporal upscaler). The cap drops at once; rises by one step after 120 sequences (600 temporal). Temporal with fit < 1 → `suspend_temporal_interpolation`, which makes `interpolation_mode()` false (renderer_d3d12.cpp:4844-4846), so interpolation stops until a key change (renderer_d3d12.cpp:3633-3635). The 1200-sequence re-probe (:920-924) cannot run then, because no sequence starts while `interpolation_mode()` is false (render_core.cpp:2591-2592). B's P-174 is confirmed.
- Cadence governor `note_game_frame_cadence` (:964-1105): 1 s windows; a miss = step interval > 1.5 periods; generated misses ≥ 2/s and ≥ own misses → one rate step down (half at ≥ 8/s); own misses probed one step lower for 1 s; tries up every 2-64 s (doubling on failure); a pause > 4 s restarts the window.
- A capped count changes the rate to 60·(k+1) and, on vblanks, re-fits it to whole vblanks (:777-779): a cap of 1 at 144 Hz gives 72 pictures/s (2 vblanks per slot), not 120.

### D.5 VSync off, Unlimited, VRR, suspend and resume (V unless marked)

- VSync off, D3D12/D3D11/Metal (tearing): slots on the free clock; presents tear at the slot time.
- VSync off, Vulkan MAILBOX: on vblanks (uncommitted change); no display timing (MAILBOX is not EveryPicture).
- Unlimited (VSync off): no slot clock; each picture is walked as soon as the last one finished, t from now; the cadence and GPU governors still cap the count per step.
- VRR: see A.6. With VSync on and VRR detected, D3D12/D3D11 sync interval stays `configured` (renderer_d3d12.cpp:4146-4148); the slots still use the vblank thread's grid, which on a VRR panel follows the presents (S).
- Window hidden, minimised or occluded: sequence superseded, occlusion probe every 100 ms (render_core.cpp render_loop, :1694-1956 region). `interp_stopping_` sets the configured count to 0 (:880). On resume the next sequence restarts the slot clock (gap > 1 period, :859-865) and the history resets (`interp_walk_time`, :1418-1439).
- Settings or HOME overlay with no sequence: `overlay_idle_presents` (:1180-1184) presents at the slot rate when it is above 61 Hz.
- Camera cut (`NEBULA_FRAME_CAMERA_CUT`) or a heuristic cut: the pair is shown as b; temporal history reset (draw_walker.cpp:1497-1499).

## E. What is blended per in-between picture

| Data | How | Where (V) |
|---|---|---|
| Scene / mirror / shadow cameras | quaternion slerp of rotation, lerp of eye position; cut (> 60°, or > 1500 units and > 6× last move + 150) or game-declared cut → b | frame_interpolation.cpp:132-207; pairing.cpp:299-404 |
| OTHER-role cameras | paired by nearest view matrix (≤ 65536 pairs), then as above | pairing.cpp:37-242; frame_core.h:507 |
| World joints (blocks) | polar decomposition: R slerp, S lerp, translation lerp (chord); teleport (moved > 0.5·eye distance + 200 or scale > 8×) → b; flip (> 150°) keeps b's rotation; identical words held | affine.cpp:90-298; frame_interpolation.cpp:104-122; pairing.cpp:299-404 |
| Eye-space blocks (calc_mode 1, camera NONE) | per joint EyeMotion: split into camera × world when the camera is known and not orthographic, else eye-space | frame_interpolation.cpp:209-269 |
| calc_mode 2 base | element-wise lerp | draw_walker.cpp:177-489 |
| Skinned positions | not blended; envelopes are summed from interpolated joints | compose.cpp |
| Lights | colour lerp (clamped 0-255), 6 attenuation terms lerp, position and direction lerp in world (follow bit) or eye space; cut → b | frame_interpolation.cpp:304-373 |
| Sky up | rotated by the camera at t | draw_walker.cpp:1456-1465 |
| Unregistered material rows | element-wise lerp when the pair has the same row counts and coordinate-row list and no cut; texture matrices via `tex_matrix_at` (scroll unwrap, step → b) | draw_walker.cpp:664-709; frame_interpolation.cpp:271-302 |
| Registered material slots | not blended (slot rows as listed) | draw_walker.cpp:641-643 |
| Ring vertices (particles excluded) | positions lerped when paired and the shape matches (vertex count, index count, stride); the WIDE second position too; cut → b | draw_walker.cpp:177-489; pairing_tests.cpp:74-84 |
| Instance rows | held from b | draw_walker.cpp:177-489 |
| Projection (6 terms) | lerped when paired, same orthographic flag, no cut | draw_walker.cpp:177-489 |
| Textures | b (a flipbook keeps its pair) | pairing.cpp; pairing_tests.cpp:36-38 |
| DISCRETE_VERTICES items | never paired → b | pairing.cpp; pairing_tests.cpp:22-29 |
| Unpaired items (new this step) | b, eye-space ones held still in the world | frame_interpolation.cpp:209-259 |
| Mod post programs | time = (game_frame − 1 + t)/60 | draw_walker.cpp:2070-2073 |
| Native mod calls | the camera at t, `game_frame` of the list | draw_walker.cpp:2111-2172 |
| Screen probes | only on the real walk | draw_walker.cpp:1911-1912, :2039-2046 |
| Render targets, frame copies, bloom | redrawn per picture into a separate key (`kGeneratedPicture`) | draw_walker.cpp:1614-1634, :1970-2013, :2034-2038 |
| Star pointer, HUD, menus | the game's own items from the list; blended only if they pair like any item. No present-time pointer: grep for pointer_override, late_pointer, cursor_now, pointer_now, latest_pointer, sample_pointer, and "pointer"/"cursor" in runtime/src/render/core found none. | V (search) |

## F. Latency

- Interpolation off: list N is walked and presented at once (`frame_end_present`, render_core.cpp:2650-2653). V.
- Interpolation on: list N waits for the present-only chunk of step N+1 (render_core.cpp:2615-2631), which `svc_frame_begun` sends after N+1's `update` and `calcAnim` (GameSystem.cpp:182-206). Its first picture then shows t ≈ 0 (list N−1's content); t reaches 1 one step after the arrival (moment N+1 + delay). V.
- So, against interpolation off, list N's content appears blended from the arrival of step N+1 and is fully shown about one step later: about one to two steps (16.7-33 ms) more from input to the picture that shows it, plus the delay (≤ 8.3 ms, usually the 97th percentile of the game's update time + 0.2 ms). Derived from the code; S for the measured value: `--diagnostics timeline` (StepWaitBegin/End, GameFrame, ContentTime, PresentEnd events, frame_timeline.h:26-50).
- Input is read once per step in `update` (PlayerInputs.cpp:93); the pointer is a list item (E). Nothing is sampled at present time. V (search).
- Swap chain depth on top: D3D12/D3D11 max frame latency 1 with VSync; Vulkan present wait keeps one picture queued (two with a present moment); Metal up to 3 drawables minus the drop rule. V.

## G. Cost per in-between picture

Per generated picture the render thread does the whole walk again (V):
- `begin_walk_frame` (render_core.cpp:2212-2518): config copy, ring sizing over all blocks, `renderer_.begin_frame` (config poll, frame generation checks), display pictures, texture store, video-memory enforcement, texture-pack and movie checks, pipeline settings.
- `prepare_walk` (draw_walker.cpp:177-489): pairing reused, but per picture: cameras at t, joints_at and compose for every block (envelope skinning on the CPU), normal matrices, lights, memcpy of all rows, ring vertex lerp, index copy, view terms.
- Material rows: every unregistered constants block lerped once per walk (draw_walker.cpp:664-709).
- Every pass recorded again: target passes, frame copies, bloom, mod posts, the scene, the 2D layer (draw_walker.cpp:1890-2060).
- GPU: the whole frame again, at the dynamic-resolution size.
- Plus one unseen t = 1 walk per step whenever the sequence is superseded (D.2): on vblanks this is every step.

So CPU and GPU cost grow with the number of draws × pictures per step. There is no reuse of the real frame's command lists or a 2D warp. B's P-156 is confirmed and extends to target passes and bloom. Per-picture cost is measured only as `wall-ms-per-generated` and `gpu-ms-per-generated` in `[interp-stats]` (render_core.cpp:1357-1358). S for the numbers: a present-stats run at 144 Hz.

## H. Rate dependence left (things counted in pictures, presents or sequences)

| Counter | Counts | Where | At 240 Hz |
|---|---|---|---|
| PresentSlots lateness window | 60 presents | present_slots.cpp:55-99 | 0.25 s (B P-041 correct here) |
| PresentSlots calm stretch | 300 presents, doubling to 4800 | present_slots.cpp | 1.25 s to 20 s |
| Android present-timing read | every 4th present | renderer_vk.cpp:2407 | 60/s (reads the driver's last 64) |
| D3D12 DWM timeline sample | every 30th present (diagnostic) | renderer_d3d12.cpp:4166-4379 | 8/s |
| Apple lateness log | every 600 waits | display_clock_apple.mm | game steps, not pictures |
| host display_refresh | every 30th call, once per step | host_main.cpp:1037-1060 | 2/s (not rate dependent; B P-041's "8 times a second at 240 Hz" is wrong) |
| Dynamic resolution settle / up | 20 pictures and 200 ms; 60 pictures and 1 s | resolution_governor.h | the time rule dominates above 100 Hz |
| Dynamic resolution update | once per scene picture, generated ones too | renderer_d3d12.cpp:1487-1497 | 240/s |
| GPU governor | 30, 120, 600, 1200 sequences | render_core.cpp:896-924 | sequences are steps: not rate dependent |
| Metal heap trim | 600 frames | texture_backend_mtl.mm:408-409 (B, not re-read) | S |
| `enforce_video_memory` | per walk; its log counts list serials | render_core.cpp:2184-2210 | per picture |
| `[interp-stats]` | every 2 s on the clock | render_core.cpp:1327 | time based |
| GPU memory ageing | 600 game steps | idle_clock.h | step based (correct) |

## I. Dynamic resolution and thermal rules

### I.1 Dynamic resolution (V)

- `resolution_governor.h` (146 lines) and `dynamic_resolution.h` (87 lines), header only.
- Steps in sixteenths 16..8; floor = the upscaler's minimum.
- Settle 20 pictures and 200 ms; down when the GPU is above 90 % of the budget for 3 pictures and 50 ms, to the largest step fitting 80 %; up one step when the next step's estimate stays under 75 % for 60 pictures and 1 s.
- Budget = 1000 / present rate; Unlimited → the refresh; else 60 (`resolution_budget_ms`, dynamic_resolution.h:23-30).
- Called once per scene picture (`begin_scene_rectangle`, renderer_d3d12.cpp:1487-1497; renderer_d3d11_scene.cpp:713-717; renderer_vk_frame.cpp:3156-3160; renderer_mtl_scene.mm:990-994) with `native_scene_gpu_ema_ms_`.
- Interplay: dynamic resolution aims at one picture in 1/rate; the interpolation GPU governor aims at all generated pictures in 0.9 × 16.67 ms. They read different GPU averages and do not know of each other. Which acts first after a load spike is not decided anywhere. S: settle with a present-stats run with dynamic resolution on at 144 Hz (`[dynres]` and `[interp-stats] gpu-generated-cap`).

### I.2 Thermal and power (V by search)

- No runtime thermal rule. Searched `thermal|Thermal|PowerManager|getThermalHeadroom|AThermal` and `headroom|sustained|temperature` over .h, .cpp, .mm, .java, .kt: only the benchmark's slowdown report (benchmark.cpp:1375-1394) and an unrelated comment.
- Android: ADPF performance hint (perf_hint.cpp): target starts at one step; `report` sends the largest thread CPU-time delta, not wall time (B P-069 confirmed); `perf_hint_set_target` updates the target.
- Windows: battery look every 2 s; EcoQoS opt-out and 1 ms timer for the render thread (render_core.cpp:1515-1542).
- Apple: QOS_CLASS_USER_INTERACTIVE for registered threads.

## J. Tests

### J.1 frame_interpolation_tests.cpp (246 lines, V)

- presentation_grid_at: 4 checks of display phase (:220-223).
- internal_extent: internal height and width follow the preset and aspect (:225-231).
- test_occlusion_budget_and_sky_direction: AO divisor budget; sky up rotates with the camera; round trip; a view without inverse leaves the direction (:195-215).
- test_tex_matrices: spatial matrix lerps whole; stepping coordinate matrix shows b; a scroll wraps (:145-160).
- test_camera_endpoints_and_arc: t = 0 is a, t = 1 is b; a turn keeps distance (arc) (:49-66).
- test_camera_cut: 115° is a cut; a cut shows b; 5000 units from rest is a cut; after a 4000 move it is motion (:68-80).
- test_static_object_under_moving_camera: split motion keeps a static object fixed in the world at t = 0, 0.3, 0.7, 1; unmatched record stays in the world (:82-106).
- test_moving_object_and_teleport: halfway translation and rotation; a 3000-unit jump is a teleport and shows b; a 2× pulse interpolates; a 170° spin is a flip, position and scale lerp, b's rotation (:108-143).
- test_light: static light stays in the world; colour lerps; no history keeps b in the world; unsplit at t = 1 is b (:162-193).

### J.2 present_slots_tests.cpp (247 lines, V)

- check_swap_queues: `presents_timed` only EveryPicture; `pictures_on_refreshes` all but Tearing; a newest-picture queue shows ≥ 1080/1200 untimed and loses most when timed (:185-227).
- check_rate at 59.94, 60, 72, 75, 90, 100, 119.88, 120, 143.98, 144, 165, 170, 180, 185, 200, 240, 360 Hz, slots of 1 and 2 vsyncs, steady, varying and spiking load: no uneven picture away from a hitch; none at all under steady or varying load; no depth change in the last 30 s; depth ≤ 4 steps + 1 vsync; steady depth down to what the GPU allows (:135-160, :231-240).
- check_frames at 60, 120, 180, 240: game frames one period apart (:162-183).
- Simulated display only; feedback lag fixed at 3 presents.

### J.3 frame_phase_tests.cpp (154 lines, V)

- test_counts: 60, 59.94, 120, 240 fit; 144, 75, none do not (:53-62).
- test_settles: at 60, 59.94, 120 with 1, 3, 5 ms work the present lands just before a refresh and the step keeps the display period (:64-75).
- test_free_running: no display or 144 Hz → the plain step (:77-81).
- test_budget: budget falls to work + margin, rises at once, ignores a 500 ms stall, capped at a frame; a start moves ≤ 0.5 ms (:83-101).
- test_falls_back: a miss → plain for 600 frames, then back; 13 ms work → plain (:103-128).
- test_in_step: DWM readings in and out of step (:130-137).

### J.4 pairing_tests.cpp (111 lines, V)

- A camera-relative static model at t = 0.5 stays in the world within 0.02 (:9-21).
- DISCRETE_VERTICES item does not pair; its camera does (:22-29).
- Continuous ring item pairs (:30-31).
- Duplicates pair one to one (:33-35).
- A texture change keeps the pair (:36-38); another mesh does not (:39-41).
- Shadow camera pairs by role after reordering (:43-47).
- Light sets: wrong camera rejected, right accepted, grown sets with the same follow bits pair, changed follow bits do not (:49-64).
- Ring items pair by part number (:66-72); `ring_shape_matches` needs the same vertex count, index count, stride and RING flag (:74-84).
- OTHER cameras pair by nearest view; one fewer; identical views in order (:86-104).

### J.5 precise_wait_tests.cpp (53 lines, V)

- Margin rules: up at once, held to the most, down slowly to the least (:20-34).
- Real waits: 20 waits of 2 ms never early; a past moment returns at once (:35-50). This test sleeps on the real clock.

### J.6 frame_telemetry_tests.cpp (53 lines, V; Windows only)

- Publisher IPC objects exist; header, identity, counts and display settings correct; a held reader never blocks the writer; publication resumes; shutdown clears the valid word (:13-50).

### J.7 Other checks

- tools/verify/interp_real.sh (35 lines, V): runs a tour at 60 and at 120 fps with `GALAXY_PRESENT_SNAPSHOT_GAME_ONLY=1` and compares the real pictures; generated pictures are not compared.
- settings_model_tests.cpp:23 checks 60 Hz offers no interpolation (seen by grep only).

### J.8 Untested behaviour

- `start_interp_sequence`, `advance_interp_sequence`, `supersede_interp_sequence`, the arrival clock and delay, `vblanks_per_slot`, `interp_gpu_governor`, `note_game_frame_cadence`, `interp_walk_time`: no unit test (grep of the test files for these names: none; S that no other test file calls them, checked only in the six files above and settings tests).
- `sdl::wait_display_frame` 3 % lock, Android Choreographer smoothing, Apple display link: no test (B notes the same).
- `next_variable_refresh`, `variable_refresh_cap`, `PresentPacer`: not in the six files.
- `present_sync_interval` (D3D12/D3D11), Metal drawable drop, Vulkan present wait and FIFO_RELAXED switching: no test.
- Dynamic resolution governor and its interplay with interpolation: not in these files.
- draw_walker blending (material rows, ring vertices, view terms, lights through split): no test beyond the pure functions.
- Generated pictures' image content: no capture compare (interp_real.sh leaves them out).

## K. Telemetry

| Source | When | What | Where (V) |
|---|---|---|---|
| Frame telemetry shared memory | Windows only, `--diagnostics telemetry`, once a second | pictures made, new shown, presented, shown again, last picture, stamp, slot, last made and shown times (steady clock); no display time | frame_telemetry.h/.cpp; tools/telemetry/nebula_frame_telemetry.py reads copy_hz, first_return_hz, game_return_hz, repeat_hz |
| Frame timeline | Windows only (`now()` 0 elsewhere), 4M records, dumped at exit | Submit, Render, GPU, Present, DisplayStats (DXGI SyncQPCTime), VBlank (DWM), SlotWait, GameFrame, StepWait, Walk (b = 1 generated), ContentTime (slot, game frame, t·1e6) | frame_timeline.h:26-50; render_core.cpp:1227-1233 |
| `[interp-stats]` | `--diagnostics present-stats`, every 2 s | generated per frame, presented Hz, late slots, vblank steps histogram, slot-to-vblank min/max, superseded, history resets, cuts, teleports, flips, wall and GPU ms per generated picture, caps, game fps, step delay, ring peaks | render_core.cpp:1319-1390 |
| `[interp-coverage]` | same | per category: items, new, missed, ring at b, discrete | :1378-1390 |
| `[present-timing]` | Android Vulkan with display timing, every 10 s | on slot, late, unrequested, even intervals, actual Hz, margin, depth | renderer_vk.cpp:2416-2483 |
| `[mouse-response-latency]` | `--diagnostics pointer-latency`, every 2 s | selected pad sample to the first present return; "physical-display-time=unmeasured" | render_core.cpp:469-515 |
| Production cadence | monitoring rows | picture made / first present intervals, percentiles | render_core.cpp:440-466 |
| `[pacing]` | SDL, every 600 steps | step lateness and rate | host_main.cpp:1103-1179 |

No path measures when a picture reached the screen except Android display timing, D3D12 DXGI statistics (diagnostic), and Metal `presentedTime` (diagnostic). The telemetry counts pictures; it does not count how many refreshes each was on screen except the D3D12 PresentRefreshCount histogram and Metal's refresh count bins (diagnostic).

## L. Findings

Target properties used below: (T1) exactly 60 steps/s; (T2) even spacing on the refreshes at any rate, VRR included; (T3) no per-draw CPU growth per in-between picture; (T4) fresh camera and pointer; (T5) nothing counts pictures; (T6) gentle degradation; (T7) the same on every platform.

**I-001** wrong behaviour (T1). The game step follows the display, not 60 Hz, whenever the display is near a multiple of 60.
- What: Windows locks the step to `count × refresh` when within 1 %; Android and Apple when within 3 %. At 59.94 or 119.88 Hz the game runs at 59.94 steps/s on all three; at 58.5-61.5 Hz on Android and Apple it runs up to 2.5 % off. Linux always runs exactly 60.
- Evidence: frame_phase.cpp:9-15, :36-46; host_main.cpp:1063-1072; display_clock.cpp:119-160; display_clock_apple.mm `wait_display_frame`; display_clock.cpp:193-211 (Linux none).
- Who: every Windows, Android and Apple player on a 59.94/119.88/239.76 Hz display (TVs, many monitors), and Android/Apple panels within 3 %.
- Confidence: verified in code. B P-003 and P-033 are correct.

**I-002** design gap (T3, T6). On vblanks, the real picture of a step is almost never shown, and every step costs one extra unseen walk.
- What: with slots on vblanks `lead` is 0, and the next step's sequence starts before the slot where t would reach 1, so `supersede_interp_sequence` walks the list at t = 1 without presenting it. At 120 Hz VSync on, a step costs 3 walks for 2 presents; at 144 Hz about 3.4 walks for 2.4.
- Evidence: render_core.cpp:797-798 (locked needs !on_vblanks), :848, :1212-1213, :1160-1171, :1254; D.2.
- Who: every renderer with VSync on (Windows, Android, Apple), and Vulkan MAILBOX since the uncommitted change.
- Confidence: verified in code for the rule; how often the real picture shows is suspected. Run: `--diagnostics present-stats` at 120 Hz VSync on; compare `superseded=` with `sequences=` and `presented-hz`.

**I-003** performance (T3). Each in-between picture is a full CPU walk and a full GPU frame.
- What: begin_walk_frame, prepare_walk (compose, skinning, lights, rows), material lerp, ring vertex lerp, every pass (targets, copies, bloom, mod posts, scene, 2D) again. CPU per picture grows with the draw count; no reuse of the real frame's work.
- Evidence: render_core.cpp:2212-2518, :2521-2565; draw_walker.cpp:177-489, :664-709, :1614-1634, :1890-2060.
- Who: phones and integrated GPUs first; any machine at high rates.
- Confidence: verified in code. Cost numbers: suspected; run present-stats at 144 Hz and read `wall-ms-per-generated` and `gpu-ms-per-generated`. B P-156 is correct and incomplete (targets and bloom are also redrawn).

**I-004** wrong behaviour (T4). Interpolation adds about one to two steps of latency, and neither camera nor pointer is sampled at present time.
- What: list N waits for step N+1's frame_begun; its content is fully shown about one step after that. The pointer and HUD are list items, blended like any item.
- Evidence: render_core.cpp:2615-2631, :730-741, :809-833; GameSystem.cpp:182-206; search in E (pointer row).
- Who: every player using interpolation; most visible with the Wii-remote pointer and mouse.
- Confidence: verified in code (path and absence by search). Size: suspected; run `--diagnostics timeline` and `pointer-latency` at 60 and 144 Hz. B P-078 is correct; its open question "whether the 2D pointer layout is interpolated at all" is answered: it is a list item and is blended when it pairs.

**I-005** wrong behaviour (T2, T7). Linux has no display clock.
- What: no vsync clock, no vblank grid, no present times. Slots run on the QPC clock at 1/rate and go into FIFO, so they beat against the display (two pictures in one refresh, then a repeat), which the uncommitted change was written to avoid elsewhere.
- Evidence: display_clock.cpp:193-211; host_window_sdl.cpp:1760-1819; render_core.cpp:756-757.
- Who: Linux players with interpolation on.
- Confidence: verified in code; visible judder suspected; run on Linux at 144 Hz with present-stats (`vblank-steps` histogram stays empty without a grid, so a camera capture is needed).

**I-006** wrong behaviour (T2), suspected. Metal drops a picture when `nextDrawable` waited more than 1.5 ms.
- What: the drop rule (renderer_mtl.mm:1189-1194) applies to generated pictures. A dropped generated picture leaves a refresh showing the previous one: one uneven step. Metal has no present time either (:768-769).
- Who: macOS and iOS with interpolation.
- Confidence: verified in code for the rule; how often it fires under slot pacing is suspected. Run: macOS with present-stats (`pictures_held_` and the `refreshes` bins).

**I-007** wrong behaviour (T2), suspected. VRR detection and slot timing.
- What: VRR counts only when the measured vblank rate drops below 0.8 × the mode; with in-between pictures near the mode rate it is not detected. When it is, slots still follow the vblank thread, whose grid on a VRR panel follows the presents.
- Evidence: renderer_common.cpp:290-399 (`next_variable_refresh`); renderer_d3d12.cpp:3882-3920; vblank_clock_win32.cpp.
- Who: G-Sync / FreeSync / VRR TV users on Windows.
- Confidence: suspected. Run: a VRR monitor at 144 Hz with the timeline (VBlank against PresentEnd) and the `variable refresh` state.

**I-008** wrong behaviour (T6). Rates fit to whole vblanks halve the rate instead of degrading gently.
- What: `vblanks_per_slot = ceil(refresh/rate − 0.02)`. A 120 Hz target on a 144 Hz display gives 72 pictures/s. A GPU or cadence cap of 1 at 144 Hz (rate 120) gives 72 pictures/s too.
- Evidence: render_core.cpp:143-149, :759-766, :777-779, :931.
- Who: 144/165 Hz displays with a lower target or a capped GPU.
- Confidence: verified in code (arithmetic).

**I-009** wrong behaviour (T6). A temporal-upscaler suspend turns interpolation off for the session.
- Evidence: render_core.cpp:903-907; renderer_d3d12.cpp:4844-4846, :3633-3635; render_core.cpp:2591-2592 (no sequence starts, so the 1200-sequence probe at :920-924 never runs).
- Confidence: verified in code. B P-174 is correct.

**I-010** design gap (T6), suspected. Two governors act on the same GPU time without knowing of each other.
- What: dynamic resolution aims each picture at 1/rate (per scene picture, generated ones included); the interpolation GPU governor aims all generated pictures at 0.9 × 16.67 ms per step and reads a different average.
- Evidence: renderer_d3d12.cpp:1487-1497; dynamic_resolution.h:23-30; render_core.cpp:878-933.
- Confidence: suspected. Run: dynamic resolution on, 144 Hz, a heavy galaxy; read `[dynres]` and `[interp-stats] gpu-generated-cap`.

**I-011** console-era remnant (T5). Counts in presents and pictures remain.
- What: PresentSlots windows (60 presents) and calm (300-4800 presents); Vulkan timing reads every 4th present; dynamic resolution settle and up counts (20 and 60 pictures, with a time floor); D3D12 DWM timeline every 30th present (diagnostic).
- Evidence: H.
- Confidence: verified in code. The host's DWM read counts steps, not pictures (correction to B P-041).

**I-012** design gap (T7). Only Android Vulkan presents at a requested time; only Windows has frame telemetry and the timeline.
- Evidence: C table; frame_telemetry.h; frame_timeline.h.
- Confidence: verified in code.

**I-013** wrong behaviour (minor), suspected. FramePhase measures the wrong work under interpolation.
- What: `work = g_last_present_ns − previous step start` (host_main.cpp:1063-1072). `g_last_present_ns` is written only for real pictures (render_core.cpp:524-529, :2551-2553). Under interpolation the real present of a list comes about two steps after its step, or never (I-002), so `work` is either the arrival delay or 0 (budget left as it was). The phase then aims at `vblank − (delay + 1.5 ms)`, not at the step's own work.
- Who: Windows at 120/240 Hz (the only locked rates) with interpolation.
- Confidence: suspected; run the timeline at 120 Hz with interpolation and compare StepWaitEnd phase against VBlank.

**I-014** wrong behaviour (T2), suspected. Game-declared camera cuts come from two places only.
- What: `MotionSignal::cameraCut` is called from `CameraViewInterpolator::reduceOscillation` on a forced camera change (CameraViewInterpolator.cpp:207-210) and from camera animations at cut frames (CameraAnim.cpp:131-133). Every other jump relies on the heuristic (> 60°, or > 1500 units and > 6× the last move + 150). A cut to a nearby angle below those limits is blended for one step (a smear across the cut). `camera_epoch` is written to the list (FrameList.cpp:527) and read by nothing in the host (grep `camera_epoch`: only nebula_frame_list.h:439 and FrameList.cpp).
- Confidence: verified in code for the sources; the visible effect is suspected (run present-stats and compare `camera-cuts/game-cuts`; capture generated pictures at a door or pipe transition).

**I-015** maintainability. Stale or wrong comments and dead parts.
- frame_interpolation.cpp:99 says "2x"; the code uses 8× (64 on squared norms, :104-122). V.
- interp_types.h: an empty namespace with a truncated comment (11 lines). V.
- render_core.cpp:1695 says "Called with queue_mutex_ held"; the function takes it. V.
- host_main.cpp:1099-1102 says the step is 60 Hz; see I-001. V.
- render_core.cpp:1254 says only non-multiple rates rarely present real frames; see I-002. V.
- `frame_rate_decouple` and `idle_present` dead (B P-001 correct). V.

**I-016** gap in tests. The scheduler is not under test (J.8): the arrival clock, `vblanks_per_slot`, both governors, supersede, `interp_walk_time`, the platform clocks' lock rules, `present_sync_interval`, the Metal drop rule and VRR detection. present_slots_tests covers only Android's display-timing controller. V (by the reads in J).

## Could not be determined (and what would settle it)

- How often real pictures are shown on vblanks (I-002): present-stats at 120 and 144 Hz, VSync on.
- Actual latency added by interpolation (F, I-004): timeline at 60 and 144 Hz, StepWaitEnd → ContentTime → PresentEnd → DisplayStats.
- VRR detection at high present rates and slot feedback (I-007): VRR monitor run with the timeline.
- Linux visible judder (I-005): Linux at 144 Hz, camera capture.
- Metal drop frequency under slots (I-006): macOS present-stats.
- Cost per generated picture on the phone and the PC (G): present-stats `wall-ms-per-generated`, `gpu-ms-per-generated`.
- Bodies of `RendererVk::interpolation_mode` (renderer_vk_frame.cpp:3919) and `RendererMtl::interpolation_mode` (renderer_mtl_draw.mm:1123): not read; assumed to match D3D12 per B P-174's citation.
- draw_walker.cpp 809-1439 and 1640-1889 (texture tables, item recording, early planners): not read in this pass; B R3.1 reports them read.
- Mod camera effects (LumaDrawView) and movie timing against audio: not read.
- Whether Android throttles Choreographer callbacks in the background (B P-004): device run.

## Statements in B-findings-port.md found wrong or incomplete

1. P-041: "At 240 Hz with interpolation the DWM timing query runs 8 times a second instead of 2." Wrong. `display_refresh()` is called from `svc_frame_wait`, once per game step (host_main.cpp:1037-1060, :1103-1179), so `frames++ % 30` is twice a second at any display rate. The PresentSlots part of P-041 is correct.
2. P-078: open question answered. The pointer is a list item; generated pictures blend it when it pairs (E). No present-time path (search confirmed).
3. P-156: incomplete. Besides compose and material rows, every target pass, frame copy, bloom and mod post is recorded again per picture, and an extra unseen t = 1 walk runs each step on vblanks (I-002, I-003).
4. P-174: correct; add that the 1200-sequence re-probe cannot run once suspended because no sequence starts (render_core.cpp:2591-2592).
5. R10.2 (Vulkan present): now outdated by the uncommitted change: Android display timing is restricted to FIFO (`presents_timed`), and the pacer is skipped under interpolation on Vulkan and Metal (section 0).
6. P-003: correct; add that the Windows lock applies with VSync off too (FramePhase reads DWM regardless of VSync, frame_phase.cpp:36-46), and that Linux never locks.
7. P-069: correct (perf_hint.cpp reports the largest thread CPU-time delta).
8. P-001, P-002, P-004, P-005, P-021, P-033, P-042: consistent with what was read here; P-005's FrameList.cpp acquire was not re-read in this pass.

## Coverage

"Full" means every line was read.

| File | Lines read | Lines not read |
|---|---|---|
| runtime/src/render/render_core.cpp (2898) | 1-1966, 2004-2720 | 1967-2003, 2721-2898 |
| runtime/include/galaxy/render/render_core.h (1126) | 60-180, 800-920, 985-1060; diff | rest |
| runtime/src/render/core/draw_walker.cpp (2213) | 1-808, 1440-1639, 1890-2213 | 809-1439, 1640-1889 |
| runtime/src/render/core/pairing.cpp (431) | full | - |
| runtime/src/render/core/compose.cpp (248) | full | - |
| runtime/src/render/frame_interpolation.cpp (393), frame_interpolation.h (92) | full | - |
| runtime/src/render/affine.cpp (300) | 60-300 | 1-59 |
| affine.h (44), interp_types.h (11) | full | - |
| present_slots.h (111), present_slots.cpp (101) | full; diff | - |
| frame_phase.h (47), frame_phase.cpp (49) | full | - |
| precise_wait.h (147), game_step.h (14), present_clock.h (8), idle_clock.h (44), display_timeline.h (12) | full | - |
| frame_telemetry.h (36), frame_telemetry.cpp (126), frame_timeline.h (177) | full | - |
| perf_hint.cpp (241) | full | - |
| platform/sdl/display_clock.cpp (213) | full | - |
| platform/apple/display_clock_apple.mm (252) | full | - |
| vblank_clock_win32.cpp (197) | full | - |
| resolution_governor.h (146), dynamic_resolution.h (87) | full | - |
| render_config.h (514) | 20-130, 195-445; diff | rest |
| render_config.cpp (997) | 108-127, 415-454, 580-674; grep lines 188-220, 305-323, 405-412, 718-742 | rest |
| renderer_common.h, renderer_common.cpp | 40-216; 290-399 | rest |
| renderer_d3d12.cpp (7501) | 1480-1509, 3140-3219, 3600-3659, 3870-3929, 4140-4379, 4836-4851 | rest |
| renderer_d3d11.cpp (1396) | 612-704; grep 276-307 | rest |
| vk/renderer_vk.cpp (3158) | 990-1164, 2200-2499, 2703-2730; diff | rest |
| mtl/renderer_mtl.mm (1244) | 285-314, 715-784, 1150-1208; diff | rest |
| renderer headers (swap_queue, set_present_moment) | grep lines; D3D12 diff | rest |
| native/host/host_main.cpp (6578) | 975-1215, 1940-1979, 2640-2715, 3060-3139; diff | rest |
| platform/sdl/host_window_sdl.cpp | 340-384, 1760-1819; diff | rest |
| platform/apple/host_window_apple.mm | 1310-1349; diff | rest |
| native/petari/src/Game/System/GameSystem.cpp (360), NebulaFrameClock.cpp (91) | full | - |
| native/petari/src/Game/Render/FrameList.cpp | 490-549, 1050-1094, 1340-1379 | rest |
| native/petari/src/Game/Util/MotionSignal.cpp (140) | full | - |
| CameraViewInterpolator.cpp | 180-224 | rest |
| CameraAnim.cpp | 115-144 | rest |
| Material.cpp | 975-1049 | rest |
| Model.cpp | 400-419, 615-654 | rest |
| native/include/nebula_frame_list.h | 380-419 | rest |
| ui/settings_model.cpp | 60-179 | rest |
| ui/settings_model.h | 40-73 | rest |
| ui/native_settings.cpp | 436-465 | rest |
| ui/settings_graphics_page.cpp | 185-303 | rest |
| ui/settings_display_page.cpp | 80-119 | rest |
| android GameActivity.java | 170-274; diff | rest |
| android DisplayRate.java (52) | full | - |
| frame_interpolation_tests.cpp (246), present_slots_tests.cpp (247), frame_phase_tests.cpp (154), pairing_tests.cpp (111), precise_wait_tests.cpp (53), frame_telemetry_tests.cpp (53) | full | - |
| tools/verify/interp_real.sh (35), tools/telemetry/nebula_frame_telemetry.py (130) | full | - |
| PlayerInputs.cpp | line 93 by grep only | rest |
| B-findings-port.md | 40-74, 205-224, 500-519, 690-719, 830-919, 1464-1588, 1740-1753, 3095-3274, 3712-3821 | rest |


