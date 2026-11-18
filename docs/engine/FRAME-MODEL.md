# The frame model

How a frame moves through Nebula's threads: who owns what, where data changes hands, and what may wait for what.
Part 1 is the code as it is today, with file and line (branch docs/frame-model, on app/e). Part 2 is the target and the
rules the later stages hold (07-BASE-FIRST, stage 2 item 1). Part 1 grows section by section.

## Part 1. Today

### 1.1 The threads a frame touches

| Thread | Started at | Owns | Hands over |
| --- | --- | --- | --- |
| Game ("Nebula Game") | `native/host/host_main.cpp:6538`, running `gModule.run()`; the host's main thread (:6527) only waits for messages, the game's end and the once-a-second report (:6628) | the game's state, the frame list it writes | a written list to the render thread (1.2) |
| Render | `runtime/src/render/render_core.cpp:259` (`RenderCore::render_thread_main`, :1550) | the device, every GPU object, the lists it walks | released lists back to the game (1.2); pictures to the display |
| Window owner | `runtime/src/render/window_owner.cpp:20` | the OS window and its message loop | size, focus and input to the game and render threads |
| Display clock | `runtime/src/render/vblank_clock_win32.cpp:175` | the vertical blank's time | the moment a picture is due (pacing) |
| Pipeline workers | `runtime/src/render/core/pipelines.cpp:104` | compiling pipeline states | finished pipelines to the render thread |
| Record workers | `runtime/src/render/core/draw_record.h:279` | building draw records | records to the render thread's walk |
| Texture pack workers | `runtime/src/render/texture_pack.cpp:176` (below normal priority, :225) | reading and decoding replacements | decoded pixels, picked up at `pump` once a frame |
| Movie | `runtime/src/render/movie_playback.cpp:75`, :299, :720 | decoding a movie's pictures | pictures to the render thread |
| Sound | `runtime/src/native_audio.cpp:37`, `runtime/src/audio_device_wasapi.cpp:103` | the output device's buffer | none to the frame; it reads the game's mixed sound |
| Save and log writers | `runtime/src/achievements.cpp:215`, `runtime/src/stats.cpp:160`, `runtime/src/ui/settings_save_model.cpp:36` | files | none to the frame |
| Host sampler | `native/host/host_main.cpp:698` | the process's own figures | figures to the logs |

