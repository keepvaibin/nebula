# CPU bug-fix and optimization checkpoint, 2026-10-07 UTC

Source-only candidate. The user prohibits game launches, rebuilds and tests for
now. No change below has compiled or established an FPS improvement. The
persistent 60 FPS / low-end / matched Dolphin acceptance goal remains active.

The installed control, saves (including ISFS directory/file alternate streams),
and caches have private copies under `D:/NebulaWork/perf-20261007`. Originals and
earlier accepted checkpoints are retained. The unrelated pre-existing
`installer/src/Setup/InstallEngine.cs` changes remain outside these checkpoints.

## Implemented CPU and correctness changes

- **Independent dirty tracking:** bulk writes partly crossing an audio-tracker
  edge previously lost that overlap if renderer tracking covered the whole
  write. Each tracker now marks its valid overlap independently. Full coverage
  still controls callback suppression. Crossing RAM aliases is rejected. The
  rare partial-span path stays outside scalar-store machine code. Added unrun
  alias/edge/fallback regressions and updated the ordered-store byte oracle.
- **Decoded-vertex cache inputs:** unused array bases/strides and CP texture
  matrix defaults previously fragmented the cache. Only indexed attributes and
  an actually consumed default position index enter that part of the key.
  Per-packet indices remain in the owned packet identity. Fresh shader constants,
  guest-array byte comparisons, invalidation, and upload ownership remain.
  Added unrun comparisons of actual decoded vertices across relevant/irrelevant
  CP changes.
- **Fixed vertex conversion:** replace variable division by `2^shift` with
  exact binary32 exponent scaling for the 8/16-bit integer input domain and
  VAT fractions 0..31. No reciprocal approximation or guest FP change. Added
  an unrun exhaustive U16/S16/fraction division oracle.
- **DSP memory publication:** empty CPU-owned bitmap words need no zero store;
  nonempty words enumerate set bits in ascending order instead of testing all
  64 positions. The same pages, byte spans and mailbox generations publish.
- **DSP outbound spans:** enumerate contiguous runs in the existing exact byte
  masks instead of inspecting all 128 bytes of every dirty page. Word/page
  adjacency, ascending callbacks, failure handling, generation acknowledgements,
  and byte ownership remain. Added an unrun independent byte oracle covering
  empty/full masks, edges, gaps, descending writes, and successive sparse flushes.
- **Indexed matrix uploads:** remove a duplicate complete XF classification
  pass. Both direct and indexed transfers still validate every address before
  any mutation, then share a private application pass. Added unrun matching
  register/dirty-effect and invalid trailing-word checks.
- **Display-list reuse:** the single-candidate lookup avoids allocating/copying
  a vector. Byte/dependency checks and invalidation remain; multiple variants
  retain the protective owned candidate vector.
- **Serial frame capture:** copy and detach live aliases in one pass without a
  temporary work vector. The opt-in parallel path retains its ownership/join
  behavior.
- **GX state/pipeline work:** uniform-only indirect coefficients, coordinate
  sizes, alpha references, fog coefficients, destination-alpha values and Z
  bias no longer invalidate shader keys. Dependency hashes invalidate only on
  their actual inputs. Canonical shader hashes and unchanged PSOs are reused.
- **Texture tables:** gather eight CPU-heap source descriptors, including a
  persistent valid null SRV, into one descriptor-copy call. Transient tables
  still belong to their frame ring; resources, aliases and retirement persist.
- **Known pipeline preparation:** prewarm can combine independently retained
  shader stages even without an exact persisted pair record. New configurations
  still acquire their required correct pipeline. Cache formats remain unchanged.
- **Checked native/host RAM lookup:** native bulk/matrix/particle helpers and
  host memory-copy services now try the existing immutable 16-entry RAM table
  before walking the complete region list. Whole-span bounds stay checked;
  partial mappings, devices and empty one-past spans retain the original list
  path. This removes a region-list walk per admitted RAM lookup. Added unrun
  six-alias, partial-table, overflow, fast-only-owner and host-region oracles.
