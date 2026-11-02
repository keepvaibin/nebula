"""memws.py PID [TOP]: a process's address space by committed region, with each region's working set (QueryWorkingSetEx, page by page): what is really in memory. Own processes only, read-only."""
import collections
import ctypes
import ctypes.wintypes as w
import sys

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000
TYPES = {0x20000: "private", 0x40000: "mapped", 0x1000000: "image"}
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
psapi.QueryWorkingSetEx.argtypes = [w.HANDLE, ctypes.c_void_p, w.DWORD]
psapi.GetMappedFileNameW.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_wchar_p, w.DWORD]
pid = int(sys.argv[1])
top = int(sys.argv[2]) if len(sys.argv) > 2 else 30
h = k.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, pid)
at = 0
alloc = collections.defaultdict(lambda: [0, 0, ""])
m = MBI()
while k.VirtualQueryEx(h, ctypes.c_void_p(at), ctypes.byref(m), ctypes.sizeof(m)):
    base = m.BaseAddress or 0
    size = m.RegionSize
    if m.State == MEM_COMMIT:
        pages = size // PAGE
        resident = 0
        for start in range(0, pages, 4096):
            n = min(4096, pages - start)
            arr = (WSX * n)()
            for i in range(n):
                arr[i].VirtualAddress = base + (start + i) * PAGE
            if psapi.QueryWorkingSetEx(h, arr, ctypes.sizeof(arr)):
                resident += sum(a.Flags & 1 for a in arr)
        key = (m.AllocationBase or 0, TYPES.get(m.Type, "?"))
        alloc[key][0] += size
        alloc[key][1] += resident * PAGE
        if m.Type != 0x20000 and not alloc[key][2]:
            name = ctypes.create_unicode_buffer(260)
            if psapi.GetMappedFileNameW(h, ctypes.c_void_p(base), name, 260):
                alloc[key][2] = name.value.rsplit("\\", 1)[-1]
    at = base + size
total = collections.Counter()
ws = collections.Counter()
for (base, kind), (c, r, name) in alloc.items():
    total[kind] += c
    ws[kind] += r
print("committed MB", {k2: v // 2**20 for k2, v in total.items()}, "working set MB", {k2: v // 2**20 for k2, v in ws.items()}, "total ws", sum(ws.values()) // 2**20)
small = collections.Counter()
rows = sorted(alloc.items(), key=lambda kv: -kv[1][1])
for (base, kind), (c, r, name) in rows[:top]:
    print(f"{base:#14x} {kind:8s} committed {c / 2**20:7.1f} MB  in memory {r / 2**20:7.1f} MB  {name.encode('ascii', 'replace').decode()}")
print("the rest:", sum(v[1] for _, v in rows[top:]) // 2**20, "MB in memory in", len(rows) - top, "allocations")
