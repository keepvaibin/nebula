# Service: draw

## Layers 7 to 9 (FB): draw.view rigs, draw.post, draw.native

14. draw.view (layer 7, `luma_draw_view.h`): create, camera, follow, texture, set_on, release; 2 views a mod, 8 in
    all, sides up to 1024.
    - Rigs (kit `camera`), in the same header, 1.0: `LumaDrawViewEye` {eye[3], at[3], up[3], fov_y} and
      `LumaDrawViewCamera` {rig, eye[3], at[3], up[3], fov_y, actor, offset[3], joint[32], near}.
    - Names. The face names them `draw.view.Eye` and `draw.view.Camera`. A C name starting `LumaDrawView` belongs to
      draw.view, never to draw (svc.rs `own`), so lumakit.md's `draw.ViewCamera` and `draw.Eye` are written
      `view.Camera` and `view.Eye`. The view handle is `LumaDrawViewRegion` (`view.Region`; layer 7e): a C name equal
      to the prefix (`LumaDrawView`) gives the handle no kind, so every call taking it was C-only (S0134).
    - Rig kinds (`LumaDrawViewRig`):
      - FIXED 0: the eye as given.
      - FOLLOW 1: the eye at `offset` in the actor's own axes, looking at its place, its up as up.
      - JOINT 2: the eye at the actor's joint `joint`, looking along the actor's front, its up as up; `near` is the
        near plane.
      - WORLD 3: the eye at `offset` in world axes from the actor, looking at it, the actor's up as up (side cameras).
    - Calls appended:
      - `rig(mod, view, camera)`: the view follows the rig each frame.
      - `eye_of(mod, camera, out eye)`: where the rig's eye is this frame (what the kit gives the main camera).
    - A rig whose actor died keeps its last eye. A `Look(fn() -> Eye)` is a script that computes an Eye each frame
      and calls `camera` with it; no closure crosses.
