"""memblock.py PID [STEP_MB]: the game's 512 MB block, how much of each stretch is in memory and how much of that holds anything but zeros.
Sound memory starts about 2 MB in (128 MB), then the stationed heap, then the game heap."""
import ctypes
import ctypes.wintypes as w
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
step = int(sys.argv[2]) * 2**20 if len(sys.argv) > 2 else 8 * 2**20
h = k.OpenProcess(0x0410, False, pid)
at = 0
block = None
m = MBI()
while k.VirtualQueryEx(h, ctypes.c_void_p(at), ctypes.byref(m), ctypes.sizeof(m)):
    if m.State == 0x1000 and m.Type == 0x20000 and m.RegionSize >= 0x10000000:
        block = m.BaseAddress
        size = m.RegionSize
        break
    at = (m.BaseAddress or 0) + m.RegionSize
if block is None:
    print("no game block")
    sys.exit(1)
total = 0
for off in range(0, size, step):
    pages = step // PAGE
    arr = (WSX * pages)()
    for i in range(pages):
        arr[i].VirtualAddress = block + off + i * PAGE
    psapi.QueryWorkingSetEx(h, arr, ctypes.sizeof(arr))
    resident = [i for i in range(pages) if arr[i].Flags & 1]
    nonzero = 0
    buf = ctypes.create_string_buffer(PAGE)
    got = ctypes.c_size_t()
    for i in resident:
        if k.ReadProcessMemory(h, ctypes.c_void_p(block + off + i * PAGE), buf, PAGE, ctypes.byref(got)) and buf.raw.count(0) != PAGE:
            nonzero += 1
    total += len(resident)
    if resident:
        print(f"{off / 2**20:6.0f} MB  in memory {len(resident) * PAGE / 2**20:6.1f}  not all zero {nonzero * PAGE / 2**20:6.1f}")
print(f"total {total * PAGE / 2**20:.1f} MB")
