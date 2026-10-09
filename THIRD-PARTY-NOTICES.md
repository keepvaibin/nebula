# Third-Party Notices

The experimental fused paired arithmetic in `runtime/src/ppc_float.cpp`
adapts the packed single-lane arithmetic approach of WiiCompiled's
`runtime/include/isa/ppc_isa_float.h::{PpcAddPairInline,PpcSubPairInline,PpcMulPairInline}`
at revision `9b7b9913e4ab60b9c57fa4be56b8da308e76d06e`.
That exact header carries `GPL-2.0-or-later` and Copyright 2018 Dolphin
Emulator Project, separately from WiiCompiled's root license. Galaxy adds
its own eligibility proofs, exact binary64 status lanes, register layout,
software fallback and precise exception/commit boundary. The modified combined
work selects GPL version 3 and is conveyed under `GPL-3.0-only`.

Nebula is licensed as a whole under the GNU General Public License,
version 3 only (`GPL-3.0-only`); see `LICENSE`. It incorporates or adapts work
from the projects identified below. Upstream material remains subject to its
applicable license, copyright notices, and disclaimers. Nebula's changes
are not endorsed by the upstream projects.

## Dusklight â€” CC0 1.0 Universal

The opt-in DSP stereo adapter additionally adapts the current
`src/dusk/audio/DuskDsp.cpp::ApplyPanning` no-signal routing policy at revision
`40457c6adb381928e4b5fef6ed459ed291edd5e2` (SHA-256
`C5C7FCC2FC8B9FF4C6DD50812E2301BB0B52350A7448766185ACDDB22E3BEA54`).
Galaxy accepts its two connected auxiliary buses only when current and target
gain are both zero; audible auxiliary/effect routes and unknown addresses
remain unsupported. This modification does not import Dusklight's Freeverb.

The synthetic button adapter in `runtime/include/galaxy/synthetic_kpad_buttons.h`
adapts the frame-owned current/previous press calculation from
`src/dusk/action_bindings.cpp::{updateActionBindings,getActionBindTrig}` at
revision `40457c6adb381928e4b5fef6ed459ed291edd5e2` (source SHA-256
`96B15A8E3099701A61613597003E133CCFB6CD050DFB6CB28754E5A32E3F95F3`).
Galaxy uses packed Wii button masks and preserves rising edges between the
100 Hz host producer and the guest KPADRead consumer. The Win32 key-down
acquisition latch is independently written. Multiple taps within one guest
read coalesce; neither an arbitrary hold duration nor a repeat timer is added.

- Upstream: <https://github.com/TwilitRealm/dusklight>
- Audited revisions: `f8ef7d63d04f5125ab3c3d9e010f886b80c82954`,
  `ad979d3dae092d0f5cbdaf49eabca7b4f1db4838` (JAudio safety guard and
  AFC decoder), and source comparison revisions
  `a74d68ded7851bc577464e2ea1ab63b952bfb993` and
  `c55cc18003998e9d3a336fb538ffac958d104d1c`
- Upstream license file: `LICENSE.md`
- License: CC0 1.0 Universal

Nebula incorporates or adapts portions of Dusklight made available under
CC0 1.0 Universal. CC0 does not require attribution; this notice records the
source and exact revision for provenance.

Dusklight's top-level CC0 dedication does not automatically apply to its Git
submodules or other material carrying separate license terms. In particular,
its `extern/aurora` submodule is separately licensed under the MIT License and
is documented below.
The `src/d/`, `src/m_Do/` and `libs/JSystem/` game-derived code requires
file-level rights review before copying; this notice does not treat the root
CC0 declaration as proof of rights in underlying Twilight Princess material.

The CC0 1.0 Universal legal code is available at
<https://creativecommons.org/publicdomain/zero/1.0/legalcode>.

## Aurora â€” MIT License

- Upstream: <https://github.com/encounter/aurora>
- Independently audited upstream revision:
  `d0c931da2ed3f41d0e42736c2ab52a78c7cf1a9d`
- Additional source revision fetched 2026-09-22:
  `77326d45415a64c40e560cebd2cdef0a0f08d840`
