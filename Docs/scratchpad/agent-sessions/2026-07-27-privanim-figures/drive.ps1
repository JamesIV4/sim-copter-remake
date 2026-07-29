param(
    [string]$Keys = "",        # e.g. "W" held
    [int]$HoldMs = 1500,
    [int]$MouseDX = 0,
    [int]$MouseDY = 0
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class IN {
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr ex; }
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr ex; }
  [StructLayout(LayoutKind.Explicit)] public struct UNION { [FieldOffset(0)] public MOUSEINPUT mi; [FieldOffset(0)] public KEYBDINPUT ki; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public UNION u; }
  [DllImport("user32.dll", SetLastError=true)] public static extern uint SendInput(uint n, INPUT[] i, int cb);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern short VkKeyScan(char c);
  public static void Key(ushort vk, bool up) {
    INPUT[] a = new INPUT[1];
    a[0].type = 1;
    a[0].u.ki.wVk = vk;
    a[0].u.ki.dwFlags = up ? 2u : 0u;
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Move(int dx, int dy) {
    INPUT[] a = new INPUT[1];
    a[0].type = 0;
    a[0].u.mi.dx = dx; a[0].u.mi.dy = dy;
    a[0].u.mi.dwFlags = 0x0001; // MOUSEEVENTF_MOVE (relative)
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@

$p = Get-Process -Name UnrealEditor, SimCopterRemake -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { Write-Output "no window"; exit 1 }
[void][IN]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 500

if ($MouseDX -ne 0 -or $MouseDY -ne 0) {
    $steps = 30
    for ($i = 0; $i -lt $steps; $i++) {
        [IN]::Move([int]($MouseDX / $steps), [int]($MouseDY / $steps))
        Start-Sleep -Milliseconds 20
    }
}

if ($Keys -ne "") {
    $vks = @()
    foreach ($c in $Keys.ToCharArray()) { $vks += [uint16]([IN]::VkKeyScan($c) -band 0xFF) }
    foreach ($vk in $vks) { [IN]::Key($vk, $false) }
    Start-Sleep -Milliseconds $HoldMs
    foreach ($vk in $vks) { [IN]::Key($vk, $true) }
}
Write-Output "done"
