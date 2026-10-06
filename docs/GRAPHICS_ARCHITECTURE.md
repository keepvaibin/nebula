# Graphics Architecture — the `galaxy::gx` Native GX Backend

Status: **Live backend**. The `galaxy_gx` static library is the active
NebulaRuntime and GxSandbox render implementation; headers under
`runtime/include/galaxy/gx/` are compile-enforced via
`runtime/src/gx/gx_headers_compile.cpp`.

This is the design record for the structured, low-overhead native backend that
consumes the captured GX FIFO stream. It replaced the old monolithic
first-boot renderer and is a lean custom consumer of the RMGE01 command stream
— **not** a general Wii video backend and not a Dolphin port.

## Decisions

| Decision | Choice | Rationale |
|---|---|---|
| API / shading language | D3D12 + HLSL (FXC, `vs_5_1`/`ps_5_1`) | Already in use and linked; SM 5.1 gives the dynamic texture indexing the uber pixel shader needs; D3D12 provides the DXR path for future ray tracing. Vulkan would discard the working backend. |
| Code location | Headers `runtime/include/galaxy/gx/`, sources `runtime/src/gx/`, `galaxy_gx` static lib | Both `NebulaRuntime` and `GxSandbox` link one library; the old renderer duplication is gone. `GraphicsSandbox/` stays as the offline `.gxdump` replay harness. |
| Public ABI | `galaxy/gx_d3d12.h` frozen; reimplemented as a thin shim over `GxBackend` | Zero churn at the `native_runtime.cpp` call site. |
| Render-target model | All GX draws → offscreen **EFB** (RGBA8 + `D24_UNORM_S8_UINT`, 640×528 × scale). `GXCopyDisp` → **XFB texture**; present = stretch-blit the VI-selected XFB, preserving the previous valid selection if the guest is between handoff states | The only model in which EFB-copy-to-texture (SMG: shadows, refraction, fades) works; the present blit is the post-processing / RT injection point; the EFB scale factor is the resolution/quality knob. |
| Depth format | `DXGI_FORMAT_D24_UNORM_S8_UINT` | Review amendment: exact Wii 24-bit Z parity; copy-clear `z24` maps losslessly; prevents Z-fighting regressions. |
| Shader stutter | Hybrid: build-time-compiled **uber-shader** fallback + **async specialized PSOs** + disk cache | See "Shader strategy". |
| Texture invalidation | **Explicit only — no content hashing** | Review amendment: hashing bottlenecks low-end CPUs. The hardware contract is explicit invalidation; `GXInvalidateTexAll()`'s BP write pattern drives `TextureCache::invalidate_all()`, and EFB-copy registration overwrites aliased entries. |
| Endianness | Checked scalar big-endian reads for opcodes/registers; **bulk SSSE3 `pshufb` byte-swap kernels** for vertex arrays | Review amendment: vertex payloads are the hot path; `FifoCursor::take()` hands out raw windows for `swap16_block`/`swap32_block`. |
| Hard-fail | Unknown opcode / CP/XF/BP state write / truncated payload / unresolvable guest pointer → `GxFatalError` + `.gxdump` artifact | Repo rule. State writes fail before mutation; only explicitly classified hardware no-op command bytes are accepted. |

## FIFO format (parser contract)

Opcode-dispatched byte stream, big-endian payloads:

| Opcode | Command | Payload |
|---|---|---|
| `0x00` | NOP | — |
| `0x08` | LOAD_CP_REG | u8 reg + u32 (VCD/VAT/array bases) |
| `0x10` | LOAD_XF_REG | u16 count−1, u16 base, count × u32 (matrices, texgen, channels, projection) |
| `0x20/28/30/38` | LOAD_INDX A–D | u16 index, u16 len:addr — indexed XF matrix load from guest arrays (**required for SMG skinning**) |
| `0x40` | CALL_DL | u32 guest addr, u32 size — nested display list, depth ≤ 4 |
| `0x48` | INVAL_VTX_CACHE | — |
| `0x61` | LOAD_BP_REG | u32: reg(31:24) \| value(23:0) |
| `0x80–0xBF` | DRAW | bits 7:3 primitive class, 2:0 vtx fmt; u16 count + vertex stream per VCD/VAT |

The BP register map that matters for SMG is encoded as named constants in
`gx_bitfields.h` (`namespace bp`); the TEV stage assembly from color-env /
alpha-env / TREF / KSEL registers lives in `assemble_tev_stage`.

## Module decomposition

```
FIFO bytes ──► FifoParser ──(FifoSink callbacks)──► GxBackend
                                │ reg loads              │ draws / EFB copies
                                ▼                        ▼
                             GxState ◄── reads ── VertexLoader → VB/IB rings
                          (CP/XF/BP + dirty bits)        │
                                │ keys                   ▼
                                ▼                  RendererD3D12 ──► EFB RT
                          PipelineCache ◄── ShaderGenerator         │ CopyDisp
                          (uber now, specialized async)             ▼
                          TextureCache (GX fmt decode, TLUT,   XFB registry ──► present blit
                                        EFB-copy aliases)      (EfbCopyManager)
```

