# Shader-cache seed — what this is, where it came from, and how to refresh it

Two files that `installer/build-release.ps1` embeds and `InstallEngine.cs`
(`SeedShaderCache`) extracts to `%LOCALAPPDATA%\Nebula\shadercache\RMGE01\` on a fresh
install, so a user's **first** launch is warmed instead of paying the full cold-start
compile. A cache the machine already built is never replaced.

| file | size | contents |
|---|---|---|
| `shaders.bin` | 1 304 200 B | 149 DXBC pairs (`GXSC` magic, version 24) |
| `psos.bin` | 5 768 B | 180 `PsoKey` records (`GKPC` magic, version 280 = `0x100 + kShaderCacheVersion`) |

The build script **checks both header versions against the constants in
`runtime/src/gx/pipeline_cache.cpp` before embedding them.** A stale `shaders.bin`
fails the release outright, because shipping a file the loader discards would look
like coverage while providing none; a stale `psos.bin` is omitted with a warning,
since it only feeds PSO prewarm. Refresh both after bumping either version.

## Why only these two, and never `pipelines.bin`

The runtime's own loaders draw the line, and the seed follows it:

| cache | loader validation | portable? |
|---|---|---|
| `shaders.bin` | **magic + version only** (`load_disk_cache`) — no adapter read anywhere | **yes** |
| `psos.bin` | magic + version; keys derive from GX state | yes |
| `pipelines.bin` | magic + version + `shader_version` + **adapter LUID** (`load_pipeline_library`) | **no — rejected** |

`pipelines.bin` is the driver's own optimized library. Its loader compares the stored LUID
against `device_->GetAdapterLuid()` and discards the file on any mismatch, so seeding it would
only produce a rejected load. Do not add it.

## Provenance, stated plainly

Copied from the machine's live cache
(`%LOCALAPPDATA%\Nebula\shadercache\RMGE01\`), which was itself produced by playing the game.
Verified before copying:

- `shaders.bin` parses to exactly **149 records consuming the whole file** (8-byte header +
  149 × (40 + vs_size + ps_size) = 1 304 200, exact);
- **all 149 pass the loader's own validation** — `fnv1a64` checksums and `D3DReflect`
  stage-type checks, reproduced by a standalone transcription of `load_disk_cache`
  (`AgentWork/agent-5/bench/load_disk_cache_probe.cpp`);
- `psos.bin` is exactly **180 × 32-byte** `PsoKey` records after its 8-byte header.
- **Every one of the 180 keys is covered by `shaders.bin` — 0 orphans.** This matters
  because `enqueue_cached_pso_prewarm` skips any key whose shader stages
  `find_cached_shader_stages` cannot supply, so an orphaned key can never build a
  pipeline and is dead weight that *looks* like coverage. Reproduce with
  `AgentWork/agent-16/gen/seedorphans.cpp`, which walks `shaders.bin` at the true
  record layout (`PsoKey` is **32 bytes**, not 80 — an earlier orphan count was
  withdrawn as an artefact of an 80-byte stride) and reports `180 covered, 0 ORPHANED`.

**These are not guaranteed to match the runtime's version constants forever.** If a
version is bumped, the runtime rejects the seed (`shader_cache_repair_pending_` /
`pso_cache_repair_pending_`) and the user simply pays the cold start they pay today —
the worst case is benign, which is what makes seeding safe. The build script's guard
turns that silent rejection into a build-time error (or, for `psos.bin`, a visible
omission) so it cannot go unnoticed.

**`psos.bin` was re-versioned from 5 to 280** when `kPsoKeyCacheVersion` moved from a
literal to the derived `0x100u + kShaderCacheVersion` form. Only the header word
changed: all 180 records were copied through verbatim, and the record layout is
unchanged (`shader_keys.h` still asserts `sizeof(PsoKey) == 32` and
`sizeof(RenderStateKey) == 16`). `AgentWork/agent-16/regen-psos-seed.ps1` performs
that rewrite and refuses to write unless the magic is `GKPC`, the record region is a
whole multiple of 32 bytes, and the rewritten record region is byte-identical to the
source.

## Coverage, and its honest limit

The seed covers **the opening scene and whatever the recording session traversed — not the whole
game.** To widen coverage, play further on a reference machine and copy the two files again; no
code change is involved, though `psos.bin` may need its version word rewritten to match
`kPsoKeyCacheVersion` (see below).

Expected effect, from the measured per-miss costs in the recording: the FXC half of a cold-start
miss is **6 717-21 474 us** and the driver half **2 287-15 244 us**. This seed removes the FXC
half for every configuration it covers, on every machine, regardless of GPU.

## Safety properties, each read out of the runtime

1. **No adapter check on these two files**, so a seed built on any GPU loads on any other.
2. **Every record validated on load** (`fnv1a64` + `D3DReflect`), which needs no D3D12 device —
   a foreign or corrupted seed is rejected record-by-record rather than trusted.
3. **A pre-existing cache is never clobbered** — `SeedShaderCache` in
   `installer/src/Setup/InstallEngine.cs` skips any file that already exists, so an update or
   reinstall cannot replace what the user's own machine built.

## Refreshing

```
copy %LOCALAPPDATA%\Nebula\shadercache\RMGE01\shaders.bin  installer\shadercache-seed\RMGE01\
copy %LOCALAPPDATA%\Nebula\shadercache\RMGE01\psos.bin     installer\shadercache-seed\RMGE01\
```

Then run `AgentWork/agent-16/gen/seedcheck.cpp` (compile and run; no runtime build needed) to
confirm both headers match the runtime's constants, that `shaders.bin`'s 149 records each pass
`fnv1a64`, and that both files consume their bytes exactly. `AgentWork/agent-16/regen-psos-seed.ps1`
rewrites only the `psos.bin` version word if the constants have moved. Also confirm that
`pipelines.bin` has not crept in — it is rejected on any adapter mismatch and must never be seeded.
