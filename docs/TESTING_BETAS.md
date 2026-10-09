# Separate beta releases

A branch holds source. A tagged GitHub prerelease holds the downloadable setup;
pushing a branch alone does not update installed copies.

Beta versions use `MAJOR.MINOR.PATCH-beta.N`. Publish the setup as
`Nebula-Beta-Setup.exe`, with its matching `Nebula-Beta-Setup.exe.sig`, and mark
the GitHub release as a prerelease. Do not add a `Nebula-Setup.exe` alias to a beta:
older preview updaters recognize that name and would offer the beta automatically.

The first beta requires downloading its setup explicitly. Subsequent Update
checks from that beta select only newer beta versions. Ordinary preview/stable
setups select ordinary releases and previews, excluding beta tags. Switching back
requires choosing the ordinary setup or restoring a retained installed version.
Never replace an existing published tag or its accepted release assets.

Setup contains the prebuilt runtime and recompiler. It reuses the user's compiled
modules only when their source/toolchain/recipe compatibility key matches and
their files are intact. Otherwise it regenerates and builds them from the user's
retained game inputs. An EXE-only replacement is unsafe across a native ABI change.
The release must pin the full intended source commit; this update-channel change
alone does not include later uncommitted performance/correctness fixes.

Before publishing, validate setup installation/update/rollback against the intended
source, matching runtime/modules, and retained saves/caches. A branch or successful
setup build does not establish gameplay correctness, low-end60FPS or a Dolphin win.
The current portable validation package can be tested beside the accepted install;
it has a private save/cache copy and a guided recording launcher. It is separate
from the public installer release and is not an automatic update.
