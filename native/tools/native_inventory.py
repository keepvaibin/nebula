"""native_inventory.py <repo root> <out dir>: every tracked file of Nebula, what console machinery it still holds and
what it needs to become native. Writes inventory.tsv (one row per file) and inventory.md (totals per signal and per
area, then every file that still holds a signal, grouped by area). The native port is finished when every signal
other than the release-hygiene ones counts zero and no file is left in a replace or delete area.

Signals are counted per file from source text, comments excluded: calls into the console's SDK (GX, GD, OS, VI, DVD,
NAND, WPAD/KPAD, AI/DSP/AR, cache and processor calls, SYSCONF, NWC24, HBM, RSO, RFL, THP, TPL, MTX/VEC), console
byte order and addresses, the renderer's emulation of the console (command stream, register state, EFB/XFB, TEV,
precision), the host's console shapes (guest memory, Wii Remote reports, the DSP program, the disc image) and test
hooks, which must compile out of release builds."""
import collections
import os
import re
import subprocess
import sys

root, out = sys.argv[1], sys.argv[2]

SIGNALS = [
    ("gx", r"\bGX[A-Z][A-Za-z0-9_]*\s*\(", "GX call"),
    ("gx_type", r"\bGX(?:TexObj|TlutObj|Color|ColorS10|LightObj|FifoObj|RenderModeObj|Texture|TexFmt|TevStageID|"
                r"Attr|AttrType|CompType|CompCnt|Primitive|VtxFmt|TexMapID|TexCoordID|ChannelID|BlendFactor|"
                r"CompareMode|ProjectionType|CullMode|FogType|IndTexStageID)\b", "GX type"),
    ("gd", r"\bGD[A-Z][A-Za-z0-9_]*\s*\(", "GD display-list build"),
    ("mtx", r"\b(?:PSMTX|PSVEC|PSQUAT|C_MTX|C_VEC|C_QUAT|MTX|VEC|QUAT)[A-Z][A-Za-z0-9_]*\s*\(", "SDK matrix/vector call"),
    ("os_thread", r"\bOS(?:CreateThread|ResumeThread|SuspendThread|SleepThread|WakeupThread|CancelThread|"
                  r"JoinThread|DetachThread|ExitThread|YieldThread|GetCurrentThread|InitThreadQueue|SetThreadPriority|"
                  r"InitMutex|LockMutex|UnlockMutex|TryLockMutex|InitCond|WaitCond|SignalCond|InitMessageQueue|"
                  r"SendMessage|ReceiveMessage|JamMessage|CreateAlarm|SetAlarm|SetPeriodicAlarm|CancelAlarm|"
                  r"InitSemaphore|WaitSemaphore|SignalSemaphore)\s*\(", "OS thread/sync/alarm"),
    ("os_interrupt", r"\b(?:OSDisableInterrupts|OSRestoreInterrupts|OSEnableInterrupts|__OSSetInterruptHandler|"
                     r"__OSMaskInterrupts|__OSUnmaskInterrupts)\s*\(", "interrupt lock"),
    ("os_time", r"\bOS(?:GetTime|GetTick|TicksToCalendarTime|TicksTo[A-Za-z]+|[A-Za-z]+ToTicks|GetSystemTime)\b|"
                r"\bOS_TIMER_CLOCK\b|\bOS_BUS_CLOCK\b", "console time base"),
    ("os_other", r"\bOS(?!CreateThread|ResumeThread|SuspendThread|SleepThread|WakeupThread|CancelThread|JoinThread|"
                 r"DetachThread|ExitThread|YieldThread|GetCurrentThread|InitThreadQueue|SetThreadPriority|InitMutex|"
                 r"LockMutex|UnlockMutex|TryLockMutex|InitCond|WaitCond|SignalCond|InitMessageQueue|SendMessage|"
                 r"ReceiveMessage|JamMessage|CreateAlarm|SetAlarm|SetPeriodicAlarm|CancelAlarm|InitSemaphore|"
                 r"WaitSemaphore|SignalSemaphore|DisableInterrupts|RestoreInterrupts|EnableInterrupts|GetTime|"
                 r"GetTick|Report|Panic|CachedToPhysical|PhysicalToCached|UncachedToPhysical|Ticks)"
                 r"[A-Z][A-Za-z0-9_]*\s*\(", "other OS call"),
    ("os_report", r"\b(?:OSReport|OSPanic|OSFatal|OSHalt)\s*\(", "console log/panic"),
    ("address", r"\bOS(?:CachedToPhysical|PhysicalToCached|UncachedToPhysical|PhysicalToUncached)\b|"
                r"\b0[xX](?:8|9|[Cc]|[Dd])[0-9A-Fa-f]{7}\b", "console address"),
    ("cache", r"\b(?:DC|IC|LC)[A-Z][A-Za-z0-9]*\s*\(|\bPPC[A-Z][A-Za-z0-9]*\s*\(", "cache/processor call"),
    ("vi", r"\bVI[A-Z][A-Za-z0-9_]*\s*\(|\bwaitForTick\b|\bmRetraceCount\b", "VI/retrace"),
    ("disc", r"\b(?:DVD[A-Z][A-Za-z0-9_]*|ARC[A-Z][A-Za-z0-9_]*)\s*\(|\bAR(?:Alloc|Free|Init|StartDMA|GetBaseAddress|"
             r"GetSize|Reset|CheckInit|QueueRequest|GetDMAStatus|SetSize)\s*\(|\bARQ[A-Z]\w*\s*\(|"
             r"\bJKRAram\w*|\bJKRDvd\w*|\bJKRDecomp\b|\bYaz0\b|\bSZS\b|\bdecompSZS\w*", "disc/ARAM/Yaz0"),
    ("nand", r"\b(?:NAND|ISFS|VF)[A-Z][A-Za-z0-9_]*\s*\(", "NAND"),
    ("input", r"\b(?:WPAD|KPAD|WUD|WENC)[A-Z][A-Za-z0-9_]*\s*\(|\bPAD(?:Read|Init|Reset|Clamp)\w*\s*\(",
     "Wii Remote/pad SDK"),
    ("audio", r"\b(?:AI|DSP|AX)[A-Z][A-Za-z0-9_]*\s*\(", "AI/DSP"),
    ("system", r"\b(?:SC|NWC24|HBM|RSO|ES|IPC)[A-Z][A-Za-z0-9_]*\s*\(", "SYSCONF/NWC24/HOME/RSO"),
    ("rfl", r"\bRFL[A-Za-z0-9_]*\s*\(", "Mii library"),
    ("thp_tpl", r"\b(?:THP|TPL)[A-Z][A-Za-z0-9_]*\s*\(", "THP/TPL"),
    ("byte_order", r"\bNEBULA_BE\b|\bNEBULA_OFFSET\b|\bBE\s*<|\bBEOffset\s*<|\bSelfRel\s*<|\bbswap\w*|"
                   r"\b__builtin_bswap\w*|\bbyteswap\w*|\bRFLI_(?:LD|ST)\w*", "big-endian access"),
    ("hw_register", r"\b__(?:GXData|cpReg|peReg|piReg|memReg|VIRegs|DSPRegs|AIRegs|EXIRegs|SIRegs|PIRegs|MEMRegs)\b|"
                    r"\b__OSPhysicalMem\w*", "hardware register block"),
    ("ppc_semantics", r"\b__nebula_(?:divw|divwu|mask)\b|\b__frsqrte\b|\b__fres\b|\bfctiwz\b|\bpaired[- ]single\b",
     "PowerPC arithmetic"),
    ("dol", r"\bdisc_dol\b|\bnebula_dol\w*|\b__NebulaDol\w*|\bmain\.dol\b", "main.dol data"),
    ("fifo", r"\bfifo\b|\bFIFO\b|\bFifo[A-Z]\w*|\bon_fifo\w*|\bgx_submit\b|\bcommand ring\b|\bBP register\b|"
             r"\bCP register\b|\bXF register\b|\bkBp[A-Z]\w*|\bkCp[A-Z]\w*|\bkXf[A-Z]\w*|\bGxState\b",
     "command stream/register state"),
    ("efb_xfb", r"\bEFB\b|\befb_\w+|\bXFB\b|\bxfb_\w+|\bkEfb\w+|\bcopy_efb\w*|\bpeek_efb\w*|\bgx_peek_efb\b",
     "EFB/XFB emulation"),
    ("tev", r"\bTEV\b|\btev_\w+|\bTev[A-Z]\w*|\bkTev\w*|\bswap table\b|\bkonst\b|\bindirect stage\b",
     "TEV emulation"),
    ("precision", r"\bRGBA6\b|\brgba6\b|\bdither\w*|\bDither\w*|\bZ24\b|\bz24\b|\bzcomploc\b|\bZCompLoc\b|"
                  r"\b6-bit\b|\bs10\b", "console precision"),
    ("gx_texture", r"\bCMPR\b|\bRGB5A3\b|\bC14X2\b|\bIA4\b|\bIA8\b|\bTLUT\b|\btlut\w*|\bdecode_(?:i4|i8|ia4|ia8|"
                   r"rgb565|rgb5a3|cmpr|c4|c8|c14)\w*", "GX texture format"),
    ("guest", r"\b[Gg]uest\w*", "recompiler-era guest model"),
    ("wii_report", r"\breport_\w+|\bwpad_report\b|\bWpadReport\w*|\bIR dot\w*|\bir_dot\w*|\bEEPROM\b|\beeprom\w*",
     "Wii Remote report model"),
    ("dsp_program", r"\bDSP data memory\b|\bIRAM\b|\bDRAM\b|\bucode\b|\bCH_BUF\b|\bFX_BUF\b|\bDSPRES_FILTER\b|"
                    r"\baudio_dsp\b|\bai_dma\b", "DSP program model"),
    ("test_hook", r"\bGALAXY_(?:TEST|TRACE|EXPORT|DEBUG|DUMP)_[A-Z0-9_]+|\bNEBULA_TEST_[A-Z0-9_]+",
     "test/trace hook (release hygiene)"),
    ("decomp_residue", r"\bTARGET_PC\b|\b__MWERKS__\b|\basm\s*\{|\b__asm\b|\bnofralloc\b|\bfralloc\b",
     "decompilation residue: console/native dual paths, CodeWarrior assembly"),
]
COMPILED = [(k, re.compile(p), d) for k, p, d in SIGNALS]
HYGIENE = {"test_hook", "os_report"}
SOURCE_EXT = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".inc", ".hlsl", ".hlsli", ".glsl", ".metal",
              ".rs", ".cs", ".kt", ".java", ".py", ".ps1", ".sh", ".cmake", ".txt", ".kts", ".gradle"}

