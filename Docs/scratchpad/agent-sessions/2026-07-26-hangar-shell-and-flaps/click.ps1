param([int]$X, [int]$Y)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct CRECT { public int Left, Top, Right, Bottom; }
public struct CPOINT { public int X, Y; }
public class W32K {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref CPOINT p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
}
"@ -ErrorAction SilentlyContinue

$proc = Get-Process UnrealEditor -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { Write-Error "no game process"; exit 1 }
$script:found = [IntPtr]::Zero
$cb = [W32K+EnumProc]{
  param($h, $p)
  $pid2 = 0
  [void][W32K]::GetWindowThreadProcessId($h, [ref]$pid2)
  if ($pid2 -eq $proc.Id) {
    $sb = New-Object System.Text.StringBuilder 512
    [void][W32K]::GetWindowText($h, $sb, 512)
    if ($sb.ToString() -like "*SimCopterRemake*") { $script:found = $h; return $false }
  }
  return $true
}
[void][W32K]::EnumWindows($cb, [IntPtr]::Zero)
if ($script:found -eq [IntPtr]::Zero) { Write-Error "no game window"; exit 1 }

[void][W32K]::SetForegroundWindow($script:found)
Start-Sleep -Milliseconds 350

$p = New-Object CPOINT; $p.X = $X; $p.Y = $Y
[void][W32K]::ClientToScreen($script:found, [ref]$p)
[void][W32K]::SetCursorPos($p.X, $p.Y)
Start-Sleep -Milliseconds 250
[W32K]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 90
[W32K]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 500
Write-Output "clicked client($X,$Y) -> screen($($p.X),$($p.Y))"
