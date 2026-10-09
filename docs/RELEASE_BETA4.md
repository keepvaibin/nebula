# Beta 4 performance and correctness checkpoint

The release packages the prebuilt public host runtime, launcher, recompiler,
and arithmetic libraries. Setup compiles only the user's game-derived modules,
linking those modules against the supplied libraries. Local portable validation
uses the same runtime and launcher extracted from the signed release installer.
The release defaults to the SSE2 x64 baseline; it does not inherit the release
machine's AVX2 requirement. No generated game source is hand-edited.

Open `Nebula-Beta-Setup.exe` to install or update. It detects the registered
installation and offers Update when its packaged version is newer, preserving
saves/settings and retaining the previous version for rollback. An existing
installation can also use Check for updates to download a newer signed beta.

Updates prefer compatible module reuse, then relinking of retained compiled
objects against the supplied public libraries. Full module recompilation is a
last resort for missing objects, incompatible ABI or unusable link inputs. New
installs retain a private object cache for future relinks. The non-game host code
always ships prebuilt. Uninstall and previous-version rollback remain available.

Retained changes include exact snapshot lookup and paired arithmetic helpers,
FIFO hashing and dependency processing, BP metadata lookup, cheaper negative DSP
polls, checked host-only link optimization, correct texture allocation budgeting,
asynchronous shader-cache persistence, shader completion notifications, persistent
EFB descriptors, and strictly admitted RGBA8 identity copies. Required shader
variants and draws still execute. Cached guest calls retain movie and pointer
entry hooks and exact external-interrupt handler ownership.

Audio endpoint recovery runs outside the gameplay owner. Endpoint loss while the
pending queue is full no longer becomes a false queue-timeout failure. Unheard
buffers retain their ownership. Replay input now publishes pointer metadata
consistently with its HID payload. The retained IOS arena bounds and interrupt
fixes address earlier Observatory failures, without claiming all interiors pass.

## Evidence and limits

| Build | Hardware | Scene/settings | Cache | Genuinely new FPS | Frame pacing/cost evidence | Correctness and scope |
|---|---|---|---|---|---|---|
| validation14 CPU batch | 9950X3D/RTX5090 | Scripted base EFB 640x528; detailed diagnostics off | Matched warmed ABBA | 53.529 to 57.212 | One candidate interval contained a 114.507 ms stall | +6.88%; movement was not visually confirmed |
| validation16 arithmetic library | Same PC | Matched base-resolution route | Warm | 57.815 to 59.456 | Not a sustained-60 acceptance | +2.84% in that comparison |
| validation24 | Same PC | Observatory; EFB 1920x1584, output 1280x720 | 854 shader pairs/818 PSOs reused | 58.102 | One 60.017 ms audio starvation episode outside timing window | No matched gain established for completion wake |
| validation31 graphics/audio/CPU integration | Same PC | Base EFB 640x528, Observatory smoke | 854 pairs/818 PSOs reused | Not qualified | GPU timings off; identity-copy path had zero admissions in this scene | Normal game exit; benchmark summary absent; no FPS claim |
| beta4 / validation32 | Same PC for build and host tests | Release candidate | User's existing cache retained | Pending user playtest | No new matched performance claim | Host, interrupt and cached-call tests pass; 416 core and 31 CLI tests pass |

Graphics boundary tests cover 231,936 identity-copy pixels and 47,872 dithering
pixels across scales 1 through 16. These are correctness tests, not gameplay
benchmarks at 16x. All three recording choices remain available. Detailed
recording can affect performance; use Lightweight for routine five-minute tests.

Later helper improvements have not yet demonstrated a matched gameplay gain.
Benchmark input/scene qualification remains incomplete. Low-end acceptance,
sustained 60 FPS across all scenes, extreme-resolution performance, and matched
Galaxy superiority over Dolphin remain unverified. Other recompilations' results
in other games are not Galaxy benchmarks.

For the laptop, test 1x (actual EFB 640x528) first. Move and turn the camera in
Star Festival, the Observatory and Good Egg where available, then close normally
and send the recording ZIP with the selected settings and any problem location.
Test higher scales separately. Keep the previous complete install and use the
whole new folder; runtime-only swaps cannot establish module compatibility.
