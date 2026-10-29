# Shader cache seed

`RMGE01/shaders.bin` and `RMGE01/psos.bin` are copies of the runtime's shader cache, recorded by
playing the game. `installer/build-release.ps1` embeds them in Setup, and Setup's
`SeedShaderCache` (`installer/native/setup_engine.cpp`) copies them to
`%LOCALAPPDATA%\Nebula\shadercache\RMGE01\` on a fresh install so the first launch does not
compile every shader. Existing cache files are never replaced.

| File | Contents |
|---|---|
| `shaders.bin` | Compiled DXBC shader pairs (`GXSC` header) generated from Nebula's own HLSL |
| `psos.bin` | Pipeline state keys (`GKPC` header): hashes of GX register state |

Neither file contains game data. Both load on any GPU: the runtime checks each record's checksum
and shader reflection data and drops records that fail. `pipelines.bin`, the driver's compiled
pipeline library, is tied to one GPU and must not be added here.

The build script compares both header versions with the constants in
`runtime/src/gx/pipeline_cache.cpp`. A `shaders.bin` with the wrong version fails the build; a
`psos.bin` with the wrong version is left out with a warning.

## Refreshing

1. Play through the scenes the seed should cover with a runtime built from this repository.
2. Copy `shaders.bin` and `psos.bin` from `%LOCALAPPDATA%\Nebula\shadercache\RMGE01\` into
   `installer/shadercache-seed/RMGE01/`.
3. Build the installer; the version check runs as part of `build-release.ps1`.
