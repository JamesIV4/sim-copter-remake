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
    # flatten to w*h index array
    $px = New-Object byte[] ($w * $h)
    for ($y = 0; $y -lt $h; $y++) {
        [Array]::Copy($buf, $y * $stride, $px, $y * $w, $w)
    }
    return @{ W = $w; H = $h; P = $px }
}

$pages = @{}
foreach ($n in 0..3) { $pages["FLAP$n"] = Get-Indexed (Join-Path $dir "FLAP$n.BMP") }
$sheets = @{}
foreach ($n in 0..2) { $sheets["FLAPBTN$n"] = Get-Indexed (Join-Path $dir "FLAPBTN$n.BMP") }

# frame definitions: sheet, x offset, width
$frames = @(
    @{ N = "BTN0.rocker";  S = "FLAPBTN0"; X = 0;  W = 17; H = 29 },
    @{ N = "BTN0.rockerP"; S = "FLAPBTN0"; X = 17; W = 17; H = 29 },
    @{ N = "BTN0.drop";    S = "FLAPBTN0"; X = 34; W = 20; H = 29 },
    @{ N = "BTN0.dropP";   S = "FLAPBTN0"; X = 54; W = 20; H = 29 },
    @{ N = "BTN1.oct";     S = "FLAPBTN1"; X = 0;  W = 17; H = 24 },
    @{ N = "BTN1.octP";    S = "FLAPBTN1"; X = 17; W = 17; H = 24 },
    @{ N = "BTN1.sliver";  S = "FLAPBTN1"; X = 34; W = 4;  H = 24 },
    @{ N = "BTN2.rocker";  S = "FLAPBTN2"; X = 0;  W = 17; H = 29 },
    @{ N = "BTN2.rockerP"; S = "FLAPBTN2"; X = 17; W = 17; H = 29 }
)

foreach ($fr in $frames) {
    $sh = $sheets[$fr.S]
    # content bounding box within the frame (non-254)
    $minx = 999; $maxx = -1; $miny = 999; $maxy = -1
    for ($y = 0; $y -lt $fr.H; $y++) {
        for ($x = 0; $x -lt $fr.W; $x++) {
            if ($sh.P[$y * $sh.W + $fr.X + $x] -ne 254) {
                if ($x -lt $minx) { $minx = $x }; if ($x -gt $maxx) { $maxx = $x }
                if ($y -lt $miny) { $miny = $y }; if ($y -gt $maxy) { $maxy = $y }
            }
        }
    }
    "$($fr.N): frame $($fr.W)x$($fr.H)  content bbox x $minx..$maxx  y $miny..$maxy"

    foreach ($pn in @("FLAP0", "FLAP1", "FLAP2", "FLAP3")) {
        $pg = $pages[$pn]
        $best = -1; $bx = -1; $by = -1; $bestTot = 0
        for ($oy = 0; $oy -le $pg.H - $fr.H; $oy++) {
            for ($ox = 0; $ox -le $pg.W - $fr.W; $ox++) {
                $hit = 0; $tot = 0
                for ($y = 0; $y -lt $fr.H; $y++) {
                    $srow = $y * $sh.W + $fr.X
                    $prow = ($oy + $y) * $pg.W + $ox
                    for ($x = 0; $x -lt $fr.W; $x++) {
                        $sv = $sh.P[$srow + $x]
                        if ($sv -ne 254) {
                            $tot++
                            if ($pg.P[$prow + $x] -eq $sv) { $hit++ }
                        }
                    }
                }
                if ($hit -gt $best) { $best = $hit; $bx = $ox; $by = $oy; $bestTot = $tot }
            }
        }
        $pct = [math]::Round(100.0 * $best / $bestTot, 1)
        if ($pct -ge 60) { "    $pn : offset ($bx,$by)  match $pct%  ($best/$bestTot)" }
    }
}
