Add-Type -AssemblyName System.Drawing
$dir = "S:\Repos\sim-copter-remake\Reference\SimCopterOriginalGame\BMP"

function Get-Indexed($path) {
    $bmp = New-Object System.Drawing.Bitmap($path)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $bmp.Width, $bmp.Height)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, $bmp.PixelFormat)
    $stride = $data.Stride
    $buf = New-Object byte[] ($stride * $bmp.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
    $bmp.UnlockBits($data)
    $w = $bmp.Width; $h = $bmp.Height
    $bmp.Dispose()
    $px = New-Object byte[] ($w * $h)
    for ($y = 0; $y -lt $h; $y++) { [Array]::Copy($buf, $y * $stride, $px, $y * $w, $w) }
    return @{ W = $w; H = $h; P = $px }
}

$pg = Get-Indexed (Join-Path $dir "FLAP0.BMP")
$sh = Get-Indexed (Join-Path $dir "FLAPBTN0.BMP")

# drop frame 0 is sheet x 34..53, 20 wide, 29 tall. Search only the left half of the page.
"All offsets in FLAP0 where the drop sprite matches >= 95%:"
for ($oy = 0; $oy -le $pg.H - 29; $oy++) {
    for ($ox = 0; $ox -le $pg.W - 20; $ox++) {
        $hit = 0; $tot = 0
        for ($y = 0; $y -lt 29; $y++) {
            for ($x = 0; $x -lt 20; $x++) {
                $sv = $sh.P[$y * $sh.W + 34 + $x]
                if ($sv -ne 254) {
                    $tot++
                    if ($pg.P[($oy + $y) * $pg.W + $ox + $x] -eq $sv) { $hit++ }
                }
            }
        }
        $pct = 100.0 * $hit / $tot
        if ($pct -ge 95) { "  ({0},{1})  {2}%" -f $ox, $oy, [math]::Round($pct, 1) }
    }
}
