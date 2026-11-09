param([string[]]$Keys, [int]$HoldMs = 150, [int]$X = -1, [int]$Y = -1, [string]$Process = 'NebulaRuntime')
# Focus the game window, optionally move the mouse (client coordinates), then hold keys together.
# Every key goes in as a scan code (KEYEVENTF_SCANCODE, KEYEVENTF_EXTENDEDKEY for the extended ones), as a real
# keyboard sends it, so position-bound keys (movement) behave in tests as they do for a player.
# Key names: letters a-z, digits 0-9, space, enter, esc, tab, shift, lshift, rshift, ctrl, lctrl, rctrl, alt, left,
# up, right, down. Example: send_keys.ps1 -Keys w,space -HoldMs 300
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class ScanKeys {
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public UIntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public UIntPtr extra; }
  [StructLayout(LayoutKind.Explicit)] public struct InputUnion { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public InputUnion u; }
  public struct POINT { public int X, Y; }
  [DllImport("user32.dll", SetLastError = true)] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint type);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr c);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int i);
  // One key down or up as a scan code. 'extended' keys (arrows, right ctrl/alt, ...) carry the 0xE0 prefix flag.
  public static void Key(ushort vk, bool extended, bool up) {
    uint scan = MapVirtualKeyW(vk, 0);
    var i = new INPUT[1];
    i[0].type = 1;
    i[0].u.ki.wVk = 0;
    i[0].u.ki.wScan = (ushort)scan;
    i[0].u.ki.dwFlags = 0x0008u | (extended ? 0x0001u : 0u) | (up ? 0x0002u : 0u);
    SendInput(1, i, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
# name -> virtual key, extended
$map = @{
  space = @(0x20, $false); enter = @(0x0D, $false); esc = @(0x1B, $false); tab = @(0x09, $false)
  shift = @(0xA0, $false); lshift = @(0xA0, $false); rshift = @(0xA1, $false)
  ctrl = @(0xA2, $false); lctrl = @(0xA2, $false); rctrl = @(0xA3, $true); alt = @(0xA4, $false)
  left = @(0x25, $true); up = @(0x26, $true); right = @(0x27, $true); down = @(0x28, $true)
}
foreach ($c in [char[]]'abcdefghijklmnopqrstuvwxyz') { $map["$c"] = @([int][char]::ToUpper($c), $false) }
foreach ($c in [char[]]'0123456789') { $map["$c"] = @([int]$c, $false) }
[void][ScanKeys]::SetProcessDpiAwarenessContext([IntPtr](-4))
$h = (Get-Process $Process | Select-Object -First 1).MainWindowHandle
[void][ScanKeys]::SetForegroundWindow($h); Start-Sleep -Milliseconds 300
if ($X -ge 0) {
  $p = New-Object ScanKeys+POINT; $p.X = $X; $p.Y = $Y
  [void][ScanKeys]::ClientToScreen($h, [ref]$p)
  $sw = [ScanKeys]::GetSystemMetrics(0); $sh = [ScanKeys]::GetSystemMetrics(1)
  for ($i = 1; $i -le 12; $i++) {
    $tx = $p.X - 60 + 5 * $i
    [ScanKeys]::mouse_event(0x8001, [int]($tx * 65535 / ($sw - 1)), [int]($p.Y * 65535 / ($sh - 1)), 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 40
  }
  Start-Sleep -Milliseconds 600
}
foreach ($k in $Keys) {
  $entry = $map[$k.ToLowerInvariant()]
  if ($null -eq $entry) { throw "unknown key name: $k" }
  [ScanKeys]::Key([uint16]$entry[0], [bool]$entry[1], $false)
}
Start-Sleep -Milliseconds $HoldMs
foreach ($k in $Keys) {
  $entry = $map[$k.ToLowerInvariant()]
  [ScanKeys]::Key([uint16]$entry[0], [bool]$entry[1], $true)
}