Header inventory (`runtime/include/galaxy/gx/`):

| Header | Responsibility |
|---|---|
| `gx_bitfields.h` | constexpr CP/XF/BP register decode; zero OS includes |
| `gx_state.h` | `GxState` register files + decoded views + dirty bits |
| `fifo_parser.h` | `FifoParser`, `FifoSink`, `FifoCursor`, `GxFatalError`, SIMD swap kernels |
| `vertex_loader.h` | `VertexLoader`: any VCD/VAT → canonical 64-byte `GxVertexOut`, indexed triangle lists |
| `shader_keys.h` | Canonicalized `PixelShaderKey`/`VertexShaderKey`/`RenderStateKey`/`PsoKey`, FNV-1a |
| `uber_constants.h` | `GxVsConstants`/`GxPsConstants` GPU-shared layouts (static_asserted) |
| `shader_gen.h` | `ShaderGenerator`: keys → specialized HLSL |
| `pipeline_cache.h` | `PipelineCache` + lock-free `PsoCompletionQueue` |
| `texture_cache.h` | GX format decode, TLUTs, EFB-copy aliases, explicit invalidation |
| `efb_copy.h` | `EfbCopyManager` XFB registry / `latest()` presentation source |
| `renderer_d3d12.h` | Device, swap chain, EFB targets, `UploadRing`/`DescriptorRing`, `DrawCall` submission |
| `gx_backend.h` | `GxBackend` orchestrator implementing `FifoSink` and the frozen public API |

`gx_bitfields.h`, `gx_state.h`, `fifo_parser.h`, `vertex_loader.h`,
`shader_keys.h`, `uber_constants.h`, `shader_gen.h` include **zero
Windows/D3D12 headers** — they replay `.gxdump` captures in plain ctest
binaries, which is how the working geometry never regresses during the
migration.

### Per-frame data flow

```
render_frame(fifo, size, memory, services)
  ├─ renderer.begin_frame()             // advance frame ring; wait ONLY on
  │                                     // fence of frame N - kFramesInFlight;
  │                                     // drain PSO completion queue (sole
  │                                     // publication point, render thread)
  ├─ parser.run(fifo, memory, *this, state)
  │    ├─ 0x08/0x10/0x61 → state.load_cp/load_xf/load_bp  (dirty bits)
  │    ├─ 0x20-0x38      → resolve guest array → state.load_xf_indexed
  │    ├─ draw           → on_draw(): flush_draw_state() if dirty
  │    │                   (keys → PipelineCache::get, constants → CB ring,
  │    │                    XF palette snapshot → matrix ring, textures →
  │    │                    TextureCache::get → SRV table), then
  │    │                   VertexLoader::load → renderer.draw(DrawCall)
  │    ├─ BP 0x52        → on_efb_copy(): XFB copy / texture copy / copy-clear
  │    └─ BP 0x45/47/48  → on_pe_finish()/on_pe_token() → PE interrupt hooks
  ├─ renderer.present(game-selected XFB)     // preserves the previous valid
  │                                          // VI selection during handoffs
  └─ renderer.end_frame()               // submit + signal, never waits
```

## Shader strategy (stutter prevention)

**Hybrid — specialized PSOs are the steady state; the uber pipeline is the
synchronous fallback used only while a specialization is in flight; everything
persists to disk.**

- *Specialized-only* stutters: first-seen TEV configs arrive in bursts at
  scene load; FXC + driver PSO build stalls tens of ms each on low-end CPUs.
- *Uber-only* costs 2–4× fragment time on exactly the iGPUs we target (a
  16-iteration dynamic TEV loop with per-iteration constant fetches and
  dynamically indexed samples), and SMG is fill-heavy.
- The hybrid costs one mechanism (promotion). Both shader families share the
  constant layouts (`uber_constants.h`) and the single GX root signature, so
  promotion is a pipeline-pointer swap with no binding changes.

Thread-safety contract (review amendment): compile workers never touch the
published map. They push completed PSOs onto the lock-free MPSC
`PsoCompletionQueue`; `drain_completions()` publishes them on the **render
thread only**, inside `begin_frame()`, before any command-list recording.

Key canonicalization: every field that does not affect codegen for the
*enabled* stage count is zeroed before hashing (`shader_keys.h`), keeping the
cache hit rate high. `RenderStateKey` (blend/zmode/cull/pixfmt) keys the PSO
but not the HLSL — one compiled shader pair serves all its render-state
variants.

