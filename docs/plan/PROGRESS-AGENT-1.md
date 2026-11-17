# Progress: Agent 1 (engine, port, gate)

One dated entry per package step. Newest last.

## 2026-11-17 E0.1 The baseline

- The 79 uncommitted files in Nebula-Clean went in as ten commits by subject, on top of 0b1982c6f: semver partial
  ranges; no player present; menu input (touch, wheel, key repeat); first-person look and the main camera's rig;
  the swap queue in pacing; pipelines warm; the benchmark at the player's settings; Android activity calls; a mod's
  waiting code asks at the title; the AO count.
- The headless host (nebula_native_host) had not compiled since 0b1982c6f: three headers it needs were included
  for renderer hosts only. Fixed in 4707a8625.
- Plan and audit added under docs/plan and docs/audit-2026-11-17.
- Checks on the baseline (builds: game, D3D12, Vulkan, D3D11 hosts, luma-test, nebula-import, 0 failures; pak store
  version matches):
  - Unit tests (rel2/avx2, MSVC 14.44, Release): 79 of 79 pass.
  - luma-validate Rust tests: 102 pass, 4 ignored (the semver partial-range test among them).
  - The 102-stop tour on Direct3D 12, hidden and silent: 101 of 102 pass. 030-FactoryGalaxy-4 setup-failed: Mario
    never gets control after 7,200 frames (P-122, the same as the audit; E2).