# Areas, first match wins: (path regex, area, disposition, what to do). Dispositions: REPLACE (the area is console
# machinery to be removed in favour of a native design), CONVERT (native code with console signals to rewrite),
# KEEP (native; rework only where it holds signals), THIRD (vendored), DOC, DATA.
AREAS = [
    (r"^third_party/", "third-party", "THIRD", "Vendored; keep license notices; update only for new native features."),
    (r"^native/petari/src/RVL_SDK/gx/|^native/petari/src/RVL_SDK/gd/", "sdk-gx", "REPLACE",
     "Decompiled GX/GD: writes the hardware command stream. Delete once nothing calls GX/GD (native records)."),
    (r"^native/petari/src/RVL_SDK/os/", "sdk-os", "REPLACE",
     "Decompiled OS scheduler, alarms, mutexes, messages, arena: replace with host threads, sync, time and memory."),
    (r"^native/petari/src/RVL_SDK/dvd/|^native/petari/src/RVL_SDK/arc/", "sdk-disc", "REPLACE",
     "Disc file system and RARC: replace with the cooked asset store."),
    (r"^native/petari/src/RVL_SDK/kpad/|^native/petari/src/RVL_SDK/wenc/", "sdk-input", "REPLACE",
     "KPAD pointer filtering and WENC speaker encoding: replace with the native input and speaker paths."),
    (r"^native/petari/src/RVL_SDK/mem/", "sdk-mem", "REPLACE", "MEM heaps: replace with native allocation."),
    (r"^native/petari/libs/RVL_SDK/include/private/", "sdk-headers-private", "REPLACE",
     "Hardware register layouts: delete with the SDK."),
    (r"^native/petari/libs/RVL_SDK/", "sdk-headers", "REPLACE",
     "SDK headers: shrink to the native types the game keeps (Mtx/Vec as the native math library), delete the rest."),
    (r"^native/sdk/gx/", "shim-gx", "REPLACE", "GX FIFO ring and markers: delete with the command stream."),
    (r"^native/sdk/os/", "shim-os", "REPLACE",
     "Single-host-thread scheduler, stack switching, time base, interrupts, division traps: replace with native "
     "threads/jobs, host time, source-level arithmetic fixes."),
    (r"^native/sdk/vi/", "shim-vi", "REPLACE", "Retrace emulation: replace with swapchain presentation and pacing."),
    (r"^native/sdk/dvd/", "shim-disc", "REPLACE", "Disc reads with slots and interrupts: replace with native async IO."),
    (r"^native/sdk/audio/", "shim-audio", "REPLACE", "AI/DSP/ARAM shims: replace with the native mixer interface."),
    (r"^native/sdk/nand/", "shim-nand", "REPLACE", "NAND file system: replace with native save files."),
    (r"^native/sdk/wpad/", "shim-input", "REPLACE", "Wii Remote report decoding: replace with the native input API."),
    (r"^native/sdk/thp/", "shim-thp", "REPLACE", "THP decode glue: replace with native video decode."),
    (r"^native/sdk/tpl/", "shim-tpl", "REPLACE", "TPL relocation: textures come cooked."),
    (r"^native/sdk/hbm/", "shim-hbm", "CONVERT", "HOME Menu entry points: keep Nebula's panel as a native screen."),
    (r"^native/sdk/misc/", "shim-system", "REPLACE", "SYSCONF and NWC24: native settings; drop the message board."),
    (r"^native/sdk/mtx/", "shim-mtx", "REPLACE", "Paired-single-shaped math: replace with a native SIMD math library."),
    (r"^native/sdk/stubs/", "shim-stubs", "REPLACE", "Stubs for unreachable SDK code: delete with the SDK."),
    (r"^native/sdk/(?:memory|dol_data|core|region)\.c$", "shim-core", "REPLACE",
     "Console arena, DOL data, register blocks, region: native memory, source tables, cooked region data."),
    (r"^native/sdk/", "shim-other", "REPLACE", "SDK shim layer: replace."),
    (r"^native/msl/include/nebula_be\.hpp$", "byte-order", "REPLACE", "BE<T>/BEOffset<T>/SelfRel<T>: delete after the cooker."),
    (r"^native/msl/include/nebula_mtx\.h$", "shim-mtx", "REPLACE", "Inline paired-single-shaped math: native SIMD."),
    (r"^native/msl/", "msl", "KEEP", "The game's C library interface: keep native; trim console-only parts."),
    (r"^native/include/nebula_game_api\.h$", "host-api", "CONVERT",
     "Console-shaped services (gx_*, disc_*, vi_*, fs_request, sc_get, wpad_*, audio_dsp/ai_*, thp, gpu_coherent, "
     "memory_regions): replace with native renderer/asset/display/save/settings/input/audio/video interfaces."),
    (r"^native/host/", "host-module-bridge", "CONVERT",
     "The services the module calls: rewrite against the native interfaces."),
    (r"^native/tools/", "tools", "KEEP", "Audits: keep the ones that still apply; delete console-only audits at the end."),
    (r"^native/petari/src/JSystem/J3DGraph|^native/petari/libs/JSystem/include/JSystem/J3DGraph", "j3d", "CONVERT",
     "J3D: shapes/materials/matrices/animation. Native shapes done; native materials compiled at load, matrix arena, "
     "packets as native draw records; loaders read cooked native data."),
    (r"^native/petari/src/JSystem/J2DGraph|^native/petari/libs/JSystem/include/JSystem/J2DGraph", "j2d", "CONVERT",
     "J2D panes/pictures/text: native 2D records at window resolution."),
    (r"^native/petari/src/JSystem/JParticle|^native/petari/libs/JSystem/include/JSystem/JParticle", "jpa", "CONVERT",
     "JPA particles: native particle records (instanced), cooked JPC data."),
    (r"^native/petari/src/JSystem/JAudio2|^native/petari/libs/JSystem/include/JSystem/JAudio2", "jaudio", "CONVERT",
     "JAudio2: keep the sound logic (JAI*, JAS sequencing); replace the DSP/AI/ARAM side with the native mixer."),
    (r"^native/petari/src/JSystem/JKernel|^native/petari/libs/JSystem/include/JSystem/JKernel", "jkernel", "CONVERT",
     "JKernel: heaps sized natively, archives from the cooked store, no ARAM, no Yaz0, JKRThread on host threads."),
    (r"^native/petari/src/JSystem/JUtility|^native/petari/libs/JSystem/include/JSystem/JUtility", "jutility", "CONVERT",
     "JUtility: video/XFB go, fonts/textures/palettes become native resources, debug consoles out of release."),
    (r"^native/petari/src/JSystem/|^native/petari/libs/JSystem/", "jsystem-support", "CONVERT",
     "JSystem support (JMath, JGeometry, JSupport, JGadget, JFramework): native math/SIMD, cooked data."),
    (r"^native/petari/src/nw4r/lyt|^native/petari/libs/nw4r/include/nw4r/lyt", "nw4r-lyt", "CONVERT",
     "Layouts: native 2D records; cooked BRLYT/BRLAN."),
    (r"^native/petari/src/nw4r/ut|^native/petari/libs/nw4r/include/nw4r/ut", "nw4r-ut", "CONVERT",
     "Fonts and resources: cooked BRFNT as native textures; the ROM font replaced."),
    (r"^native/petari/src/nw4r/db|^native/petari/libs/nw4r/include/nw4r/db", "nw4r-db", "CONVERT",
     "Debug console/exception: native crash reports; out of release."),
    (r"^native/petari/src/nw4r/|^native/petari/libs/nw4r/", "nw4r-other", "CONVERT", "nw4r math: native math library."),
    (r"^native/petari/src/RVLFaceLib/|^native/petari/libs/RVLFaceLib/", "mii", "CONVERT",
     "Mii library: native database file, faces rendered with native passes, no NAND, no big-endian loads."),
    (r"^native/petari/(?:src|include)/Game/System/", "game-system", "CONVERT",
     "Game systems: main loop, frame control, files, saves, NAND, SYSCONF, WPad, errors, reset: native platform."),
    (r"^native/petari/(?:src|include)/Game/Screen/", "game-screen", "CONVERT",
     "Screens and image effects: native 2D and native passes for every EFB-copy effect."),
    (r"^native/petari/(?:src|include)/Game/Util/", "game-util", "CONVERT",
     "MR utilities: draw helpers to native records, file/memory/math/system utilities to native."),
    (r"^native/petari/(?:src|include)/Game/(?:AudioLib|GameAudio|Speaker|RhythmLib)/", "game-audio", "CONVERT",
     "Game audio glue: native mixer API; speaker sound native."),
    (r"^native/petari/(?:src|include)/Game/NWC24/", "game-nwc24", "REPLACE", "Message board: remove or native."),
    (r"^native/petari/(?:src|include)/Game/", "game", "CONVERT",
     "Gameplay: native; only the console signals listed for the file change (draws, math, data access)."),
    (r"^native/petari/include/nebula_disc\.hpp$", "game-system", "CONVERT",
     "The game's disc access helpers: files from the cooked asset store."),
    (r"^native/petari/", "petari-other", "KEEP", "Petari metadata."),
    (r"^native/", "native-build", "KEEP", "Module build and benches."),
    (r"^runtime/src/gx/vk/|^runtime/include/galaxy/gx/vk/", "renderer-vulkan", "CONVERT",
     "Vulkan backend: draw from native records; descriptor indexing, push constants, dynamic rendering."),
    (r"^runtime/(?:src|include/galaxy)/gx/(?:fifo_parser|gx_state|gx_shim|gx_bitfields|gx_headers|vertex_loader|"
     r"memory_snapshot|snapshot_region|sorted_dirty|frame_completion|xf_lighting|line_point_raster|efb_copy|"
     r"efb_conversion|xfb_present|shader_keys|shader_gen|uber_|rt_tev|sampler_key|texture_cache|"
     r"texture_backend|upload_ring|placed_texture|vs_constants|simd_kernel)", "renderer-translation", "REPLACE",
     "GX translation: command parsing, register state, GX vertex/texture decoding, EFB/XFB copies, TEV/XF shader "
     "emulation, guest-memory change detection. Replace with the native record renderer; reuse parts that are "
     "native already (upload ring, placed allocator, texture backend) after removing GX shapes from them."),
    (r"^runtime/(?:src|include/galaxy)/(?:gx/gx_backend|gx/native_shapes|gx_d3d12)", "renderer-core", "CONVERT",
     "Backend between the game and the GPU: becomes the native record consumer (native shapes stay)."),
    (r"^runtime/(?:src|include/galaxy)/gx/(?:renderer_d3d12|renderer_common|pipeline_cache|render_config|"
     r"display_modes|window_owner|vblank_clock)", "renderer-d3d12", "CONVERT",
     "D3D12 backend and infrastructure: bindless, root constants, one frame graph, native targets."),
    (r"^runtime/(?:src|include/galaxy)/gx/(?:ray_tracing|rt_)", "feature-rt", "CONVERT",
     "Ray tracing: rebuild on native shapes/materials/lights/instances (phase 4)."),
    (r"^runtime/(?:src|include/galaxy)/gx/(?:frame_interpolation|interp_gpu|transform_history|temporal_upscaler|"
     r"streamline_bridge)", "feature-temporal", "CONVERT",
     "Interpolation, upscalers, frame generation: exact per-object motion from native data (phase 2)."),
    (r"^runtime/(?:src|include/galaxy)/gx/(?:post_process|game_bloom|ambient_occlusion|enhanced_lighting|"
     r"curved_surfaces|game_lights|game_objects|galaxy_select)", "feature-graphics", "CONVERT",
     "Graphics features: on native targets and native data (phase 2)."),
    (r"^runtime/(?:src|include/galaxy)/gx/(?:texture_pack|movie_replacement|game_asset_extract)", "feature-packs", "CONVERT",
     "Texture packs and HD movies: names from the cooker (Dolphin hashes of original data), native textures/video."),
    (r"^runtime/(?:src|include/galaxy)/gx/host_window_win32", "host-ui-win32", "CONVERT",
     "Window, settings and achievements overlay: move the UI model to shared code for every host."),
    (r"^runtime/(?:src|include/galaxy)/gx/", "renderer-other", "CONVERT", "Renderer: native records."),
    (r"^runtime/(?:src|include/galaxy)/jaudio_host_mixer", "host-dsp", "REPLACE",
     "Host copy of the JAudio DSP program: replace with a native float mixer."),
    (r"^runtime/(?:src|include/galaxy)/(?:native_host|wii_device_data|virtual_disc|native_thp_video|guest_memory|"
     r"synthetic_kpad_buttons)",
     "host-console-model", "REPLACE",
     "Input as Wii Remote reports, virtual device data, disc image, software THP: replace with native input, "
     "cooked assets and native video."),
    (r"^runtime/(?:src|include/galaxy)/(?:wiimote_|controller_|generic_hid|switch_controller|playstation_|"
     r"steam_controller|native_input|input_bindings|pointer_control|host_pointer|rumble|tilt)", "host-input-devices",
     "KEEP", "Device drivers: feed the native input API directly (no reports)."),
    (r"^runtime/(?:src|include/galaxy)/(?:mods|mod_import|mod_xml)", "host-mods", "CONVERT",
     "Mods: Riivolution files resolved by the cooker/loader; code patches as native hooks."),
    (r"^runtime/(?:src|include/galaxy)/(?:mii_)", "host-mii", "CONVERT", "Mii import into the native database."),
    (r"^runtime/", "host-runtime", "KEEP", "Host runtime: native; check its signals."),
    (r"^platform/", "platform-hosts", "KEEP", "SDL and Android hosts: kept building; ported in phase 7."),
    (r"^crates/", "importer", "CONVERT", "Importer: add the asset cooker (native formats) at import."),
    (r"^installer/", "installer", "KEEP", "Windows Setup: runs the importer/cooker; update flow."),
    (r"^docs/", "docs", "DOC", "Update with every change; mod-making docs after approval."),
    (r"^localization/", "localization", "DATA", "Nebula's own text."),
    (r"^tools/", "dev-tools", "KEEP", "Developer tools."),
    (r"", "root", "KEEP", "Build and repo files."),
]
AREAS = [(re.compile(p), a, d, w) for p, a, d, w in AREAS]
# Requirement IDs per area (the requirements document's map from areas to requirements).
REQUIREMENTS = {
    "game": "B-01..B-07", "game-system": "B-10..B-17", "game-screen": "B-20..B-22, E-12", "game-util": "B-25, B-26",
    "game-audio": "B-30, H", "game-nwc24": "B-31", "j3d": "B-40, E-04, E-05", "j2d": "B-41, E-12", "jpa": "B-42",
    "jaudio": "B-43, H", "jkernel": "B-44, D-01", "jutility": "B-45", "jsystem-support": "B-46, C-08",
    "nw4r-lyt": "B-50", "nw4r-ut": "B-51", "nw4r-db": "B-52", "nw4r-other": "B-52", "mii": "B-55",
    "petari-other": "B-04, P-05", "msl": "C-07", "byte-order": "C-05", "host-api": "C-06",
    "host-module-bridge": "C-06", "native-build": "C-09", "tools": "N-03", "dev-tools": "N-03",
    "renderer-translation": "E-02", "renderer-core": "E-01, E-03", "renderer-d3d12": "E-04..E-17",
    "renderer-vulkan": "E-04..E-17", "renderer-other": "E-04..E-17", "feature-graphics": "F",
    "feature-temporal": "F", "feature-rt": "G", "feature-packs": "D-05, D-06, M-04", "host-ui-win32": "L-01",
    "host-dsp": "H-01", "host-console-model": "M-02, I-01, I-07", "host-input-devices": "I-02..I-06",
    "host-mods": "D-04, M-03", "host-mii": "B-55, M-03", "host-runtime": "M-01, K", "platform-hosts": "S, L-01",
    "importer": "N-01, D-01..D-07", "installer": "N-02, D-07", "docs": "R", "localization": "L-02",
    "third-party": "P-05", "root": "Q-06, R-01",
}


