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

Allowed examples include game IDs, hashes, behaviour-oriented tests and hand-written source.

`tools/audit_repo.ps1` enforces this policy on tracked files. Local outputs stay in ignored directories such as
`target/` and `build-*/`, or in an installation outside the repository. `game.pak` is packed from the user's own disc
on the user's machine and is never tracked, attached to releases or downloaded by the updater. `NebulaNative.exe` and
`smg_game.dll` ship prebuilt in Setup: they are built only from this repository's source, the game module from the
Petari decompilation in `native/petari`.
