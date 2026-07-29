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

foreach ($name in @("FLAP1", "FLAP2", "FLAP3")) {
    $pg = Get-Indexed (Join-Path $dir "$name.BMP")
    "=== $name  $($pg.W)x$($pg.H) ==="
    # signature per column
    $sig = @()
    for ($x = 0; $x -lt $pg.W; $x++) {
        $s = New-Object System.Text.StringBuilder
        for ($y = 0; $y -lt $pg.H; $y++) { [void]$s.Append(("{0:x2}" -f $pg.P[$y * $pg.W + $x])) }
        $sig += $s.ToString()
    }
    # group identical columns
    $map = @{}
    $order = @()
    for ($x = 0; $x -lt $pg.W; $x++) {
        if (-not $map.ContainsKey($sig[$x])) { $map[$sig[$x]] = @(); $order += $sig[$x] }
        $map[$sig[$x]] += $x
    }
    "distinct columns: $($map.Count) of $($pg.W)"
    foreach ($k in $order) {
        $cols = $map[$k]
        if ($cols.Count -ge 3) { "  repeated x$($cols.Count): " + ($cols -join ",") }
    }
}