def strip_comments(text, ext):
    if ext in {".py", ".sh", ".ps1", ".cmake", ".txt"}:
        return re.sub(r"(?m)#.*$", "", text)
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


files = subprocess.run(["git", "-C", root, "ls-files"], capture_output=True, text=True, check=True).stdout.split("\n")
files = [f for f in files if f]
rows = []
for rel in files:
    area, disp, what = next((a, d, w) for p, a, d, w in AREAS if p.search(rel))
    if area.startswith("sdk-"):
        reqs = "B-70"
    elif area.startswith("shim-"):
        reqs = "C-01..C-05, C-08"
    else:
        reqs = REQUIREMENTS[area]
    ext = os.path.splitext(rel)[1].lower()
    counts = collections.Counter()
    lines = 0
    if ext in SOURCE_EXT and disp != "THIRD":
        try:
            raw = open(os.path.join(root, rel), encoding="utf-8", errors="replace").read()
        except OSError:
            raw = ""
        lines = raw.count("\n")
        body = strip_comments(raw, ext)
        for key, rx, _ in COMPILED:
            n = len(rx.findall(body))
            if n:
                counts[key] = n
    rows.append((rel, area, disp, lines, counts, what, reqs))

os.makedirs(out, exist_ok=True)
keys = [k for k, _, _ in SIGNALS]
with open(os.path.join(out, "inventory.tsv"), "w", encoding="utf-8", newline="\n") as f:
    f.write("path\tarea\tdisposition\trequirements\tlines\t" + "\t".join(keys) + "\taction\n")
    for rel, area, disp, lines, counts, what, reqs in rows:
        f.write(f"{rel}\t{area}\t{disp}\t{reqs}\t{lines}\t" + "\t".join(str(counts.get(k, 0)) for k in keys) +
                f"\t{what}\n")

