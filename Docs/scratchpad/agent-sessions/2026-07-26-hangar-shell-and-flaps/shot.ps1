param([string]$Out = "shot.png")

Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct RECT { public int Left, Top, Right, Bottom; }
public struct POINT { public int X, Y; }
public class Win32 {
  [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll", CharSet=CharSet.Auto)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
}
"@ -ErrorAction SilentlyContinue

function Get-GameWindow {
  $proc = Get-Process UnrealEditor -ErrorAction SilentlyContinue | Select-Object -First 1
  if (-not $proc) { return [IntPtr]::Zero }
  $found = [IntPtr]::Zero
  $cb = [Win32+EnumProc]{
    param($h, $p)
    $pid2 = 0
    [void][Win32]::GetWindowThreadProcessId($h, [ref]$pid2)
    if ($pid2 -eq $proc.Id) {
      $sb = New-Object System.Text.StringBuilder 512
      [void][Win32]::GetWindowText($h, $sb, 512)
      $t = $sb.ToString()
      if ($t -like "*SimCopterRemake*") { $script:found = $h; return $false }
    }
    return $true
  }
  [void][Win32]::EnumWindows($cb, [IntPtr]::Zero)
  return $script:found
}

$hwnd = Get-GameWindow
if ($hwnd -eq [IntPtr]::Zero) { Write-Error "game window not found"; exit 1 }
[void][Win32]::SetForegroundWindow($hwnd)
Start-Sleep -Milliseconds 400

$r = New-Object RECT
[void][Win32]::GetClientRect($hwnd, [ref]$r)
$tl = New-Object POINT; $tl.X = 0; $tl.Y = 0
[void][Win32]::ClientToScreen($hwnd, [ref]$tl)
$w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top

Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($tl.X, $tl.Y, 0, 0, (New-Object System.Drawing.Size($w, $h)))
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Output "$Out ${w}x${h} at $($tl.X),$($tl.Y)"
