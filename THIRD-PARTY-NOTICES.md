# Third-party notices

Nebula is licensed as a whole under the GNU General Public License, version 3 only
(`GPL-3.0-only`); see [LICENSE](LICENSE). It includes or adapts work from the projects below.
That work remains subject to its own license, copyright notices and disclaimers, and the
upstream projects do not endorse Nebula. [docs/PROVENANCE.md](docs/PROVENANCE.md) lists which
Nebula files each project contributed to.

## Aurora (MIT)

- Upstream: <https://github.com/encounter/aurora>
- Revisions used: `3227d76c60e1e782ca576610bce61c9e7744d8be`, `d0c931da2ed3f41d0e42736c2ab52a78c7cf1a9d`,
  `77326d45415a64c40e560cebd2cdef0a0f08d840`, `aeb38ab1fcc6a018999cd440f55b445236b736a0`,
  `1d52d36271e87d1fc2086ffb741e91df0e5351f7`

> The MIT License
>
> Copyright (c) 2022 Luke Street
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## Dusklight (CC0 1.0 Universal)

- Upstream: <https://github.com/TwilitRealm/dusklight>
- Revisions used: `40457c6adb381928e4b5fef6ed459ed291edd5e2`, `ad979d3dae092d0f5cbdaf49eabca7b4f1db4838`,
  `f8ef7d63d04f5125ab3c3d9e010f886b80c82954`, `a74d68ded7851bc577464e2ea1ab63b952bfb993`,
  `c55cc18003998e9d3a336fb538ffac958d104d1c`
- License: CC0 1.0 Universal, <https://creativecommons.org/publicdomain/zero/1.0/legalcode>

CC0 does not require attribution; this entry records the source. Dusklight's CC0 dedication
covers its contributors' own work, not its separately licensed submodules (its `extern/aurora`
submodule is Aurora, above) or the Nintendo material its decompiled sources are based on.

## WiiCompiled (GPL-3.0)

- Upstream: <https://github.com/patchzyy/Wiicompiled>
- Revisions used: `83463764b8acda394e058b0c689a10b8561fc380`, `9b7b9913e4ab60b9c57fa4be56b8da308e76d06e`
- License: GNU General Public License version 3; full text in this repository's `LICENSE`

WiiCompiled's `runtime/include/isa/ppc_isa_float.h` carries `GPL-2.0-or-later` and
"Copyright 2018 Dolphin Emulator Project". Nebula's adaptation of its paired-single arithmetic
in `runtime/src/ppc_float.cpp` is used under GPL version 3.

## Dolphin (GPL-2.0-or-later)

- Upstream: <https://github.com/dolphin-emu/dolphin>, revision `e6f3ae17627e4344da95b13424af5baf4c892b08`
- `runtime/include/galaxy/ppc_estimate.h` adapts the reciprocal and reciprocal square root estimate
  tables and interpolation from `Source/Core/Common/FloatUtils.cpp`. The test reference
  `runtime/src/ppc_estimate_reference_tests.h` adapts the same code and values from
  `Source/UnitTests/Common/FloatUtilsTest.cpp` and `Source/UnitTests/Core/PowerPC/TestValues.h`.
  Copyright 2018, 2021 Dolphin Emulator Project.
- `third_party/dolphin-free-dsp-rom/` holds the project's replacement DSP instruction and coefficient
  ROMs. Source, revision, hashes and license texts are in that folder's `README.md`.

These files are used under GPL version 3 in this `GPL-3.0-only` work.

## Berkeley SoftFloat Release 3e (BSD-3-Clause)

- Upstream: <https://www.jhauser.us/arithmetic/SoftFloat.html>
- Copyright 2011-2018 The Regents of the University of California
- License: `third_party/berkeley-softfloat-3/COPYING.txt`

## SMAA (MIT)

`third_party/smaa/` holds the unmodified `SMAA.hlsl`, `AreaTex.h` and `SearchTex.h` from
<https://github.com/iryoku/smaa>. Copyright (C) 2013 Jorge Jimenez, Jose I. Echevarria,
Belen Masia, Fernando Navarro and Diego Gutierrez. License: `third_party/smaa/LICENSE.txt`.

## AMD FidelityFX Super Resolution 1.0 (MIT)

`third_party/fidelityfx-fsr1/` holds the unmodified `ffx_a.h` and `ffx_fsr1.h` from
<https://github.com/GPUOpen-Effects/FidelityFX-FSR>. Copyright (c) 2021 Advanced Micro Devices, Inc.
License: `third_party/fidelityfx-fsr1/LICENSE.txt`.

## NVIDIA Streamline 2.14.1 headers (MIT)

`third_party/streamline/include/` holds unmodified headers from
<https://github.com/NVIDIA-RTX/Streamline> (tag v2.14.1). Copyright (c) 2022-2023 NVIDIA CORPORATION.
License: `third_party/streamline/LICENSE.txt`. The Streamline and DLSS binaries are not part of
Nebula: Setup or the settings download them from NVIDIA's release when the player asks for DLSS and
accepts the NVIDIA RTX SDKs license.

## nod and other Rust crates

`nebula-recomp.exe` reads disc images with the `nod` crate 1.4.4 by Luke Street
(<https://github.com/encounter/nod>, MIT OR Apache-2.0). `nod` contains the Wii common keys
needed to read a Wii disc partition.

`nebula-recomp.exe` links about 80 crates (`Cargo.lock`) under MIT, Apache-2.0, BSD-2-Clause,
BSD-3-Clause, Zlib, Unlicense or Unicode-3.0 terms, and the C libraries zstd (BSD-3-Clause),
liblzma (0BSD) and bzip2 (bzip2 license). Their license texts are in
[docs/licenses/rust-dependencies.txt](docs/licenses/rust-dependencies.txt), which is installed with
Nebula. The `nod` and `powerpc` crates ship no license file; both declare `MIT OR Apache-2.0`.

## Tools downloaded by Setup

Setup downloads these from their publishers and checks pinned SHA-256 hashes. Nebula does not
redistribute them.

- clang-cl and lld-link (LLVM 23, Apache-2.0 WITH LLVM-exception) from llvm-mingw 20260922,
  <https://github.com/mstorsjo/llvm-mingw>.
- Microsoft MSVC 14.44 C++ runtime headers and libraries and Windows SDK 10.0.26100 packages
  from the Visual Studio 2022 release manifest, under the Microsoft Visual Studio Build Tools
  license, which the user accepts in Setup (<https://go.microsoft.com/fwlink/?LinkId=2179911>).
  The Microsoft C++ runtime DLLs from that download are copied next to the game.
- CMake 3.31.6 (BSD-3-Clause), <https://github.com/Kitware/CMake>.
- Ninja 1.12.1 (Apache-2.0), <https://github.com/ninja-build/ninja>.

## Earlier MIT license

Nebula was developed under the name GalaxyRecomp. Revisions before the change to
`GPL-3.0-only` were published under this notice:

> MIT License
>
> Copyright (c) 2026 GalaxyRecomp contributors
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## Nintendo content

None of the licenses above apply to Nintendo software, game code, game assets, disc images or
trademarks. Nebula is not affiliated with or endorsed by Nintendo and does not distribute that
material.
