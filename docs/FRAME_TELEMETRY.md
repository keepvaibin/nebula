# Galaxy frame telemetry

Normal play publishes a 256-byte local Windows snapshot approximately once a
second. A separate below-normal-priority worker reads existing atomic counters;
it never acquires a render/input/guest lock, reads GPU pixels, or logs events.
Set `GALAXY_FRAME_TELEMETRY=0` in the package environment to disable publication.
Full session monitoring remains off by default and is independent of this IPC.

The measured boundaries are **owned XFB copy production** and **first successful
game-timed Present return for each copy serial**. Neither is proof of distinct
simulation content, physical display visibility, or correctly paced 60 FPS.
Auxiliary presentations cannot inflate the first-serial count. RTSS presentation
FPS is a separate measurement. Menus, movies and moving gameplay must be measured
and labeled separately.

The mapping is `Local\Nebula.FrameTelemetry.v1.<PID>`, guarded by the same
name plus `.Guard`. Writer and reader use zero-timeout mutex acquisition; a busy
reader makes publication skip a sample, never block gameplay. Readers reject
abandoned acquisitions, wrong process creation FILETIME, unsupported/truncated
packets, inactive publishers and snapshots older than 2.5 seconds. Process handles
also reject exited processes and PID reuse. Handles are local to the Windows
session; there is no network or persistent shared log.

All words are unsigned 64-bit little-endian integers:

| Word | Meaning |
| --- | --- |
| 0..2 | Magic `0x47414c5854463031`, schema 1, size 256 bytes |
| 3..5 | PID, process creation FILETIME, publication sequence |
| 6..8 | Publication QPC, QPC frequency, active flag |
| 9..12 | XFB copies, first serial returns, game-timed returns, repeated/stale serial returns |
| 13..15 | Last presented serial, frame stamp, guest XFB address |
| 16..18 | Last copy/first-return/worker sample steady nanoseconds |
| 19..20 | Worker CPU 100ns (OS clock granularity), maximum atomic sample cost in microseconds |
| 21..23 | Initial requested output width/height and internal EFB scale; not live resize dimensions |
| 24 | Cumulative worker thread cycles, including publication and waiting overhead |
| 25..31 | Reserved, zero |

The fields are independent atomic observations with bounded sampling skew; they
are not a transactional renderer snapshot. Rates use counter differences over
the actual publication interval. First baseline, resets and long gaps do not
produce invented spikes. Zero copies in a fresh interval means zero production;
unknown or stale measurements display `--`. CPU time returning zero at OS clock
granularity is not a claim of zero overhead. Word 20 excludes the subsequent
IPC copy/guard operations; word 24 covers the worker's cumulative execution.

`tools/everon/galaxy_frame_telemetry.py` is the bounded reader. The optional
`install_galaxy_telemetry.py --root <Everon @Resources/Refined> --backup <local dir>`
validates every integration boundary, backs up existing files into a unique
directory and preserves detection overrides. Existing UTF-16 skin encoding is
retained. Restart the existing Everon helper/refresh its GameStats skin to load
the adapter. Older runtime packages correctly remain labeled present FPS.

With native IPC available, Everon shows XFB copies/s, first returns/s, RTSS
present FPS and mean copy interval separately. Exact new-frame 1%/10% lows and
percentiles are unavailable from this once-per-second interface and remain `--`.
Sparse RTSS samples are labeled sample lows for other games, never exact Galaxy
game-frame percentiles. Optional session logs retain bounded frame-tail reports
and audio/movie/Home/input summaries; they do not guarantee diagnosis of every
future slowdown.

Tests cover reader contention, resumed publication, shutdown invalidation,
stale/truncated packets, process incarnation, counter resets and copy/first-return
separation. Runtime correlation, overhead measurements and delivery identities
are recorded in the aspect/layout issue ledger and local session evidence.
