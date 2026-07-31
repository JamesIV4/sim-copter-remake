# Scratch: screenshot each front-end screen by clicking through the main menu.
#
# Main-menu item rows are at page y 42 + 64*i with the page itself at (2,29) in the original's
# 640x480 space, so a 1280x960 window is a clean 2x and the click points are exact. Re-locating
# between clicks is unnecessary here because nothing on this page moves.

$ErrorActionPreference = "Stop"
$outDir = "S:\Repos\sim-copter-remake\Docs\scratchpad\mainmenu-art"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
Add-Type -AssemblyName System.Drawing

$log = "S:\Repos\sim-copter-remake\SimCopterRemake\Saved\Logs\SimCopterRemake.log"
if (Test-Path $log) { Remove-Item $log -Force }

$proc = Start-Process "C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -PassThru -ArgumentList `
    '"S:\Repos\sim-copter-remake\SimCopterRemake\SimCopterRemake.uproject"', '-game', '-windowed', `
    '-ResX=1280', '-ResY=960', '-log'

$deadline = (Get-Date).AddSeconds(120)
while ((Get-Date) -lt $deadline) {
    if ((Test-Path $log) -and ((Get-Content $log -Raw -ErrorAction SilentlyContinue) -match "Bringing World .* up for play")) { break }
    Start-Sleep -Milliseconds 500
}
Start-Sleep -Seconds 6

$proc.Refresh()
$hwnd = $proc.MainWindowHandle
[void][Win32]::ShowWindow($hwnd, 5)
[void][Win32]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 800

$rect = New-Object Win32+RECT
[void][Win32]::GetClientRect($hwnd, [ref]$rect)
$origin = New-Object Win32+POINT
[void][Win32]::ClientToScreen($hwnd, [ref]$origin)
$w = $rect.R - $rect.L; $h = $rect.B - $rect.T
$scale = $h / 480.0

function Shoot($name) {
    Start-Sleep -Milliseconds 900
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($origin.X, $origin.Y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $bmp.Save("$outDir\$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "wrote $name.png"
}

# Page coordinates in the original's 640x480 space -> screen.
function ClickPage($px, $py) {
    $sx = $origin.X + [int]([Math]::Round($px * $scale)) + [int](($w - 640 * $scale) / 2)
    $sy = $origin.Y + [int]([Math]::Round($py * $scale))
    [void][Win32]::SetCursorPos($sx, $sy)
    Start-Sleep -Milliseconds 300
    [Win32]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [Win32]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
}

# Item i's hit band: page (2 + 29 .. 2 + 394) x (29 + 42 + 64i .. + 26). Aim at its middle.
function ClickItem($i) { ClickPage 210 (29 + 42 + 64 * $i + 13) }

Shoot "shot-mainmenu"

ClickItem 0            # New Career Game -> career select
Shoot "shot-careerselect"
ClickPage 431 352      # its Cancel button (page 431,338 + half a 100x28 frame)
Shoot "shot-back-from-career"

ClickItem 2            # New User Game -> the city picker
Shoot "shot-usercitypicker"
ClickPage 358 402      # Cancel: picker page origin (65,22) + (243,366) + half a frame
Shoot "shot-back-from-picker"

ClickItem 1            # Open Career Game -> the message box
Shoot "shot-messagebox"

Stop-Process -Id $proc.Id -Force
