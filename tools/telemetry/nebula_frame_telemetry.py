"""Read Nebula's bounded Windows IPC; XFB serials are not content/simulation proof."""
from __future__ import annotations

import ctypes
import struct

MAGIC = 0x47414C5854463031
PACKET = struct.Struct("<32Q")


def decode_packet(data, pid, creation, now_qpc):
    if len(data) != PACKET.size:
        return None
    values = PACKET.unpack(data)
    if (values[:3] != (MAGIC, 1, PACKET.size) or values[3] != pid
            or values[4] != creation or not values[5] or values[8] != 1
            or not values[7] or values[6] > now_qpc
            or now_qpc - values[6] > values[7] * 2.5):
        return None
    return values


class Rates:
    def __init__(self):
        self.previous = None
        self.last = None

    def accept(self, packet):
        previous = self.previous
        if (previous is None or previous[3:5] != packet[3:5]
                or packet[5] < previous[5] or packet[6] < previous[6]
                or any(packet[i] < previous[i] for i in (9, 10, 11, 12))):
            self.previous, self.last = packet, None
            return None
        if packet[5] == previous[5]:
            return self.last
        self.previous = packet
        seconds = (packet[6] - previous[6]) / packet[7]
        if not 0.5 <= seconds <= 3.0:
            self.last = None
            return None
        self.last = {
            "copy_hz": (packet[9] - previous[9]) / seconds,
            "first_return_hz": (packet[10] - previous[10]) / seconds,
            "game_return_hz": (packet[11] - previous[11]) / seconds,
            "repeat_hz": (packet[12] - previous[12]) / seconds,
            "session": f"{packet[3]}:{packet[4]}",
            "sequence": packet[5],
            "worker_cpu_100ns": packet[19],
            "sample_max_us": packet[20],
            "worker_cycles": packet[24],
            "copy_age_ms": max(0, packet[18] - packet[16]) / 1000000 if packet[16] else None,
            "first_return_age_ms": max(0, packet[18] - packet[17]) / 1000000 if packet[17] else None,
        }
        return self.last


class Client:
    """No file logs, allocation on game threads, or blocking IPC acquisition."""
    def __init__(self):
        self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        for name in ("OpenFileMappingW", "OpenMutexW", "OpenProcess"):
            getattr(self.kernel, name).restype = ctypes.c_void_p
        self.kernel.OpenFileMappingW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
        self.kernel.OpenMutexW.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_wchar_p]
        self.kernel.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
        self.kernel.MapViewOfFile.restype = ctypes.c_void_p
        self.kernel.MapViewOfFile.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                           ctypes.c_uint32, ctypes.c_uint32, ctypes.c_size_t]
        self.kernel.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
        self.kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        self.kernel.ReleaseMutex.argtypes = [ctypes.c_void_p]
        self.kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        self.kernel.GetProcessTimes.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(ctypes.c_uint64)] * 4
        self.kernel.QueryPerformanceCounter.argtypes = [ctypes.POINTER(ctypes.c_int64)]
        self.pid = 0
        self.mapping = self.guard = self.view = self.process = None
        self.creation = 0
        self.rates = Rates()

    def close(self):
        if self.view:
            self.kernel.UnmapViewOfFile(self.view)
        for handle in (self.mapping, self.guard, self.process):
            if handle:
                self.kernel.CloseHandle(handle)
        self.mapping = self.guard = self.view = self.process = None
        self.pid, self.creation = 0, 0
        self.rates = Rates()

    def read(self, pid):
        if self.pid != pid:
            self.close()
        if not self.view:
            name = f"Local\\Nebula.FrameTelemetry.v1.{pid}"
            self.mapping = self.kernel.OpenFileMappingW(4, False, name)
            self.guard = self.kernel.OpenMutexW(0x100001, False, name + ".Guard")
            self.process = self.kernel.OpenProcess(0x101000, False, pid)
            if not self.mapping or not self.guard or not self.process:
                self.close()
                return None
            self.view = self.kernel.MapViewOfFile(self.mapping, 4, 0, 0, PACKET.size)
            creation, exit_time, kernel, user = (ctypes.c_uint64() for _ in range(4))
            if (not self.view or not self.kernel.GetProcessTimes(self.process,
                    ctypes.byref(creation), ctypes.byref(exit_time), ctypes.byref(kernel), ctypes.byref(user))):
                self.close()
                return None
            self.creation, self.pid = creation.value, pid
        if self.kernel.WaitForSingleObject(self.process, 0) != 258:
            self.close()  # Dead process, including retained handles/PID reuse.
            return None
        acquired = self.kernel.WaitForSingleObject(self.guard, 0)
        if acquired not in (0, 128):
            return None
        try:
            if acquired == 128:
                return None  # Abandoned writer may have left a partial packet.
            data = ctypes.string_at(self.view, PACKET.size)
        finally:
            self.kernel.ReleaseMutex(self.guard)
        now = ctypes.c_int64()
        self.kernel.QueryPerformanceCounter(ctypes.byref(now))
        packet = decode_packet(data, pid, self.creation, now.value)
        if packet is None:
            self.rates = Rates()
            return None
        rates = self.rates.accept(packet)
        if rates is not None:
            rates = dict(rates, age_ms=(now.value-packet[6])*1000/packet[7])
        return rates
