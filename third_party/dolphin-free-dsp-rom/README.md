# Dolphin free DSP ROM replacement

`dsp_rom.bin` (instruction ROM) and `dsp_coef.bin` (coefficient ROM) are the
legal, clean-room GameCube/Wii DSP ROM replacements written by the Dolphin
Emulator Project. They contain no Nintendo code. Nebula embeds them in
`nebula-recomp`, which compiles the instruction ROM into native code alongside
the game's own DSP ucode during installation.

| File | Upstream path | Size | SHA-256 |
| --- | --- | --- | --- |
| `dsp_rom.bin` | `Data/Sys/GC/dsp_rom.bin` (v0.4) | 8192 | `4ea1fea6c649bcf9f627007bc9403d5437896c681d3e089b083263a7646cd3ae` |
| `dsp_coef.bin` | `Data/Sys/GC/dsp_coef.bin` | 4096 | `d7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179` |
| `dsp_rom_readme.txt` | `docs/DSP/free_dsp_rom/dsp_rom_readme.txt` | | upstream change history |

Source: <https://github.com/dolphin-emu/dolphin> at commit
`f87e849a0afb3092aa0e30d363e730a106c65fe6`. The binaries were last changed
upstream in `a5e2a0d97307ef146879a6f46a86d728a3ac2e97`. The assembly source
for the instruction ROM is in Dolphin's `docs/DSP/free_dsp_rom/` directory.

License: Dolphin distributes these files under GPL-2.0-or-later (see
`DOLPHIN-COPYING.txt` and `GPL-2.0-or-later.txt`). Nebula uses them under
GPL version 3, as that license permits.