The remaining threads of `runtime/src` (network, mods' services, controllers, updates, Discord) do not take part in a
frame; they are listed in 1.6 with what each may touch.

### 1.2 The frame list: written, handed over, released

1. The game writes a `NebulaFrameList` and submits it: `native/host/host_main.cpp:2778` (`svc_submit_frame`) stamps
   the game frame, then `galaxy::render::attach_frame_list` (`runtime/src/render/render_api.cpp:63`) stores it as the
   producer's list, and `submit_frame` asks for a picture.
2. On the game thread, `RenderCore::render_frame` (`runtime/src/render/render_core.cpp:558`) takes the list into a
   `FrameChunk` (:579-601) with the registries' and the texture store's queued work (:593-594), taken under the device
   gate's hold (:590).
3. The game thread waits there for room in the render queue (:604-635): `queue_place` decides from the queue's depth
   (`render_queue_depth`, a stalled render thread gets `kStalledRenderQueueDepth`) and the render thread's busy time;
   the wait is `queue_space_cv_` (:631, :633). The time it waits is counted as the game's wait (`GameWaitTimer`, :618).
   Then the chunk joins `frame_queue_` (:645) and `queue_cv_` wakes the render thread (:647).
4. The render thread takes the front chunk (:1911-1912), or drops a chunk made for a lost device's epoch and releases
   its list at once (:1903-1907).
5. A list is released when the render thread no longer needs it: it keeps the newest list it walked and the one before
   (the in-between pictures read both), and `release_lists_before` (:2250) releases every older one with a release
   fence first (:2253), so every read of the list comes before the game writes it again. The game writes a list again
   only once it sees the release (`native/host/host_main.cpp:2061`).
6. Waits on the whole render thread: `RenderCore::wait_for_render_idle` (:696) holds the game until the queue is empty
   and no chunk is in flight. It is called at `native/host/host_main.cpp:2084` and :3236 (named in 1.5 with why).

So today the game and the render thread share two things per frame: the list (owned by the game until submitted, by
the render thread until released) and the chunk's queued registry and texture work (taken under the device gate).

### 1.3 The render thread's loop

1. `RenderCore::render_thread_main` (`runtime/src/render/render_core.cpp:1550`) names the thread, raises it above the
   normal priority on Windows (:1555), opts the process out of power throttling and holds the 1 ms timer (:1561-1568),
   then runs `render_loop` (:1760) until it ends; a lost device runs `restart_device` and the loop again (:1571-1576).
2. With nothing queued it sleeps on `queue_cv_` for up to 100 ms hidden or 250 ms shown (:1863), so a service waiting for
   a new card can reach it (DeviceGate).
3. A display slot that is due goes before queued chunks (:1867-1878): presents keep their cadence, and a saturated GPU
   cannot render every endpoint unseen.
4. It takes the front chunk (:1911); a render chunk followed by a present-only one takes that present with it
   (:1914-1922). In-flight counts (`render_chunks_in_flight_`, `present_chunks_in_flight_`, :1926, :1929) are what
   `wait_for_render_idle` (1.2 item 6) waits on; each taken chunk frees queue room (`queue_space_cv_`, :1908, :1941).
5. A present-only chunk presents the cached picture again, or hands its moment to the in-between sequence (:1942-1960);
   with no slot clock (an unlimited rate) it presents again once the last present is done (:1974-1981).
6. A render chunk is walked (`walk_frame`, :2588), and the lists older than the one before are released (1.2 item 5).
7. On a lost device, `drop_device_state` (:1605) releases every queued and walked list (:1608-1624) and clears the walk,
   the in-between sequence and the screen probes (:1629-1656), so no list stays held by a dead device.

### 1.4 What the render thread waits for on the GPU

Two frames are in flight (`kFramesInFlight`, `runtime/include/galaxy/render/renderer_common.h:49`).

| Renderer | Per frame | Inside a frame, the whole GPU |
| --- | --- | --- |
| Direct3D 12 | the slot about to be used again (`renderer_d3d12.cpp:3774-3782`); the oldest history ring segment when it is reused (:3952-3956); the frame latency waitable, 100 ms bound (:4364) | `wait_for_gpu` (:916) in `reset_sampler_frame` (:1410), `prepare_temporal_upscaler` (:5034, :5067), `record_temporal_upscale` (:5229), and a capture's readback (:6904, :6907) |
| Vulkan | the slot (`vk/renderer_vk.cpp:2013`); the history ring segment (:2111) | to be read (1.4 continues) |
| Direct3D 11 | the slot (`d3d11/renderer_d3d11.cpp:536`); the frame latency waitable, 100 ms bound (:676) | to be read |

Outside a frame, `wait_for_gpu` drains the GPU at a window size change (:1955), a swap chain's re-creation or resize
(:3371, :3419), an HDR switch (:4150), shutdown (:3114, :3141) and the pictures at exit (:6769). Those are allowed by the
rules of part 2; the four inside a frame are not, and go to the list at the end of part 1.

The game thread waits on the render thread at three places: queue room (1.2 item 3), the render thread idle (1.2 item
6), and a screen probe's answers for frame F - 2 (`render_core.cpp:2032`, bounded by `kProbeWaitNs`), which waits only
for that list's GPU work, as the list was walked when F - 1 superseded it (:2028-2031).

### 1.5 What the game thread waits for

The game thread runs the game module's loop; each wait below is a call it makes into the host (`native/host/host_main.cpp`).

| Wait | Where | Bound | Kind |
| --- | --- | --- | --- |
| The next step's moment | `svc_frame_wait` (:1140): the display's vertical blank on the SDL hosts (:1166), else a precise wait for the host clock's due time (:1182, `PreciseWait`), recorded as `StepWaitBegin`/`StepWaitEnd` | the step period (`kGameStepNs`) | pacing, every frame |
| Suspended (minimised, or the system's suspend) | `wait_while_suspended` (:1034), from the step wait (:1188) | until resumed | by design |
| Room in the render queue | `render_frame`'s wait (`render_core.cpp:604-635`, 1.2 item 3) | none of its own | every frame the render thread is behind |
| A screen probe's answers | `svc_probe_results` (:2792) to `screen_probe_results` (`render_core.cpp:2032`) | `kProbeWaitNs` | only frames with probes |
| The render thread idle | the output's shape latched (`svc_frame_begun`, :3232-3240, `wait_for_render_idle`) | none | a window or aspect change |
| The last save request | `svc_save_wait` (:3499) to `StatusBoard::wait` (`runtime/src/save_files.cpp:179`) | none | the game's own `waitRequest` |
| A file's bytes | `svc_asset_read` (:3152): a copy from the store's mapped view (`gPak.store.read`), so a page not in memory is read from the drive on this thread | the drive | loads |

The start check's wait for each list's release (:2070-2078) and its final `wait_for_render_idle` (:2084) are the
`--start-check` mode's own, not the game's.

Two of these hold the game for the whole render queue: the aspect latch (all queued frames, then the picture is fitted)
and a render queue already at its depth. The asset read is the one wait on the drive inside a step; every other file the
game touches goes through the save worker.

### 1.6 The other threads, and what of the frame each touches

| Thread | Touches | Hand-over |
| --- | --- | --- |
| Window owner (`window_owner.cpp:20`) | the window's size, focus, input | the size reaches the aspect latch (1.5) and the swap chain's resize (1.4, a whole-GPU wait) |
| Display clock (`vblank_clock_win32.cpp:175`, started once by `vblank_grid`) | the vertical blank's time and period | under `g_mutex`, read by the step wait and the present slots |
| Pipeline workers (`core/pipelines.cpp:104`, lowest priority) | compiling pipeline states | each published to an atomic cell (`cell` :145, `publish` :164); a draw whose pipeline is not there yet compiles it on the render thread (`create_now` :253; a quick link where pipelines link from parts, :271) |
| Record workers (`core/draw_record.h`, `RecordPool`) | encoding a pass's chunks | fork and join inside the walk: the render thread takes a share and returns when all are done |
| Texture pack workers (`texture_pack.cpp:176`) | reading and decoding replacements | decoded pixels picked up at `pump` once a frame; the upload is the render thread's |
| Movie (`movie_playback.cpp:75`, :299, :720; :1016 prepares the decoder) | decoding a movie | pictures through the stager to the render thread |
| Streamline start (`renderer_d3d12.cpp:2314`) | loading the vendor's library | joined at device creation, before any frame |
| Picture writer (`renderer_common.cpp:427`) | writing a captured picture to disk | the pixels are copied out first; counted so the end waits for it |
| HDR watcher (`os_hdr.cpp:393`) | the system's HDR state | read by the swap chain's HDR switch (1.4, a whole-GPU wait) |
| Telemetry publisher (`frame_telemetry.cpp:104`) | the frame counts | samples them into shared memory for outside readers |
| Texture categories index (`texture_categories.cpp:347`) | reading the store's index | `index_ready` (acquire) before any lookup |
| Sound, save writers, host sampler, input (`host_main.cpp:3914`) | none of the frame | — |

Not part of a frame: controllers (`controller_hid.cpp`, `wiimote_hid.cpp`, `switch2_ble_win.cpp`), mods' services
(`luma_net.cpp`, `luma_fs.cpp`, `luma_mods.cpp`, `luma_assets.cpp`, `luma_capture.cpp`, `luma_input.cpp`), the settings
screen's workers (`ui/native_settings.cpp`), updates (`update/update.cpp`), vendor downloads, Discord.

### 1.7 What breaks the frame's rules today, or keeps the console's shape

The rules (part 2): the render thread never compiles, never reads a file and never waits for the whole GPU inside a
frame; the game thread waits for the render thread only at the points this file names. Each part with its row; a part
with no row has a new one.

| Part | Where | Row |
| --- | --- | --- |
| A draw whose pipeline is not ready compiles it on the render thread | `core/pipelines.cpp:253` (`create_now`) | P-010 |
| Utility pipelines compiled on the render thread at first use, outside the cache | `core/pipelines.cpp` | P-052 |
| A present waits for every pipeline creation in flight (Direct3D 12) | `core/present_gate.h` | N-1067 |
| The sampler heap, full, drains the whole GPU | `renderer_d3d12.cpp:1410` (`reset_sampler_frame`) | P-048 |
| Size changes drain the whole GPU inside a frame | `renderer_d3d12.cpp:5034`, :5067, :5229 (the temporal upscaler), :1955 | P-028 |
| The game thread waits for frame F - 2's probe answers | `render_core.cpp:2032` | N-1068 |
| The game thread waits for the whole render queue at an aspect change | `host_main.cpp:3236` | to be given |
| A capture's readback waits for the GPU inside a frame | `renderer_d3d12.cpp:6904`, :6907 | to be given |
| The game thread copies loads from the mapped store: a page not in memory is a drive read inside a step | `host_main.cpp:3152` (`svc_asset_read`) | to be given |

The list grows with the files still to read (the post-process files for Vulkan and Metal, the upscaler and
frame-generation bodies, `draw_walker.cpp`).
