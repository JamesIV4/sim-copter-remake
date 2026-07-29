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

$dmg = Get-Indexed (Join-Path $dir "DAMAGE.BMP")
$pg = Get-Indexed (Join-Path $dir "DASH6.BMP")

"Match score of DAMAGE frame 0 along y=63, x=0..140:"
$line = ""
for ($ox = 0; $ox -le 140; $ox++) {
    $hit = 0; $tot = 0
    for ($y = 0; $y -lt 14; $y++) {
        for ($x = 0; $x -lt 15; $x++) {
            $sv = $dmg.P[$y * $dmg.W + $x]
            if ($sv -ne 254) { $tot++; if ($pg.P[(63 + $y) * $pg.W + $ox + $x] -eq $sv) { $hit++ } }
        }
    }
    $pct = [int](100.0 * $hit / $tot)
    if ($pct -ge 70) { $line += "x=$ox`:$pct%  " }
}
$line

"`nRow y=68 palette indices, x=0..140:"
$s = ""
for ($x = 0; $x -le 140; $x++) { $s += ("{0:x2} " -f $pg.P[68 * $pg.W + $x]) }
$s

"`nRow y=20 (upper black readout), x=0..140:"
$s = ""
for ($x = 0; $x -le 140; $x++) { $s += ("{0:x2} " -f $pg.P[20 * $pg.W + $x]) }
$s

"`nRow y=45 (lower black readout), x=0..140:"
$s = ""
for ($x = 0; $x -le 140; $x++) { $s += ("{0:x2} " -f $pg.P[45 * $pg.W + $x]) }
$s

"`nColumn x=60, y=0..82:"
$s = ""
for ($y = 0; $y -lt 82; $y++) { $s += ("{0:x2} " -f $pg.P[$y * $pg.W + 60]) }
$s
