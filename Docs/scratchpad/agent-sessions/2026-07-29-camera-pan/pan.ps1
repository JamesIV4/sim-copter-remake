# Drives the running -game window to verify middle-drag camera pan.
# Reuses the console/screenshot helpers from the 2026-07-28 session.
. "S:\Repos\sim-copter-remake\Docs\scratchpad\agent-sessions\2026-07-28-ingame-driving\drive.ps1"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Mouse {
  [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, uint data, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  public const uint MOVE = 0x0001;
  public const uint MIDDLEDOWN = 0x0020;
  public const uint MIDDLEUP = 0x0040;
}
"@ -ErrorAction SilentlyContinue

# Relative middle-button drag over the viewport centre. dy is in mouse units:
# negative moves the mouse up the screen.
function Invoke-MiddleDrag([int]$dy, [int]$steps = 20) {
  $h = Get-GameWindow
  [Win32]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 400
  $r = New-Object Win32+RECT
  [Win32]::GetClientRect($h, [ref]$r) | Out-Null
  $c = New-Object Win32+POINT
  $c.X = [int](($r.Right - $r.Left) / 2)
  $c.Y = [int](($r.Bottom - $r.Top) / 2)
  [Win32]::ClientToScreen($h, [ref]$c) | Out-Null
  [Win32Mouse]::SetCursorPos($c.X, $c.Y) | Out-Null
  Start-Sleep -Milliseconds 200
  [Win32Mouse]::mouse_event([Win32Mouse]::MIDDLEDOWN, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 150
  $step = [int]($dy / $steps)
  for ($i = 0; $i -lt $steps; $i++) {
    [Win32Mouse]::mouse_event([Win32Mouse]::MOVE, 0, $step, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 30
  }
  Start-Sleep -Milliseconds 150
  [Win32Mouse]::mouse_event([Win32Mouse]::MIDDLEUP, 0, 0, 0, [UIntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}
