# Content Policy

Nebula source control must contain source code, metadata, scripts, tests,
and documentation only. User-owned game content and outputs derived from a dump
must stay local and untracked.

Tracked files must not include:

- Nintendo game assets or containers, including `.dol`, `.arc`, `.rso`, `.iso`,
  `.rvz`, `.wbfs`, `.szs`, `.thp`, and related disc/content formats.
- Generated native game modules, executables, object files, libraries, PDBs, or
  release binaries.
- Generated diagnostics, frame dumps, shader caches, NAND data, build trees, or
  machine-local launch logs.
- Files larger than 5 MiB, unless the policy is deliberately updated for a
  source-controlled vendor artifact.
- API keys or credentials.

Allowed examples include game IDs, hashes, symbol addresses, behavior-oriented
tests, hand-written source, and metadata tables needed by the recompiler.

`tools/audit_repo.ps1` enforces this policy on tracked files. Local generated
outputs stay in ignored directories such as `generated/`, `target/` and
`build-*/`, or in an installation outside the repository. The install-time
`RMGE01_boot_image.bin`, generated module sources, DSP program, `game.pak` and
compiled modules are dump-derived: they are produced on the user's machine and
are never tracked, attached to releases, or downloaded by the updater.

The only binary data files in the repository are the Dolphin project's
clean-room DSP ROM replacements in `third_party/dolphin-free-dsp-rom/`.
