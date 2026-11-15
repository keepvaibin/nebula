"""Counts of console-era names in the tree. Groups in CLOSED must read zero; the pre-push hook runs this.
console_census.py [TREE]. Input parsers of the importer and the remote edge are the only places allowed to name a format."""
import os
import re
import sys

TREE = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ROOTS = ("native", "runtime", "crates", "tools/dev", "docs")
SKIP = (
    "crates/nebula-import/src/cook/model_ir.rs",
    "crates/nebula-import/src/cook/clip.rs",
    "crates/nebula-import/src/cook/mod.rs",
    "crates/nebula-import/src/cook/formats.rs",
    "crates/nebula-import/src/cook/native_data.rs",
    "crates/nebula-import/src/cook/mii_parts.rs",
    "crates/nebula-import/src/cook/sound.rs",
    "runtime/include/galaxy/mii_formats.h",
    "runtime/src/mii_formats.cpp",
    "docs/",
    "native/tools/native_inventory.py",
    "native/tools/boundary.py",
)
EXT = {".cpp", ".hpp", ".h", ".c", ".inc", ".hlsl", ".hlsli", ".rs", ".py", ".cmake", ".txt", ".md", ".S", ".s"}

GROUPS = {
    "math": r"\bJGeometry|\bJMath\b|\bJMA[A-Z][a-z]|\bTVec[234]|\bTPos3|\bTRot3|\bTMtx34|\bTSRT|\bTBox[23]|\bTUtil\b|\bTRandom_fast_|\bTDirBox|\bTQuat4|\bTPartialMtx|\bPSMTX|\bPSVEC|\bPSQUAT",
    "j3d": r"j3d",
    "audio": r"\bJAS[A-Z]|\bJAI[A-Z]|\bJAU[A-Z]|\bJAudio|\bJAudio2|\bJAud",
    "particles": r"\bJPA[A-Z]|\bJPAC",
    "jut": r"\bJUT[A-Z]|\bJSU[A-Z]|\bJKR[A-Z]|\bJMap|\bJSUInput|\bJUTException",
    "jsystem": r"jsystem",
    "nw4r": r"nw4r",
    "rfl": r"\bRFL[A-Z_]|\brfl[A-Z_]|MiiLib",
    # Archives are found or loaded when asked for, so the loaders, archive lists and mounts are gone.
    "loading": r"\bStationed|makeArchiveList|mountArchive|\bFileLoader\b|\bStageFileLoader|\bArchiveHolder\b|\bFileHolder\b|\brequestMount|\bisLoadedFile|\breceiveFile|ArchiveListCollector|\bmountAsync|\bloadToMainRAM|\bisReadResourceFromStore",
    # GX/GP calls and the command FIFO; not a name that starts with "GPU" (D3D12's GPUUploadHeapSupported) or Vulkan's
    # FIFO present mode.
    "fifo": r"\bGX[A-Z]\w+|\bGP(?!U)[A-Z]\w+|\b(?:GX|gx|cmd|Cmd|command|Command)_?(?:FIFO|[Ff]ifo)\b|\b[Ff]ifo\b|DisplayList",
    "sjis": r'"(?:\\[0-7]{3}){2,}',
    "offsets": r"\bfield_0x|\b_[0-9A-Fa-f]{2,3}\b(?!\w)",
    # Batch 5: the audio thread's lock and the console's sound pools.
    "audio_sync": r"\bGameAudioExclusion|\bexcludeAudio|\benterAudio|\bleaveAudio|\brestoreAudio|NEBULA_HOST_LOCK_AUDIO|\bsAudioThread|PoolAllocObject|\bSpkMixingBuffer|\bAudRhythmWrap|\bAudMeSeq",
    # Batch 7: one game thread, so its locks, thread slots and the host's thread services.
    "threads": r"\bGameMutex|\bMutexHolder|\bGameThreads|\bthread_slots|\bNEBULA_THREAD_SLOTS|\bNebulaHostThread|\bthread_adopt|\bthreads_dump|\bnebula_threads|\bsListMutex|\bsMappedLock|\bnebula_host_lock|\bNEBULA_HOST_LOCK",
}
CLOSED = {"math", "j3d", "audio", "audio_sync", "particles", "threads", "loading", "rfl", "fifo"}
CASE_FREE = {"j3d", "jsystem", "nw4r"}

compiled = {k: re.compile(v, re.I if k in CASE_FREE else 0) for k, v in GROUPS.items()}
counts = {k: 0 for k in GROUPS}
worst = {k: {} for k in GROUPS}
SELF = "tools/verify/console_census.py"
for root in ROOTS:
    for d, _, names in os.walk(os.path.join(TREE, root)):
        for n in names:
            if os.path.splitext(n)[1] not in EXT:
                continue
            path = os.path.join(d, n)
            rel = os.path.relpath(path, TREE).replace("\\", "/")
            if rel == SELF or any(rel.startswith(s) for s in SKIP):
                continue
            text = open(path, encoding="utf-8", errors="replace").read()
            for k, r in compiled.items():
                c = len(r.findall(text)) + len(r.findall(rel))
                if c:
                    counts[k] += c
                    worst[k][rel] = c
bad = 0
for k in GROUPS:
    mark = "closed" if k in CLOSED else "open"
    print(f"{k:10} {counts[k]:7} {mark}")
    if k in CLOSED and counts[k]:
        bad += counts[k]
        for rel, c in sorted(worst[k].items(), key=lambda x: -x[1])[:20]:
            print(f"    {c} {rel}")
sys.exit(1 if bad else 0)
