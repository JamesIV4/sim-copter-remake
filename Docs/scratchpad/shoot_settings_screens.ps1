# Scratch: screenshot each in-game Settings page by clicking through playmenu.bmp.
#
# Launches straight into the city level rather than through the front end, so the run does not
# depend on the menu, then raises the Settings screen with Escape.
#
# playmenu.bmp sits at (50,10) in the original's 640x480 space and its rows are at y 64 + 40*i
# with an eight-row list (a user game), so a 1280x960 window is a clean 2x and the click points
# are exact.

$ErrorActionPreference = "Stop"
$outDir = "S:\Repos\sim-copter-remake\Docs\scratchpad\settings-art"

Add-Type @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public class Win32S {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT lpPoint);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int X, int Y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
    delegate bool EnumProc(IntPtr h, IntPtr p);

    // SetForegroundWindow is refused when the calling process does not own the foreground, which
    // silently leaves whatever was on top in every screenshot. Pinning the window topmost is not.
    public static void ForceOnTop(IntPtr h) {
        SetWindowPos(h, new IntPtr(-1), 0, 0, 0, 0, 0x0001 | 0x0002);  // HWND_TOPMOST, NOSIZE|NOMOVE
        SetForegroundWindow(h);
    }
    public static string Describe(IntPtr h) {
        StringBuilder cls = new StringBuilder(256); GetClassName(h, cls, cls.Capacity);
        StringBuilder txt = new StringBuilder(256); GetWindowText(h, txt, txt.Capacity);
        RECT r; GetClientRect(h, out r);
        return string.Format("{0} class='{1}' title='{2}' {3}x{4}", h, cls, txt, r.R - r.L, r.B - r.T);
    }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }

    // -log opens a console window that is often the process's "main" window, and the game
    // viewport is a separate top-level. Pick the visible window with the largest client area
    // whose class is not the console's.
    public static IntPtr FindViewport(int pid) {
        IntPtr best = IntPtr.Zero; long bestArea = 0;
        EnumWindows(delegate(IntPtr h, IntPtr p) {
            uint wpid; GetWindowThreadProcessId(h, out wpid);
            if (wpid != (uint)pid || !IsWindowVisible(h)) return true;
            StringBuilder cls = new StringBuilder(256);
            GetClassName(h, cls, cls.Capacity);
            if (cls.ToString() == "ConsoleWindowClass" || cls.ToString() == "CASCADIA_HOSTING_WINDOW_CLASS") return true;
            RECT r; GetClientRect(h, out r);
            long area = (long)(r.R - r.L) * (r.B - r.T);
            if (area > bestArea) { bestArea = area; best = h; }
            return true;
        }, IntPtr.Zero);
        return best;
    }
}
"@
Add-Type -AssemblyName System.Drawing

$log = "S:\Repos\sim-copter-remake\SimCopterRemake\Saved\Logs\SimCopterRemake.log"
if (Test-Path $log) { Remove-Item $log -Force }

# No -log: the console window it opens steals both the foreground and the process's main window
# handle. The log file is written either way.
$proc = Start-Process "C:\GameDev\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" -PassThru -ArgumentList `
    '"S:\Repos\sim-copter-remake\SimCopterRemake\SimCopterRemake.uproject"', '/Game/CityRender', `
    '-game', '-windowed', '-ResX=1280', '-ResY=960'

# The city build is the slow part - the mesh work happens after the world comes up for play.
$deadline = (Get-Date).AddSeconds(300)
while ((Get-Date) -lt $deadline) {
    if ((Test-Path $log) -and ((Get-Content $log -Raw -ErrorAction SilentlyContinue) -match "Bringing World .* up for play")) { break }
    Start-Sleep -Milliseconds 500
}
Start-Sleep -Seconds 25

$proc.Refresh()
$hwnd = [Win32S]::FindViewport($proc.Id)
if ($hwnd -eq [IntPtr]::Zero) { $hwnd = $proc.MainWindowHandle }
Write-Output ("viewport: " + [Win32S]::Describe($hwnd))
[void][Win32S]::ShowWindow($hwnd, 9)   # SW_RESTORE
[Win32S]::ForceOnTop($hwnd)
Start-Sleep -Milliseconds 1500

$rect = New-Object Win32S+RECT
[void][Win32S]::GetClientRect($hwnd, [ref]$rect)
$origin = New-Object Win32S+POINT
[void][Win32S]::ClientToScreen($hwnd, [ref]$origin)
$w = $rect.R - $rect.L; $h = $rect.B - $rect.T
$scale = $h / 480.0
$xOffset = [int](($w - 640 * $scale) / 2)

function Shoot($name) {
    Start-Sleep -Milliseconds 1100
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($origin.X, $origin.Y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
    $bmp.Save("$outDir\$name.png", [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "wrote $name.png"
}

function Key($vk) {
    [Win32S]::keybd_event($vk, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [Win32S]::keybd_event($vk, 0, 2, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 900
}

# Page coordinates in the original's 640x480 space -> screen.
function ClickPage($px, $py) {
    $sx = $origin.X + [int]([Math]::Round($px * $scale)) + $xOffset
    $sy = $origin.Y + [int]([Math]::Round($py * $scale))
    [void][Win32S]::SetCursorPos($sx, $sy)
    Start-Sleep -Milliseconds 300
    [Win32S]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [Win32S]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800
}

# Row i's text band: page (50 + 102, 10 + 64 + 40i) tall by the 26 px font. Aim at its middle,
# inside the hit band's 70..380.
function ClickItem($i) { ClickPage 200 (10 + 64 + 40 * $i + 13) }

# Every sub-dialog is centred on the 640x480 screen, so its buttons are at
#   ((640 - w) / 2 + x + 50, (480 - h) / 2 + y + 14)
# for a 100x28 button.bmp frame. 594x435 pages sit at (23,23), sound.bmp at (45,23), MBox at
# (88,64).
function ClickCancel594($x, $y) { ClickPage (23 + $x + 50) (23 + $y + 14) }

Shoot "shot-city-before"

Key 0x1B                    # Escape -> the Settings screen
Shoot "shot-settings-menu"

ClickItem 0                 # City Settings
Shoot "shot-citysettings"
ClickCancel594 364 376
Shoot "shot-back-from-city"

ClickItem 1                 # Graphics
Shoot "shot-graphics"
ClickCancel594 432 318
Shoot "shot-back-from-graphics"

ClickItem 2                 # Sound
Shoot "shot-sound"
ClickPage (45 + 334 + 50) (23 + 359 + 14)

ClickItem 3                 # Controls
Shoot "shot-controls"
ClickCancel594 444 380

ClickItem 4                 # Save Game -> the refusal message box
Shoot "shot-save-message"
ClickPage (88 + 244 + 50) (64 + 256 + 14)

ClickItem 6                 # Leave City -> the Yes/No confirm
Shoot "shot-leave-confirm"

Stop-Process -Id $proc.Id -Force