Disk cache, `%LOCALAPPDATA%\Nebula\shadercache\RMGE01\`:

- `shaders.bin` — append-only `{magic, version, key, vs/ps DXBC, fnv}`
  records; second run builds PSOs from cached DXBC on the workers during
  boot, no FXC.
- `pipelines.bin` — `ID3D12PipelineLibrary1` blob keyed by `PsoKey` hash;
  invalidated when the stored adapter LUID / driver version mismatch.
- Optional `precompile.list` — keys harvested from play traces, warmed at
  initialize (the precompilation leg).

TEV fidelity: stage math in float, each stage result quantized to the 8-bit
lattice (`floor(x*255+0.5)/255`) before compare ops and alpha test, making
comparisons against `ref/255` exact — cheap, and avoids classic banding and
alpha-cutout bugs.

## EFB copies and presentation

- **EFB**: RTV `RGBA8` + DSV `D24_UNORM_S8_UINT`, `640×528 × efb_scale`.
  GX copy-clear converts the latched `z24` via `z24 / 0xFFFFFF`. SMG's usual
  `RGBA6_Z24` pixel format renders at 8-bit; tracked via `PeControl`.
- **GXCopyDisp** (`copy_to_xfb`): `EfbCopyManager` keeps
  `guest_xfb_addr → XfbTexture`; the copy records an EFB→XFB blit honoring
  y-scale. No YUV conversion, no guest-memory write-back — a guest CPU read
  of the XFB range is a native_host tripwire.
- **Present**: the XFB address selected by the guest VI/XFB manager; if that
  manager is briefly between valid states, the backend preserves the previous
  valid VI-selected XFB instead of substituting the newest copied buffer.
- **GXCopyTex**: blit the EFB region into a `TextureCache` entry registered
  at the destination guest address; later `TX_SETIMAGE3` pointing there binds
  the GPU copy directly. Mandatory beyond the title screen (drop shadows,
  water, transitions).
- **PE token/finish** (BP `0x45/0x47/0x48`): routed through
  `FifoSink::on_pe_finish/on_pe_token` into the runtime's PE interrupt
  machinery (`GXDrawDone` / `GXSetDrawSync` waiters). Possibly a boot-progress
  fix, not just graphics — see HANDOFF_TITLE_SCREEN.md's GameSystem pump stall.

## Migration plan (the triangle never breaks)

Each step ends with `/W4 /WX` clean and the current boot output still
rendering; one step = one commit.

| Step | Work |
|---|---|
| M0 | De-duplicate: move the monolith into `runtime/src/gx/`, create the `galaxy_gx` static lib, point `NebulaRuntime` + `GxSandbox` at it, delete the `GraphicsSandbox/` copy. |
| M1 | Extract `RendererD3D12` mechanically (keep the per-frame GPU wait for now). |
| M2 | Extract `GxState` + `FifoParser`; **switch to hard-fail + `.gxdump`**; implement LOAD_INDX; fix FIFO capture to be command-aligned if torn packets surface; wire `FifoSink` PE callbacks (runtime may ignore them initially). |
| M3 | EFB depth buffer + blend/zmode/cull from BP via `RenderStateKey` (small static PSO table). First visible correctness jump. |
| M4 | `TextureCache` + TexImage decode; fixed PS becomes tex0 × vertex color. Title-screen imagery appears. |
| M5 | `VertexLoader` rewrite: canonical vertex, per-vertex PNMTXIDX, indexed output, XF palette as structured buffer. Fixes skinning. |
| M6 | `ShaderGenerator` + uber pipelines + `PipelineCache` (uber-only first). TEV correct. |
| M7 | EFB/XFB model: draws retarget the EFB, copy-exec, present = XFB blit; remove the purple clear and the runtime replay hack. |
| M8 | Frame pipelining (drop the per-frame GPU wait), disk cache, async specialization workers. |

## Risks / open questions

1. **Torn FIFO packets**: the legacy parser's tolerate-and-skip behavior
   suggests the WGPIPE/CP-drain capture may split commands; M2's hard-fail
   will surface this immediately. Fix = command-aligned capture (carry parser
   residue across drains) in `native_host.cpp`.
2. **PE interrupt wiring** crosses the gx/native_host boundary; may require a
   `kNativeAbiVersion` bump if `native_api.h` grows a hook.
3. **Texture staleness** under explicit-only invalidation: a guest mutating
   texture memory without `GXInvalidateTexAll`/EFB-copy shows stale texels.
   Accepted trade-off (review amendment); `debug_invalidate_every_frame`
   exists for A/B verification.
4. **Indirect texturing** (`gen_mode().num_ind_stages > 0`) and **XF channel
   lighting**: key bits reserved now; fatal-log on first use until
   implemented (water/heat effects are in-game, not title-screen).
5. **EFB CPU access** (`GXPeekZ` for star-pointer picking): GPU readback with
   sync pain if SMG uses it; native_host tripwire first, design deferred.
6. **RenderStateKey permutation count**: theory large, SMG practice ~10–20;
   uber PSOs per render state are built from prebuilt DXBC (~1 ms driver
   cost). Measure; pre-create the common table at init if it stutters.