15. draw.post (layer 8; `luma_draw_post.h`, service `draw.post` 1.0, no permission). A post program is a full-screen
    pixel program over the frame.
    - Source (3.4 layer 4: the `post` kind of the cook).
      - Files: `assets/post/<name>.hlsl` and `post/<name>.json` `{"params": [{"name", "type":
        float|float2|float3|float4, "default"}], "reads": ["depth", "motion"]}`. The file defines `float4 post(PostIn
        i)`.
      - Parameters are globals of their names, as material shaders' are. They are packed in order into four rows of
        four floats (16 floats; a param never crosses a row; more is `Cook.Post.TooManyParams`).
      - The forbidden words, sizes and refusals are material shaders' (`Cook.Shader.*` as `Cook.Post.*`).
      - No bindings and no entry point: bindings differ per backend, so the host owns them.
      - The host puts its prelude before that text (PostIn, scene, scene_texel, depth, motion, `post_params`, the
        bindings) and its own main after it, and compiles it (DXIL, SPIR-V) at `post_register`, as mod_shader_compile
        does for material programs.
    - What the program sees.
      - `PostIn` {uv (0 to 1, top left 0), pixel (pixels), size (the frame's pixels), time (seconds, the materials'
        time)}.
      - `float4 scene(float2 uv)` and `float4 scene_texel(int2 pixel)`: the frame so far, display-encoded 0 to 1, with
        the post before it in the stage already applied.
      - With `reads` depth, `float depth(float2 uv)`: the distance from the eye along the view axis in game units (the
        far plane's where nothing was drawn).
      - With `reads` motion, `float2 motion(float2 uv)`: where that surface was the frame before, minus uv (uv units).
      - The output colour is display-encoded; its alpha is ignored.
    - Stages (`LumaDrawPostStage`):
      - WORLD 0: after the 3-D scene ends and the game's bloom, before 2-D, at the resolution the frame is shown at
        (after the upscaler).
      - FRAME 1: after the HUD (hud.after), before the present point.
      - Depth and motion are the scene's, at its internal resolution, sampled by uv at both stages. Motion exists only
        where the frame has motion vectors (temporal upscaling on); elsewhere a program that reads motion is refused
        (542).
      - One program per stage a mod (a slot). In a stage the mods' programs run in Mods-list order, bottom first (as
        the draw points), each reading the one before.
    - Calls:
      - `program(mod, name, out program)`: the mod's cooked `<name>.post`; 8 programs a mod. Refusals:
        - missing: LUMA_E_NOT_FOUND, 540 Draw.Post.Missing {name};
        - The host refused to compile it: LUMA_E_ARGUMENT, 541 Draw.Post.Compile {name, detail};
        - it reads motion where the host has none: LUMA_E_ABSENT, 542 Draw.Post.NoMotion {name}.
      - `set(mod, stage, program)`; 0 empties the slot.
      - `params(mod, stage, values, count)`: count 0 to 16, kept until changed; the params' defaults until then. An
        empty slot: LUMA_E_STATE, 543 Draw.Post.Empty {where}.
      - `program_free(mod, program)`: a program in a slot empties the slot.
      - Everything leaves with the mod. Facts `draw.post.<stage>` {mod, program}.
    - Game side (FB). Each frame, at each stage with a slot set, the game appends one pass `NEBULA_PASS_POST` per slot
      to the frame list: `arg0` the host's program id, `first_item` the first of its 4 parameter rows in
      `instance_rows`. With no slot set there is no pass, one load.
    - Host side (FA), `nebula_game_api.h` 152:
      - `int32_t (*post_register)(void* user, const void* text, uint32_t size, uint32_t reads, uint32_t* program,
        char* error, uint32_t error_capacity)`: NEBULA_POST_OK 0 and *program (never 0); NEBULA_POST_COMPILE 1 and the
        compiler's first error in `error`; NEBULA_POST_NO_MOTION 2 (reads motion, no motion vectors).
      - `void (*post_free)(void* user, uint32_t program)`.
      - The pass draws one full-screen triangle with the program into a new picture (D3D12, Vulkan and D3D11). It binds the
        frame so far, depth and motion as `reads` asks.
      - The constant block is the host's (galaxy/render/mod_post.h): 8 rows, 128 bytes (D3D12 root constants b0,
        Vulkan push constants, D3D11 constant buffer b0): {size.xy, inv_size.xy}, {time (game_frame / 60 s), frame, flags, 0}, depth_terms
        (distance = x / (d * y + z), w the far plane's distance), {motion_scale.xy, 0, 0} (the host's motion target is
        in pixels toward the previous position), params[4] (the pass's 4 rows). The game sends only the program id and
        the 4 rows.
      - The new picture is the frame from there (as TAKE_FRAME).
      - Each picture the host draws from the list runs it, interpolated pictures too.
      - `NEBULA_PASS_POST` is appended to NebulaPassKind.
16. draw.native (layer 9; `luma_draw_native.h`, service `draw.native` 1.0, permission `native`). A mod's own GPU
    commands at a draw point.
    - Calls:
      - `at(mod, point, fn, user)`: points 0 to 2 in the scene's target with its depth; 6 present, the output picture
        without depth; others LUMA_E_ARGUMENT. fn null removes.
      - `backend(mod, out)`: 1 D3D12, 2 Vulkan, 3 D3D11; the same all session. 0 on Metal: no native draw is called
        there.
      - A mod without `native` granted cannot bind the service (services-registry 5.2).
    - The callback `void fn(void* user, const LumaDrawNativeFrame* frame)`:
      - It runs on the host's render thread when the host reaches that place in the frame list, once for each picture
        it draws from the list (interpolated pictures too).
      - It does not run on the game thread: the mod synchronizes what it shares with its game-thread code itself.
      - The frame and every handle in it are valid only during the call.
    - LumaDrawNativeFrame (`size` first):
      - size, backend, point, game_frame, picture (0, 1, ... within the list), width, height (pixels), samples (1, 2,
        4 or 8: the scene's target and depth may be multisampled).
      - view[16]: the game's 3x4 camera and 0 0 0 1, rows of four. projection[16]: clip from eye as the host draws the
        scene into that target, in the backend's own clip convention (depth range, direction, this picture's jitter),
        so a vertex drawn with projection x view meets the scene's depth exactly. Both zero at 6.
      - `d3d12` {device (ID3D12Device*), queue (ID3D12CommandQueue*), command_list (ID3D12GraphicsCommandList*, open,
        recording the frame), target (ID3D12Resource*), target_rtv (D3D12_CPU_DESCRIPTOR_HANDLE.ptr), target_format
        (DXGI_FORMAT), depth, depth_dsv, depth_format (0 at 6)}.
      - or `vulkan` {instance, physical_device, device, queue, queue_family, get_instance_proc_addr, command_buffer
        (recording, outside any render pass), target_image, target_view, target_format (VkFormat), target_layout,
        depth_image, depth_view, depth_format, depth_layout (0 at 6)}.
      - or `d3d11` {device (ID3D11Device1*), context (ID3D11DeviceContext1*, the immediate context: the host's thread
        only, during the call), target (ID3D11Texture2D*), target_rtv (ID3D11RenderTargetView*), target_format
        (DXGI_FORMAT), depth_format (0 at 6), depth, depth_dsv (null at 6)}. The call runs on a context state of its
        own (`ID3D11DeviceContext1::SwapDeviceContextState`): it starts with nothing bound and binds what it draws
        with; the host's state comes back after it.
    - What the mod leaves: the target and depth in the states and layouts given, no render pass open, nothing
      submitted on the host's queue. The host sets its own state again after every call.
    - Game side (FB). The game appends `NEBULA_PASS_NATIVE` at the point: `first_item` is the call's index in the
      list's new `native_calls` (`NebulaNativeCall` {fn, user}: the game's trampoline, which skips a function its slot
      changed since (removed, replaced, its mod stopped); the game changes a slot only after no call of it runs. No
      guard runs on the render thread: the `native` consent covers it) and `arg0` the point. So a mod's function never
      runs after its mod's end, without holding the mod's code past it.
    - Host side (FA), `nebula_game_api.h` 152:
      - NebulaFrameList gains `const NebulaNativeCall* native_calls; uint32_t native_call_count;`.
      - `NebulaNativeFrame` is LumaDrawNativeFrame (a typedef in nebula_frame_list.h, which includes
        luma_draw_native.h): one struct.
      - `NEBULA_OPTION_GRAPHICS_BACKEND` (1 D3D12, 2 Vulkan, 3 D3D11) answers draw.native.backend.
      - At the pass the host ends its render pass (Vulkan) or keeps the list open (D3D12), fills the frame, calls, and
        restores its state.
