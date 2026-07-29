param(
  [int]$ProcessId,
  [int]$ClientX,
  [int]$ClientY,
  [int]$Repeat = 1
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Click {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint cButtons, uint dwExtraInfo);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  public const uint LEFTDOWN = 0x0002, LEFTUP = 0x0004;
}
"@

$hwnd = (Get-Process -Id $ProcessId).MainWindowHandle
[Win32Click]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 600

$pt = New-Object Win32Click+POINT
$pt.X = $ClientX; $pt.Y = $ClientY
[Win32Click]::ClientToScreen($hwnd, [ref]$pt) | Out-Null

for ($i = 0; $i -lt $Repeat; $i++) {
  [Win32Click]::SetCursorPos($pt.X, $pt.Y) | Out-Null
  Start-Sleep -Milliseconds 250
  [Win32Click]::mouse_event([Win32Click]::LEFTDOWN, 0, 0, 0, 0)
  Start-Sleep -Milliseconds 90
  [Win32Click]::mouse_event([Win32Click]::LEFTUP, 0, 0, 0, 0)
  Start-Sleep -Milliseconds 350
}
"clicked client ($ClientX,$ClientY) -> screen ($($pt.X),$($pt.Y)) x$Repeat"