totals = collections.Counter()
files_with = collections.Counter()
by_area = collections.OrderedDict()
for rel, area, disp, lines, counts, what, reqs in rows:
    totals.update(counts)
    for k in counts:
        files_with[k] += 1
    a = by_area.setdefault(area, {"disp": disp, "what": what, "reqs": reqs, "files": 0, "lines": 0,
                                  "signals": collections.Counter(), "flagged": []})
    a["files"] += 1
    a["lines"] += lines
    a["signals"].update(counts)
    if counts and any(k not in HYGIENE for k in counts) or disp == "REPLACE":
        a["flagged"].append((rel, lines, counts))
    else:
        a.setdefault("clean", []).append(rel)

desc = {k: d for k, _, d in SIGNALS}
with open(os.path.join(out, "inventory.md"), "w", encoding="utf-8", newline="\n") as f:
    f.write("# Native inventory\n\n")
    f.write(f"{len(rows)} tracked files. REPLACE: console machinery to remove; CONVERT: native code holding console "
            "signals; KEEP: native; THIRD: vendored.\n\n## Signals\n\n| signal | meaning | matches | files |\n|---|---|---|---|\n")
    for k in keys:
        f.write(f"| {k} | {desc[k]} | {totals.get(k, 0)} | {files_with.get(k, 0)} |\n")
    f.write("\n## Areas\n\n| area | disposition | requirements | files | lines | signals (matches) |\n"
            "|---|---|---|---|---|---|\n")
    for area, a in by_area.items():
        sig = ", ".join(f"{k} {n}" for k, n in a["signals"].most_common())
        f.write(f"| {area} | {a['disp']} | {a['reqs']} | {a['files']} | {a['lines']} | {sig} |\n")
    for area, a in by_area.items():
        if not a["flagged"]:
            continue
        f.write(f"\n## {area} ({a['disp']}; {a['reqs']})\n\n{a['what']}\n\n")
        for rel, lines, counts in sorted(a["flagged"]):
            sig = ", ".join(f"{k} {n}" for k, n in counts.most_common()) or "whole file"
            f.write(f"- `{rel}` ({lines} lines): {sig}\n")
    f.write("\n## Every other file\n\nNo console signal found. Each is still covered by its area's requirements and may "
            "change when its area does.\n")
    for area, a in by_area.items():
        if not a.get("clean"):
            continue
        f.write(f"\n### {area} ({a['reqs']})\n\n")
        folders = collections.OrderedDict()
        for rel in sorted(a["clean"]):
            folders.setdefault(os.path.dirname(rel) or ".", []).append(os.path.basename(rel))
        for folder, names in folders.items():
            f.write(f"- `{folder}/`: " + ", ".join(names) + "\n")
print(f"{len(rows)} files; " + ", ".join(f"{k} {totals.get(k, 0)}/{files_with.get(k, 0)}" for k in keys))
