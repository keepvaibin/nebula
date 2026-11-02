"""threadprof.py EXE_PATH_PART THREAD_NAME_PART SECONDS PDB_DIR [--wait S]: samples one thread of a running process from outside (my own runs only),
walks its stack with dbghelp and prints the functions by self and by inclusive share. Works on any frozen build whose pdb matches."""
import ctypes, ctypes.wintypes as w, sys, time, collections
K = ctypes.WinDLL('kernel32', use_last_error=True)
D = ctypes.WinDLL('C:/Windows/System32/dbghelp.dll', use_last_error=True)
PSAPI = ctypes.WinDLL('psapi')
exe_part, name_part, seconds, pdb_dir = sys.argv[1].lower(), sys.argv[2].lower(), float(sys.argv[3]), sys.argv[4]
wait = float(sys.argv[sys.argv.index('--wait') + 1]) if '--wait' in sys.argv else 0
K.OpenProcess.restype = w.HANDLE
K.OpenThread.restype = w.HANDLE
K.GetCurrentProcess.restype = w.HANDLE
def find_process():
    arr = (w.DWORD * 8192)(); n = w.DWORD()
    PSAPI.EnumProcesses(arr, ctypes.sizeof(arr), ctypes.byref(n))
    for pid in arr[:n.value // 4]:
        h = K.OpenProcess(0x1000, 0, pid)
        if not h:
            continue
        buf = ctypes.create_unicode_buffer(1024); sz = w.DWORD(1024)
        ok = K.QueryFullProcessImageNameW(w.HANDLE(h), 0, buf, ctypes.byref(sz))
        K.CloseHandle(w.HANDLE(h))
        if ok and exe_part in buf.value.lower():
            return pid, buf.value
    return None, None
end = time.time() + 180
while True:
    pid, path = find_process()
    if pid or time.time() > end:
        break
    time.sleep(0.5)
assert pid, 'no process'
print('process', pid, path)
time.sleep(wait)
class TE(ctypes.Structure):
    _fields_ = [('dwSize', w.DWORD), ('cntUsage', w.DWORD), ('th32ThreadID', w.DWORD), ('th32OwnerProcessID', w.DWORD), ('tpBasePri', w.LONG), ('tpDeltaPri', w.LONG), ('dwFlags', w.DWORD)]
K.CreateToolhelp32Snapshot.restype = w.HANDLE
snap = K.CreateToolhelp32Snapshot(4, 0)
te = TE(); te.dwSize = ctypes.sizeof(TE)
target = None
ok = K.Thread32First(w.HANDLE(snap), ctypes.byref(te))
K.GetThreadDescription.argtypes = [w.HANDLE, ctypes.POINTER(ctypes.c_wchar_p)]
while ok:
    if te.th32OwnerProcessID == pid:
        h = K.OpenThread(0x0002 | 0x0008 | 0x0040 | 0x0200 | 0x0400, 0, te.th32ThreadID)
        if h:
            s = ctypes.c_wchar_p()
            K.GetThreadDescription(w.HANDLE(h), ctypes.byref(s))
            if s.value and name_part in s.value.lower():
                target = h
                print('thread', te.th32ThreadID, s.value)
                break
            K.CloseHandle(w.HANDLE(h))
    ok = K.Thread32Next(w.HANDLE(snap), ctypes.byref(te))
assert target, 'no thread'
proc = K.OpenProcess(0x1F0FFF, 0, pid)
D.SymSetOptions(0x2 | 0x10 | 0x4)  # undecorate, load lines off, deferred
D.SymInitializeW.argtypes = [w.HANDLE, w.LPCWSTR, w.BOOL]
assert D.SymInitializeW(proc, pdb_dir, 1)
class M128(ctypes.Structure):
    _fields_ = [('a', ctypes.c_ulonglong * 2)]
class CONTEXT(ctypes.Structure):
    _pack_ = 16
    _fields_ = [('P', ctypes.c_ulonglong * 6), ('ContextFlags', w.DWORD), ('MxCsr', w.DWORD), ('Seg', w.WORD * 6), ('EFlags', w.DWORD), ('Dr', ctypes.c_ulonglong * 6),
                ('Rax', ctypes.c_ulonglong), ('Rcx', ctypes.c_ulonglong), ('Rdx', ctypes.c_ulonglong), ('Rbx', ctypes.c_ulonglong), ('Rsp', ctypes.c_ulonglong), ('Rbp', ctypes.c_ulonglong),
                ('Rsi', ctypes.c_ulonglong), ('Rdi', ctypes.c_ulonglong), ('R', ctypes.c_ulonglong * 8), ('Rip', ctypes.c_ulonglong), ('rest', ctypes.c_byte * 976)]
class ADDR(ctypes.Structure):
    _fields_ = [('Offset', ctypes.c_ulonglong), ('Segment', w.WORD), ('Mode', w.DWORD)]
class KD(ctypes.Structure):
    _fields_ = [('b', ctypes.c_ulonglong * 14)]
class FRAME(ctypes.Structure):
    _fields_ = [('PC', ADDR), ('Ret', ADDR), ('Frame', ADDR), ('Stack', ADDR), ('BStore', ADDR), ('Fn', ctypes.c_void_p), ('Params', ctypes.c_ulonglong * 4), ('Far', w.BOOL), ('Virtual', w.BOOL), ('Res', ctypes.c_ulonglong * 3), ('KD', KD)]
D.SymFunctionTableAccess64.restype = ctypes.c_void_p
D.SymFunctionTableAccess64.argtypes = [w.HANDLE, ctypes.c_ulonglong]
D.SymGetModuleBase64.restype = ctypes.c_ulonglong
D.SymGetModuleBase64.argtypes = [w.HANDLE, ctypes.c_ulonglong]
D.StackWalk64.argtypes = [w.DWORD, w.HANDLE, w.HANDLE, ctypes.POINTER(FRAME), ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
fta = ctypes.cast(ctypes.WinDLL('C:/Windows/System32/dbghelp.dll').SymFunctionTableAccess64, ctypes.c_void_p)
gmb = ctypes.cast(ctypes.WinDLL('C:/Windows/System32/dbghelp.dll').SymGetModuleBase64, ctypes.c_void_p)
class SYM(ctypes.Structure):
    _fields_ = [('SizeOfStruct', w.ULONG), ('TypeIndex', w.ULONG), ('Reserved', ctypes.c_ulonglong * 2), ('Index', w.ULONG), ('Size', w.ULONG), ('ModBase', ctypes.c_ulonglong), ('Flags', w.ULONG), ('Value', ctypes.c_ulonglong), ('Address', ctypes.c_ulonglong), ('Register', w.ULONG), ('Scope', w.ULONG), ('Tag', w.ULONG), ('NameLen', w.ULONG), ('MaxNameLen', w.ULONG), ('Name', ctypes.c_char * 512)]
D.SymFromAddr.argtypes = [w.HANDLE, ctypes.c_ulonglong, ctypes.POINTER(ctypes.c_ulonglong), ctypes.POINTER(SYM)]
cache = {}
def name(addr):
    n = cache.get(addr)
    if n is None:
        s = SYM(); s.SizeOfStruct = 88; s.MaxNameLen = 500; d = ctypes.c_ulonglong()
        if D.SymFromAddr(proc, addr, ctypes.byref(d), ctypes.byref(s)):
            n = s.Name.decode('latin1')
        else:
            n = hex(addr)
        cache[addr] = n
    return n
selfc = collections.Counter(); incl = collections.Counter(); paths = collections.Counter(); total = 0
endt = time.time() + seconds
th = w.HANDLE(target)
ctxbuf = ctypes.create_string_buffer(ctypes.sizeof(CONTEXT) + 64)
off = (-ctypes.addressof(ctxbuf)) & 15
while time.time() < endt:
    if K.SuspendThread(th) == 0xFFFFFFFF:
        break
    ctx = CONTEXT.from_buffer(ctxbuf, off)
    ctx.ContextFlags = 0x10000B
    stack = []
    if K.GetThreadContext(th, ctypes.byref(ctx)):
        f = FRAME()
        f.PC.Offset = ctx.Rip; f.PC.Mode = 3; f.Frame.Offset = ctx.Rbp; f.Frame.Mode = 3; f.Stack.Offset = ctx.Rsp; f.Stack.Mode = 3
        for _ in range(40):
            if not D.StackWalk64(0x8664, proc, th, ctypes.byref(f), ctypes.byref(ctx), None, fta, gmb, None) or f.PC.Offset == 0:
                break
            stack.append(f.PC.Offset)
    K.ResumeThread(th)
    if stack:
        names = [name(a) for a in stack]
        total += 1
        selfc[names[0]] += 1
        for n in set(names):
            incl[n] += 1
        paths[' < '.join(names[:5])] += 1
    time.sleep(0.003)
print('samples', total)
print('--- self')
for n, c in selfc.most_common(25):
    print('%5.1f%% %s' % (100 * c / max(total, 1), n[:110]))
print('--- inclusive')
for n, c in incl.most_common(40):
    print('%5.1f%% %s' % (100 * c / max(total, 1), n[:110]))
print('--- paths')
for n, c in paths.most_common(12):
    print('%5.1f%% %s' % (100 * c / max(total, 1), n[:220]))
