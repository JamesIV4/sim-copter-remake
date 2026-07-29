Add-Type @"
using System;
using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint x, uint y, uint d, IntPtr e);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
}
"@ -ErrorAction SilentlyContinue

Add-Type -AssemblyName System.Drawing

function Get-GameWindow {
  $p = Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
  if ($null -eq $p) { throw "no game window" }
  return $p.MainWindowHandle
}

function Shot([string]$path) {
  $h = Get-GameWindow
  [W]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 400
  $r = New-Object W+RECT
  [W]::GetClientRect($h, [ref]$r) | Out-Null
  $tl = New-Object W+POINT; $tl.X = 0; $tl.Y = 0
  [W]::ClientToScreen($h, [ref]$tl) | Out-Null
  $w = $r.R - $r.L; $ht = $r.B - $r.T
  $bmp = New-Object System.Drawing.Bitmap($w, $ht)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($tl.X, $tl.Y, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
  $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  Write-Output "$path ($w x $ht)"
}

function Click([int]$cx, [int]$cy) {
  $h = Get-GameWindow
  [W]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 300
  $p = New-Object W+POINT; $p.X = $cx; $p.Y = $cy
  [W]::ClientToScreen($h, [ref]$p) | Out-Null
  [W]::SetCursorPos($p.X, $p.Y) | Out-Null
  Start-Sleep -Milliseconds 200
  [W]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [W]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
  Write-Output "clicked client ($cx,$cy) -> screen ($($p.X),$($p.Y))"
}
