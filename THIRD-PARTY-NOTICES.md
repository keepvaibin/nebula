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

## Petari (CC0 1.0 Universal)

- Upstream: <https://github.com/SMGCommunity/Petari>, by SMGCommunity and its contributors
- Revision used: `86ae7a3`, with Nebula's changes, in `native/petari` (see its README)
- License: CC0 1.0 Universal, `native/petari/LICENSE`

Nebula's game core is Petari's decompilation of Super Mario Galaxy. CC0 does not require
attribution; this entry records the source and credits its authors.

## WiiCompiled (GPL-3.0)

- Upstream: <https://github.com/patchzyy/Wiicompiled>
- Revisions used: `83463764b8acda394e058b0c689a10b8561fc380`, `9b7b9913e4ab60b9c57fa4be56b8da308e76d06e`
- License: GNU General Public License version 3; full text in this repository's `LICENSE`

`runtime/include/galaxy/native_audio_queue.h` adapts its Wii audio interface PCM byte order.

## AMD FidelityFX SDK 1.1.4 (MIT)

The FSR 3 upscaler and its DX12 backend, built from <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>
(tag `v1.1.4`, downloaded and checked by SHA-256 at configure time) and linked into `NebulaNative.exe`. Copyright (c)
2023-2024 Advanced Micro Devices, Inc. License: MIT, the SDK's `LICENSE.txt`.

## libarchive 3.8.1, xz 5.8.1, zlib 1.3.1

`third_party/archive/` holds the libraries the mod importer reads zip, 7z and rar archives with: libarchive (BSD-2-Clause,
`third_party/archive/libarchive/COPYING`), xz's liblzma (0BSD, `third_party/archive/xz/COPYING.0BSD`) and zlib (Zlib,
`third_party/archive/zlib/LICENSE`).

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

## stb_image and stb_image_write (MIT or public domain)

`third_party/stb/` holds unmodified `stb_image.h` v2.30 and `stb_image_write.h` v1.16 from
<https://github.com/nothings/stb> (texture packs' PNG loading and texture dumps). Copyright (c) 2017 Sean Barrett,
used under the MIT alternative: `third_party/stb/LICENSE`.

## bcdec (MIT or Unlicense)

`third_party/bcdec/bcdec.h` (v0.98) from <https://github.com/iOrange/bcdec> decodes texture packs' BC1-BC5 and BC7
DDS files. Copyright (c) 2022 Sergii Kudlai, used under the MIT alternative: `third_party/bcdec/LICENSE`.

## MiiPort (ISC)

`runtime/src/mii_formats.cpp` adapts the Switch's colour and glasses downgrade tables and the 3DS/Wii U and Switch Mii
record layouts from <https://github.com/Genwald/MiiPort> (revision `15a3032dd2e929c8b3686fd357b7b86032794dac`:
`include/convert_mii.h`, `scripts/hexpats/*.hexpat`).

Copyright 2021 Genwald

Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby
granted, provided that the above copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
OF THIS SOFTWARE.

## MiiJS (ISC)

`runtime/src/mii_formats.cpp` adapts the 3DS-to-Wii part tables (`data.js` backTables), the Mii Studio, Mii Creator
and DS record layouts and the Mii Studio code's obfuscation from <https://github.com/Stewared/MiiJS> (version 3.1.0),
licensed under the ISC license (copyright Stewared; the same permission notice and disclaimer as MiiPort's
above). Its conversions were checked against Nebula's byte for byte.

## nod and other Rust crates

Setup's game importer, `nebula-import.exe` (`crates/nebula-import`), reads disc images with the
`nod` crate 1.4.4 by Luke Street (<https://github.com/encounter/nod>, MIT OR Apache-2.0). `nod`
contains the Wii common keys needed to read a Wii disc partition.

`nebula-import.exe` links 45 crates (`Cargo.lock`) under MIT, Apache-2.0, BSD-2-Clause,
BSD-3-Clause, Zlib or 0BSD terms, and the C libraries zstd (BSD-3-Clause), liblzma (0BSD) and
bzip2 (bzip2 license). Their license texts are in
[docs/licenses/rust-dependencies.txt](docs/licenses/rust-dependencies.txt). The `nod` crate ships
no license file; it declares `MIT OR Apache-2.0`.

## Vulkan renderer libraries

The Vulkan renderer (`runtime/src/gx/vk`, every platform but Windows' D3D12 build) links these,
unmodified, compiled from source:

- glslang 16.6.0, <https://github.com/KhronosGroup/glslang>: BSD-3-Clause for its core, with
  parts under BSD-2-Clause, MIT, Apache-2.0 and the other licenses its `LICENSE.txt` names.
- SPIRV-Tools, revision `ef96ed763b43b59b33b31b362f09a02b729fa1c9` (glslang's `known_good.json`),
  <https://github.com/KhronosGroup/SPIRV-Tools>: Apache-2.0.
- SPIRV-Headers, revision `496543121ce6419f23d6fa5d7194ba66c36212d2`,
  <https://github.com/KhronosGroup/SPIRV-Headers>: MIT. Copyright (c) 2015-2024 The Khronos Group Inc.
- Vulkan-Headers vulkan-sdk-1.4.350.1, <https://github.com/KhronosGroup/Vulkan-Headers>:
  Apache-2.0 OR MIT. Copyright 2015-2023 The Khronos Group Inc.
- volk vulkan-sdk-1.4.350.1, <https://github.com/zeux/volk>: MIT. Copyright (c) 2018-2026
  Arseny Kapoulkine.
- Vulkan Memory Allocator 3.4.0, <https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator>:
  MIT. Copyright (c) 2017-2026 Advanced Micro Devices, Inc.

Their license files ship with each build that uses them.

## SDL (Zlib)

SDL 3.4.18, <https://github.com/libsdl-org/SDL>, is the window, input and sound layer of the
SDL platforms (`platform/sdl`: Android, Linux, macOS), and its Java half is the Android app's
game activity. Copyright (C) 1997-2026 Sam Lantinga. License: Zlib, its `LICENSE.txt`.

## Android app

- AndroidX Core, Activity, Lifecycle and Jetpack Compose (UI, Foundation, Animation, Material 3,
  Material Icons), <https://developer.android.com/jetpack/androidx>: Apache-2.0. Copyright The
  Android Open Source Project.
- The Kotlin standard library and kotlinx.coroutines, <https://github.com/JetBrains/kotlin>,
  <https://github.com/Kotlin/kotlinx.coroutines>: Apache-2.0. Copyright JetBrains s.r.o. and
  Kotlin Programming Language contributors.
- `libnebula_import.so` (`crates/nebula-import`, setup from the player's disc image) links `nod`
  and 41 other crates; their license texts are in
  [docs/licenses/rust-dependencies-android.txt](docs/licenses/rust-dependencies-android.txt)
  (`tools/rust_licenses.py`), which the app shows under Open source licenses.

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
trademarks. Nebula is not affiliated with or endorsed by Nintendo. It distributes no Nintendo
binaries, assets, disc images or data taken from them: the game's files, and the data tables
in its `main.dol`, are read from the user's own disc.