- Earlier full-source comparison revision and current Dusklight submodule:
  `aeb38ab1fcc6a018999cd440f55b445236b736a0`
- Current standalone source comparison revision:
  `1d52d36271e87d1fc2086ffb741e91df0e5351f7`
- Earlier Dusklight submodule revision:
  `d0933b745abe0eb9815bedcea8047575da18698d`
- Copyright (c) 2022 Luke Street

The bounded native THP experiment in `runtime/src/native_thp_video.cpp` adapts
`lib/dolphin/thp/THPDec.cpp` and the bounded reader subset of `lib/internal.hpp`
at `3227d76c60e1e782ca576610bce61c9e7744d8be`. Galaxy adds input/output bounds,
EOF propagation, dimension checks and a documented optional wrapper HLE boundary.

These revision identifiers distinguish earlier incorporated bytes from the
latest source comparison and the earlier Dusklight submodule. Aurora code
incorporated or adapted by Nebula remains covered by the following MIT
notice:

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

## WiiCompiled â€” GNU General Public License version 3

The synthetic KPAD motion adapter added September 30, 2026 in
`runtime/include/galaxy/synthetic_kpad_motion.h` adapts `Length`, `Distance`,
`WriteStatus` and the raw/KPAD axis conversion in `WriteUnifiedStatus` from
`runtime/src/hle/input/kpad.cpp` at revision
`9b7b9913e4ab60b9c57fa4be56b8da308e76d06e`. Galaxy uses its own calibrated
synthetic 10-bit accelerometer sample and checked guest stores. The adapted
implementation preserves shake pulses through the existing keyboard/mouse
and controller KPAD HLE boundary; WiiCompiled's no-IR policy is not copied.

- Upstream: <https://github.com/patchzyy/Wiicompiled>
- Audited revision: `8769cf6dea663549983ab70b6c3b0c4ae15cc7b0`
- Adapted source revision: `83463764b8acda394e058b0c689a10b8561fc380`
- Upstream license file: `LICENSE`
- License: GNU General Public License version 3

Nebula incorporates or adapts portions of WiiCompiled. Nebula's
adaptations and the combined work are conveyed under `GPL-3.0-only`; the full
license text is in this repository's `LICENSE`. Imported source-level notices
must remain intact, and modified versions must be identified as modified as
required by GPLv3 section 5.

## Berkeley SoftFloat Release 3e â€” BSD 3-Clause

Nebula vendors Berkeley SoftFloat Release 3e from John R. Hauser's
official distribution for deterministic IEEE-754 operations.

- Upstream: <https://www.jhauser.us/arithmetic/SoftFloat.html>
- Local license: `third_party/berkeley-softfloat-3/COPYING.txt`
- Copyright 2011-2018 The Regents of the University of California
- License: BSD 3-Clause

SoftFloat is used as an arithmetic primitive. PowerPC-specific FPSCR behavior,
NaN propagation, paired-single quantization, and architectural exceptions are
implemented and tested separately by Nebula.

## Prior MIT notice

Nebula was developed under the earlier name GalaxyRecomp. Revisions predating
the GPL-3.0-only relicensing were published with the following MIT notice,
reproduced verbatim. It is preserved here for attribution and license
history; the current project distribution is licensed under `GPL-3.0-only`.

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

## Dolphin free DSP ROM replacement â€” GPL-2.0-or-later

`third_party/dolphin-free-dsp-rom/` holds the Dolphin Emulator Project's
clean-room DSP instruction and coefficient ROMs (no Nintendo code). They are
embedded in `nebula-recomp` and compiled into each local installation. Source,
pinned revision, hashes and license texts are in that folder's `README.md`.

## nod â€” MIT OR Apache-2.0

