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

$s = Get-Indexed (Join-Path $dir "SEATWIN2.BMP")
"SEATWIN2 $($s.W)x$($s.H)"
# The inner well is one flat index. Report the run of the most common index on a middle row.
foreach ($y in @(8, 20, 57, 100, 110)) {
    $runs = @(); $cur = -1; $start = 0
    for ($x = 0; $x -lt $s.W; $x++) {
        $v = $s.P[$y * $s.W + $x]
        if ($v -ne $cur) { if ($cur -ge 0 -and ($x - $start) -ge 4) { $runs += ("{0:x2}@{1}..{2}" -f $cur, $start, ($x - 1)) }; $cur = $v; $start = $x }
    }
    if ($cur -ge 0 -and ($s.W - $start) -ge 4) { $runs += ("{0:x2}@{1}..{2}" -f $cur, $start, ($s.W - 1)) }
    "  y=$y : " + ($runs -join "  ")
}
foreach ($x in @(80)) {
    $runs = @(); $cur = -1; $start = 0
    for ($y = 0; $y -lt $s.H; $y++) {
        $v = $s.P[$y * $s.W + $x]
        if ($v -ne $cur) { if ($cur -ge 0 -and ($y - $start) -ge 4) { $runs += ("{0:x2}@{1}..{2}" -f $cur, $start, ($y - 1)) }; $cur = $v; $start = $y }
    }
    if ($cur -ge 0 -and ($s.H - $start) -ge 4) { $runs += ("{0:x2}@{1}..{2}" -f $cur, $start, ($s.H - 1)) }
    "  x=$x : " + ($runs -join "  ")
}

# DASH6: the joystick well is the big dark rectangle around x 200..280.
$d = Get-Indexed (Join-Path $dir "DASH6.BMP")
"DASH6 joystick well:"
foreach ($y in @(35)) {
    $runs = @(); $cur = -1; $start = 0
    for ($x = 190; $x -lt 300; $x++) {
        $v = $d.P[$y * $d.W + $x]
        if ($v -ne $cur) { if ($cur -ge 0 -and ($x - $start) -ge 4) { $runs += ("{0:x2}@{1}..{2}" -f $cur, $start, ($x - 1)) }; $cur = $v; $start = $x }
    }
    "  y=$y : " + ($runs -join "  ")
}
foreach ($x in @(240)) {
    $runs = @(); $cur = -1; $start = 0
    for ($y = 0; $y -lt $d.H; $y++) {
        $v = $d.P[$y * $d.W + $x]
        if ($v -ne $cur) { if ($cur -ge 0 -and ($y - $start) -ge 4) { $runs += ("{0:x2}@{1}..{2}" -f $cur, $start, ($y - 1)) }; $cur = $v; $start = $y }
    }
    "  x=$x : " + ($runs -join "  ")
}

# DASH4: the compass window on the right.
$d4 = Get-Indexed (Join-Path $dir "DASH4.BMP")
"DASH4 $($d4.W)x$($d4.H), non-keyed columns:"
$first = -1; $last = -1
for ($x = 0; $x -lt $d4.W; $x++) {
    $any = $false
    for ($y = 0; $y -lt $d4.H; $y++) { if ($d4.P[$y * $d4.W + $x] -ne 254) { $any = $true; break } }
    if ($any) { if ($first -lt 0) { $first = $x }; $last = $x }
}
"  opaque x $first..$last"
foreach ($y in @(10, 20)) {
    $runs = @(); $cur = -1; $start = 0
    for ($x = 380; $x -lt $d4.W; $x++) {
        $v = $d4.P[$y * $d4.W + $x]
        if ($v -ne $cur) { if ($cur -ge 0 -and ($x - $start) -ge 3) { $runs += ("{0:x2}@{1}..{2}" -f $cur, $start, ($x - 1)) }; $cur = $v; $start = $x }
    }
    "  y=$y : " + ($runs -join "  ")
}