- **Audio interleave aliasing:** load both source halfwords before either
  store, and retain the last loaded values in r6/r0 rather than rereading
  potentially overwritten input. Device/callback/partial-span paths retain
  the DOL's exact scalar ordering, registers and fault PCs. Whole checked RAM
  can still coalesce publication when no external callback would observe it.
  Added unrun overlap, callback-state, dirty-tracker and partial-store checks.
- **Vector-copy effects:** distinguish the scalar and paired-single DOL
  routines, restore their volatile FPR/PS1 effects, GQR0 quantization, FPU
  retry and original partial-fault behavior through existing precise helpers.
  Added unrun literal-value, overlap, quantization and fault checks. The
  translator still excludes both entry substitutions; retained native JPA
  wrappers use the helper, but accepted-build path coverage is unverified.
- **Matrix-scale publication:** the active native helper previously published
  only on completion although its last multiply follows the first store and
  may raise an enabled FP exception. Ordered scalar helpers now publish each
  completed store, preserve exact load/store fault PCs and use the same
  precise stfs conversion as translated code. This is a correctness fix,
  with additional notification work rather than an asserted speedup. Added
  unrun overflow-after-first-store checks for callback and both dirty trackers.
- **Bulk dirty-word publication:** multi-page notifications now form exact
  masks per bitmap word instead of marking every page individually. Fully
  dirty words still avoid locked instructions; missing renderer bits use
  atomic OR so concurrent producers are retained. Partial-tracker overlap
  uses the same word masks. The scalar single-page path stays inlined and the
  bulk loop is out of line. Added an unrun byte/page oracle across six aliases,
  word edges, clipped spans and pre-existing bits. Removed work is up to 63
  bitmap loads/updates per complete 64-page word; timing is unmeasured.
- **Texture palette dependencies:** direct texture formats no longer key on
  unused TLUT format/slot registers. Identical TLUT reloads retain valid decoded
  entries, while changed bytes still retire every overlapping palette. The
  dependency recorder still observes the source read. Added unrun resource/SRV
  reuse checks across all eight direct formats plus palette format/slot,
  identical reload and changed reload cases. This can remove complete duplicate
  decode/resource/upload operations; its frequency and saving are unmeasured.
- **Texture settings lookup:** authored mips, nearest filtering, small and
  intensity-only textures no longer acquire the settings lock and copy the
  whole configuration on every cache lookup. Eligible native mip generation
  still reads the live enhanced-mipmap option. Added an unrun live-option and
  distinct-resource check. No filtering choice or output dimensions changed.

For the sustained CPU candidates, contribution and milliseconds saved are
**unmeasured**. Code inspection establishes removed work: one allocation per
eligible display-list lookup/capture, fewer decode-cache misses, variable vertex
divides, redundant key/hash/lookup work, up to seven descriptor-copy calls per
new table, and up to 63 redundant bit tests per sparse DSP bitmap word. Cache
preparation alone cannot satisfy sustained-throughput acceptance. DSP outbound
enumeration now examines two mask words plus their contiguous runs per dirty
page; indexed XF transfers eliminate one classification check per uploaded word.
The newer RAM-lookup candidate also remains unmeasured; the audio/vector fixes
do not establish that their retained helper paths were exercised in the control.
No entry substitution, scheduling boundary or game-speed change was enabled.

Recoverable local CPU/cache checkpoints: `415b7c4` (dirty tracking and GX CPU
work), `4e5da14` (known stage prewarm), `ba1c362` (exact DSP outbound spans),
and `1ca255f` (single indexed-XF validation). All are source candidates;
regression checks were written but have not run.

## Existing evidence and compact results

Historical sessions are unmatched routes/settings and intrusive monitoring was
enabled. The rates below summarize the last ten recorded windows; XFB copies
are a production proxy, not proof of distinct correctly paced simulation. Tail
values are the largest reported bound/max among those windows, not a combined
interval percentile. Renderer fields are nested selected slow-chunk costs;
they must not be added together or treated as an all-frame critical-path sample.

