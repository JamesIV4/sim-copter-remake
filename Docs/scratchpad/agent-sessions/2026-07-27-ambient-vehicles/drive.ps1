Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Drawing;
using System.Drawing.Imaging;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr FindWindow(string c, string n);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern short VkKeyScan(char ch);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    public static void Key(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(18); keybd_event(vk, 0, 2, UIntPtr.Zero); System.Threading.Thread.Sleep(18); }
    public static void Type(string s) {
        foreach (char c in s) {
            short v = VkKeyScan(c);
            byte vk = (byte)(v & 0xff);
            bool shift = (v & 0x100) != 0;
            if (shift) keybd_event(0x10, 0, 0, UIntPtr.Zero);
            Key(vk);
            if (shift) keybd_event(0x10, 0, 2, UIntPtr.Zero);
        }
    }
    public static void Shot(IntPtr h, string path) {
        RECT r; GetClientRect(h, out r);
        POINT p = new POINT(); p.X = 0; p.Y = 0; ClientToScreen(h, ref p);
        int w = r.Right - r.Left, ht = r.Bottom - r.Top;
        if (w <= 0 || ht <= 0) return;
        Bitmap bmp = new Bitmap(w, ht);
        using (Graphics g = Graphics.FromImage(bmp)) { g.CopyFromScreen(p.X, p.Y, 0, 0, new Size(w, ht)); }
        bmp.Save(path, ImageFormat.Png);
    }
}
"@ -ReferencedAssemblies System.Drawing, System.Windows.Forms

function Get-GameWindow {
    $p = Get-Process -Name UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
    if ($p) { return $p.MainWindowHandle }
    return [IntPtr]::Zero
}

function Send-Console([string]$Cmd) {
    $h = Get-GameWindow
    if ($h -eq [IntPtr]::Zero) { Write-Output "no window"; return }
    [Win32]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 250
    [Win32]::Key(0xC0)          # ` opens the console
    Start-Sleep -Milliseconds 500
    [Win32]::Type($Cmd)
    Start-Sleep -Milliseconds 250
    [Win32]::Key(0x0D)          # Enter
    Start-Sleep -Milliseconds 400
    Write-Output "sent: $Cmd"
}

function Save-Shot([string]$Path) {
    $h = Get-GameWindow
    if ($h -eq [IntPtr]::Zero) { Write-Output "no window"; return }
    [Win32]::SetForegroundWindow($h) | Out-Null
    Start-Sleep -Milliseconds 400
    [Win32]::Shot($h, $Path)
    Write-Output "shot: $Path"
}
