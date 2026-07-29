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

# The damage lamp's unlit frame is DAMAGE.BMP x 0..14. Find where that exact 15x14 tile is
# stamped on DASH6 so the lamp positions come from the art, not from eyeballing.
$dmg = Get-Indexed (Join-Path $dir "DAMAGE.BMP")
$pg = Get-Indexed (Join-Path $dir "DASH6.BMP")
"DAMAGE $($dmg.W)x$($dmg.H)   DASH6 $($pg.W)x$($pg.H)"

foreach ($frame in 0, 1, 2) {
    $fx = $frame * 15
    $hits = @()
    for ($oy = 0; $oy -le $pg.H - 14; $oy++) {
        for ($ox = 0; $ox -le $pg.W - 15; $ox++) {
            $hit = 0; $tot = 0
            for ($y = 0; $y -lt 14; $y++) {
                for ($x = 0; $x -lt 15; $x++) {
                    $sv = $dmg.P[$y * $dmg.W + $fx + $x]
                    if ($sv -ne 254) { $tot++; if ($pg.P[($oy + $y) * $pg.W + $ox + $x] -eq $sv) { $hit++ } }
                }
            }
            if ($tot -gt 0 -and (100.0 * $hit / $tot) -ge 92) { $hits += "($ox,$oy)=$([math]::Round(100.0*$hit/$tot,0))%" }
        }
    }
    "  damage frame ${frame}: " + ($hits -join "  ")
}

# MANAGGE is the points meter block.
$man = Get-Indexed (Join-Path $dir "MANAGGE.BMP")
"MANAGGE $($man.W)x$($man.H)"
$hits = @()
for ($oy = 0; $oy -le $pg.H - $man.H; $oy++) {
    for ($ox = 0; $ox -le $pg.W - $man.W; $ox++) {
        $hit = 0; $tot = 0
        for ($y = 0; $y -lt $man.H; $y++) {
            for ($x = 0; $x -lt $man.W; $x++) {
                $sv = $man.P[$y * $man.W + $x]
                if ($sv -ne 254) { $tot++; if ($pg.P[($oy + $y) * $pg.W + $ox + $x] -eq $sv) { $hit++ } }
            }
        }
        if ($tot -gt 0 -and (100.0 * $hit / $tot) -ge 90) { $hits += "($ox,$oy)" }
    }
}
"  MANAGGE on DASH6: " + ($hits -join " ")