`nebula-recomp` reads ISO, RVZ, WIA, WBFS, CISO and GCZ images with the `nod`
crate 1.4.4 by Luke Street (<https://github.com/encounter/nod>), used under the
MIT license. Like Dolphin, `nod` contains the Wii retail and debug common-key
constants needed to decrypt a user's own Wii disc partition. Nebula does not
assert any position on the legal status of those constants beyond their
publication by these upstream projects; see `docs/PROVENANCE.md`.

## Other Rust crates

`nebula-recomp.exe` statically links about 80 crates (Cargo.lock), all under
MIT, Apache-2.0, BSD-2/3-Clause, Zlib, Unlicense or Unicode-3.0 terms, plus the
C libraries zstd (BSD-3-Clause), liblzma (0BSD) and bzip2 (bzip2 license)
vendored by their `-sys` crates. Their license texts are collected in
`docs/licenses/rust-dependencies.txt`, which is also installed with Nebula.
The `nod` and `powerpc` crates ship no license file; both declare
`MIT OR Apache-2.0`.

## Tools downloaded at install time (not distributed by Nebula)

Nebula-Setup downloads these from their publishers and verifies pinned SHA-256
values; Nebula does not redistribute them:

- clang-cl and lld-link (LLVM 23, Apache-2.0 WITH LLVM-exception) from the
  llvm-mingw 20260922 release, <https://github.com/mstorsjo/llvm-mingw>.
- Microsoft MSVC 14.44 C++ runtime headers and libraries and Windows SDK
  10.0.26100 packages from the Visual Studio 2022 release manifest, under the
  Microsoft Visual Studio Build Tools license, which the user accepts in Setup
  (<https://go.microsoft.com/fwlink/?LinkId=2179911>). The Microsoft C++
  runtime DLLs from that download are copied next to the game.
- CMake 3.31.6 (BSD-3-Clause) from <https://github.com/Kitware/CMake>.
- Ninja 1.12.1 (Apache-2.0) from <https://github.com/ninja-build/ninja>.

## Nintendo and user-supplied content

No license or public-domain dedication identified in this file applies to
Nintendo software, game code, game assets, disc images, trademarks, or other
user-supplied game content. Nebula is an independent project, is not
affiliated with or endorsed by Nintendo, and does not convey those materials.

## Selective bloom reference

The default-off experimental policy was independently implemented using the
Dolphin native-resolution bloom documentation and SuperMarioGalaxy graphics
mod metadata as behavioral references. No Dolphin implementation is linked.
Reference: https://dolphin-emu.org/blog/2022/07/07/dolphin-progress-report-may-and-june-2022/
 and https://github.com/dolphin-emu/dolphin/tree/master/Data/Sys/Load/GraphicMods/Super%20Mario%20Galaxy .
Dolphin is GPL-2.0-or-later.

## Scaled display-copy filtering reference

The high-resolution XFB filter footprint correction was independently written
after consulting Dolphin's TextureCacheBase.cpp scaled VRAM pixel-height policy
and its 2018 copy-filter documentation. No Dolphin implementation was copied
or linked for this correction. Dolphin source is GPL-2.0-or-later.
https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/TextureCacheBase.cpp
https://dolphin-emu.org/blog/2018/06/03/dolphin-progress-report-april-and-may-2018/

## Broadway floating-point estimate tables — GPL-2.0-or-later

`runtime/include/galaxy/ppc_estimate.h` adapts the reciprocal and reciprocal-
square-root estimate tables and interpolation from Dolphin's
`Source/Core/Common/FloatUtils.cpp` at revision
`e6f3ae17627e4344da95b13424af5baf4c892b08`, Copyright 2018 Dolphin Emulator
Project, GPL-2.0-or-later. WiiCompiled's `runtime/include/isa/ppc_isa_float.h`
at `6fa24737d3362b6cba8162671745262751f457b8` uses the same tables.

The test-only `runtime/src/ppc_estimate_reference_tests.h` adapts that numerical
reference plus `Source/UnitTests/Common/FloatUtilsTest.cpp` and
`Source/UnitTests/Core/PowerPC/TestValues.h` from the same Dolphin revision,
Copyright 2018, 2021 Dolphin Emulator Project, GPL-2.0-or-later. Upstream notices
are retained. The combined work selects GPL version 3 and is conveyed under
Nebula's GPL-3.0-only license. No retail game code or content is included here.
