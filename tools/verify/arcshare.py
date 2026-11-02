"""arcshare.py PID STORE: for each store file found whole in the game's block: how many of its 4 KB pages are still byte-identical to the file (could be shared with a mapped store)."""
import collections
import ctypes
import ctypes.wintypes as w
import re
import struct
import sys

PAGE = 4096


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p), ("AllocationProtect", w.DWORD), ("PartitionId", w.WORD),
                ("RegionSize", ctypes.c_size_t), ("State", w.DWORD), ("Protect", w.DWORD), ("Type", w.DWORD)]


class WSX(ctypes.Structure):
    _fields_ = [("VirtualAddress", ctypes.c_void_p), ("Flags", ctypes.c_size_t)]


k = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
k.OpenProcess.restype = w.HANDLE
k.VirtualQueryEx.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
k.VirtualQueryEx.restype = ctypes.c_size_t
k.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
psapi.QueryWorkingSetEx.argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD]
pid = int(sys.argv[1])
h = k.OpenProcess(0x0410, False, pid)
at = 0
m = MBI()
block = None
while k.VirtualQueryEx(h, ctypes.c_void_p(at), ctypes.byref(m), ctypes.sizeof(m)):
    if m.State == 0x1000 and m.Type == 0x20000 and m.RegionSize >= 0x10000000:
        block, size = m.BaseAddress, m.RegionSize
        break
    at = (m.BaseAddress or 0) + m.RegionSize
buf = bytearray()
step = 1 << 20
got = ctypes.c_size_t()
chunk = ctypes.create_string_buffer(step)
for off in range(0, size, step):
    pages = step // PAGE
    arr = (WSX * pages)()
    for i in range(pages):
        arr[i].VirtualAddress = block + off + i * PAGE
    psapi.QueryWorkingSetEx(h, arr, ctypes.sizeof(arr))
    if any(a.Flags & 1 for a in arr) and k.ReadProcessMemory(h, ctypes.c_void_p(block + off), chunk, step, ctypes.byref(got)):
        buf += chunk.raw
    else:
        buf += bytes(step)
print("block", size // 2**20, "MB, nonzero MB", sum(1 for i in range(0, len(buf), PAGE) if any(buf[i:i + 64])) * PAGE / 2**20)
f = open(sys.argv[2], "rb")
head = f.read(64)
index_at = struct.unpack_from("<Q", head, 16)[0]
count = struct.unpack_from("<Q", head, 24)[0]
f.seek(index_at)
index = f.read()
# Nodes come first in the index: position, size, name offset, parent, flags, reserved (32 bytes). Files are told from folders by flags.
print("index", len(index), "header words", struct.unpack_from("<8Q", head, 0))
nodes = []
at = 0
while at + 32 <= len(index):
    pos, length, name, parent, flags, res = struct.unpack_from("<QQIIII", index, at)
    if pos > index_at or length > index_at or res != 0:
        break
    nodes.append((pos, length, name, flags))
    at += 32
print("nodes", len(nodes))
want = {}
for pos, length, name, flags in nodes:
    if length >= 4096 and not flags & 1:
        f.seek(pos)
        key = f.read(32)
        want.setdefault(key, []).append((pos, length))
found = collections.Counter()
total_same = total_pages = 0
rows = []
i = 0
data = bytes(buf)
while i + 32 <= len(data):
    key = data[i:i + 32]
    if key in want and any(key):
        pos, length = want[key][0]
        f.seek(pos)
        file = f.read(length)
        if i + length <= len(data):
            same = sum(1 for p in range(0, length, PAGE) if data[i + p:i + p + PAGE] == file[p:p + PAGE])
            pages = (length + PAGE - 1) // PAGE
            total_same += same
            total_pages += pages
            rows.append((length, same, pages, i))
            i += length
            continue
    i += 32
print(f"files found {len(rows)}: {total_pages * PAGE / 2**20:.1f} MB, identical pages {total_same * PAGE / 2**20:.1f} MB")
for r in sorted(rows, reverse=True)[:14]:
    print(f"{r[0] / 2**20:7.2f} MB at {r[3] / 2**20:7.1f}: {r[1] * 100 // r[2]} % identical")