| Build | Hardware | Scene | Internal dimensions / output | Cache | XFB production | Tails | Relevant CPU/GPU costs | Correctness / retained change |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| preview.2; runtime `fa88b542`, game `a7e4747d` (install receipt) | Intel Arc 8086:7d55; CPU unknown | Other-PC recorded route, unmatched | 4x: backing 2560x2112, active copy 2560x1824; output 2560x1600, 16:10 | Subsequent configurations missed | Mean 6.56/s, windows 5.45–7.44/s | p95/p99 bounds up to 578.91 ms; max 578.91 ms; session max 1877.36 ms | Pipeline acquisition chunk max 1628.92 ms; GPU unmeasured | Historical control; no native THP decoding exercised |
| preview.2; runtime `e77b9e16`, game `d373c275` | Ryzen 9 9950X3D / RTX 5090 | Current-PC recorded route, unmatched | 6x: backing 3840x3168, active copy 3840x2736; output 3840x2160, 16:9 | 17 logged misses present in later cache | Mean 45.83/s, windows 39.46–55.04/s | p95/p99 bounds up to 34 ms; max 35.36 ms; session max 509.85 ms | Pipeline acquisition chunk max 405.44 ms; texture flush max 20.04 ms; GPU unmeasured | Preserved control; no native THP decoding exercised |
| This source candidate | None tested | None tested | 1x and requested higher scale pending | Preserved formats; relaunch reuse pending | Unmeasured | Unmeasured | CPU mechanisms above; GPU unmeasured | Unbuilt, untested; source checkpoint only |

The installed `e77b9e16...` runtime and retained `rt2/4245d019...` runtime have
identical 2,270,720-byte `.text` sections (SHA256
`e2d87ab7ddd16840b9a2d940868e5535c9c90673f435d5d8ab30afdf99fd7992`).
Different whole-file hashes do not establish different native instructions.
This does not establish identical data/imports/modules/settings or equivalence
to the other-PC runtime. The installed module's source/compiler pairing is not
fully recoverable from existing receipts. Future builds embed Git commit/tree,
dirty state, runtime-input fingerprint and compiler recipe; future launch records
hash the actual runtime/module files rather than trusting installation metadata.

Routine monitoring no longer opts into per-command/draw renderer timing. A
separate explicit detailed option enables those timings and existing GPU queries.
No GPU samples now report `unavailable`, with disabled/pending/measured status.
Monitoring overhead remains unmeasured. Every launcher session has a new folder.

## Deferred validation and delivery

Once execution restrictions are lifted, compile the focused FIFO, vertex,
memory-snapshot, native-API, cache-persistence and DSP checks. Check changed
headers in regenerated game/Home modules as well as the runtime: an EXE swap
does not update inline code already compiled into an older DLL. Preserve the
control and use a separate candidate package with exact hashes and build recipe.

Use an existing saved slow position/checkpoint, no repeated prologue. Compare
control/candidate at 1x then the requested scale, same actual active dimensions,
aspect/output, audio, speed and all other settings. Use monitoring off, routine,
and short detailed intervals separately. Repeat warmed intervals and second
launch cache reuse; distinguish first-use compilation. Check simulation speed,
moving new content, pacing tails, IR targeting, graphics, audio, Home, and native
THP on its existing movie route. No low-end or Dolphin acceptance is verified.
On the other PC, compare a recorded Dolphin version/settings in the same Galaxy
scene at matched active dimensions/effects and 100% speed; keep first-use and
warmed results separate. Dusklight/WiiCompiled/Aurora inform implementation,
not a matched Galaxy FPS result.

Local reference review: Aurora pipeline/depth-peek ownership and prewarm queues;
WiiCompiled register residency/call barriers; Dusklight fixed arithmetic helpers.
This batch copies no reference or game code. The existing arithmetic proofs,
interrupt boundaries, native DSP, input waits and rejected float/AVX experiments
are retained.

API constraints checked against primary documentation:
[descriptor copies](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-copydescriptors),
[null SRVs](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device-createshaderresourceview),
[pipeline-library descriptor matching](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12pipelinelibrary-loadgraphicspipeline),
and [GPU timestamp scope](https://learn.microsoft.com/en-us/windows/win32/direct3d12/timing).
Palette cache review also checked Dolphin's own
[texture-cache implementation](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/TextureCacheBase.cpp),
which includes palette content in its hash only for palettized formats. That
supports the dependency distinction; it is not a Galaxy performance comparison.
