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

# FLAPBTN2: two 17x29 frames. Show which rows differ.
$s = Get-Indexed (Join-Path $dir "FLAPBTN2.BMP")
"FLAPBTN2 frame0 vs frame1, per-row differing pixel count (17 wide, 29 tall):"
for ($y = 0; $y -lt 29; $y++) {
    $d = 0
    for ($x = 0; $x -lt 17; $x++) {
        if ($s.P[$y * $s.W + $x] -ne $s.P[$y * $s.W + 17 + $x]) { $d++ }
    }
    "  y{0,2}: {1}" -f $y, $d
}

$s1 = Get-Indexed (Join-Path $dir "FLAPBTN1.BMP")
"FLAPBTN1 frame0 vs frame1, per-row differing pixel count (17 wide, 24 tall):"
for ($y = 0; $y -lt 24; $y++) {
    $d = 0
    for ($x = 0; $x -lt 17; $x++) {
        if ($s1.P[$y * $s1.W + $x] -ne $s1.P[$y * $s1.W + 17 + $x]) { $d++ }
    }
    "  y{0,2}: {1}" -f $y, $d
}

$s0 = Get-Indexed (Join-Path $dir "FLAPBTN0.BMP")
"FLAPBTN0 drop frames (x34..53 vs x54..73), per-row diff (20 wide):"
for ($y = 0; $y -lt 29; $y++) {
    $d = 0
    for ($x = 0; $x -lt 20; $x++) {
        if ($s0.P[$y * $s0.W + 34 + $x] -ne $s0.P[$y * $s0.W + 54 + $x]) { $d++ }
    }
    "  y{0,2}: {1}" -f $y, $d
}
"FLAPBTN0 rocker frames identical to FLAPBTN2? "
$same = $true
for ($y = 0; $y -lt 29; $y++) { for ($x = 0; $x -lt 34; $x++) { if ($s0.P[$y * $s0.W + $x] -ne $s.P[$y * $s.W + $x]) { $same = $false } } }
"  $same"
