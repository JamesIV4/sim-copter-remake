param([string]$Command)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class K {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("user32.dll")] public static extern short VkKeyScan(char c);
    public const uint KEYUP = 2;
    public static void Tap(byte vk) { keybd_event(vk, 0, 0, IntPtr.Zero); System.Threading.Thread.Sleep(25); keybd_event(vk, 0, KEYUP, IntPtr.Zero); System.Threading.Thread.Sleep(25); }
    public static void Type(string s) {
        foreach (char c in s) {
            short v = VkKeyScan(c);
            byte vk = (byte)(v & 0xff);
            bool shift = ((v >> 8) & 1) != 0;
            if (shift) keybd_event(0x10, 0, 0, IntPtr.Zero);
            Tap(vk);
            if (shift) keybd_event(0x10, 0, KEYUP, IntPtr.Zero);
        }
    }
}
"@

$proc = Get-Process UnrealEditor -ErrorAction SilentlyContinue | Where-Object { $_.MainWindowTitle -ne "" } | Select-Object -First 1
if ($null -eq $proc) { "no game window"; exit 1 }
[void][K]::SetForegroundWindow($proc.MainWindowHandle)
Start-Sleep -Milliseconds 500

# VK_OEM_3 is the tilde / backquote console key
[K]::Tap(0xC0)
Start-Sleep -Milliseconds 400
[K]::Type($Command)
Start-Sleep -Milliseconds 200
[K]::Tap(0x0D)  # Enter
Start-Sleep -Milliseconds 500
"ran: $Command"
