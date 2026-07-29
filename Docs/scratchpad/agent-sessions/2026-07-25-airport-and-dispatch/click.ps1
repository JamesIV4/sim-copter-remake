param([int]$X, [int]$Y)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct PT { public int X, Y; }
public class W32Click {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref PT p);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr e);
}
"@

$proc = Get-Process UnrealEditor -ErrorAction Stop | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$h = $proc.MainWindowHandle
[W32Click]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 500

$p = New-Object PT
$p.X = $X; $p.Y = $Y
[W32Click]::ClientToScreen($h, [ref]$p) | Out-Null
[W32Click]::SetCursorPos($p.X, $p.Y) | Out-Null
Start-Sleep -Milliseconds 250
[W32Click]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)  # LEFTDOWN
Start-Sleep -Milliseconds 80
[W32Click]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)  # LEFTUP
Write-Host "clicked client ($X,$Y) -> screen ($($p.X),$($p.Y))"
