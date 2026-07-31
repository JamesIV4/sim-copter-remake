# Scratch: launch the remake windowed and screenshot the front end.
#
# Only the front end needs this: the game boots straight to /Game/MainMenu, so there is nothing to
# drive - wait for the world to come up, bring the window forward and grab the client area. See
# Docs/memory/simcopter-ingame-verification.md (and AGENTS.md section 6 before reaching for it).

param(
    [string]$Out = "S:\Repos\sim-copter-remake\Docs\scratchpad\mainmenu-art\shot.png",
    [int]$SettleSeconds = 6
)

$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@

$log = "S:\Repos\sim-copter-remake\SimCopterRemake\Saved\Logs\SimCopterRemake.log"
if (Test-Path $log) { Remove-Item $log -Force }

$proc = Start-Process "C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -PassThru -ArgumentList `
    '"S:\Repos\sim-copter-remake\SimCopterRemake\SimCopterRemake.uproject"', '-game', '-windowed', `
    '-ResX=1280', '-ResY=960', '-log'

# Poll the log rather than sleeping blind; clicking or grabbing before the widget exists just
# produces an empty frame that looks like a broken layout.
$deadline = (Get-Date).AddSeconds(120)
$ready = $false
while ((Get-Date) -lt $deadline) {
    if (Test-Path $log) {
        $text = Get-Content $log -Raw -ErrorAction SilentlyContinue
        if ($text -match "Bringing World .* up for play") { $ready = $true; break }
    }
    Start-Sleep -Milliseconds 500
}
Write-Output "world up: $ready"
Start-Sleep -Seconds $SettleSeconds

$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) { throw "no window" }
[void][Win32]::ShowWindow($hwnd, 5)
[void][Win32]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 800

$rect = New-Object Win32+RECT
[void][Win32]::GetClientRect($hwnd, [ref]$rect)
$origin = New-Object Win32+POINT
[void][Win32]::ClientToScreen($hwnd, [ref]$origin)

Add-Type -AssemblyName System.Drawing
$w = $rect.R - $rect.L
$h = $rect.B - $rect.T
$bmp = New-Object System.Drawing.Bitmap($w, $h)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($origin.X, $origin.Y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "wrote $Out ($w x $h)"

Stop-Process -Id $proc.Id -Force
