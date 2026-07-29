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

foreach ($name in @("FLAP0", "FLAP1", "FLAP2", "FLAP3")) {
    $pg = Get-Indexed (Join-Path $dir "$name.BMP")
    "=== $name ==="
    foreach ($period in @(6)) {
        # columns x where col(x) == col(x+period) over all rows
        $ok = @()
        for ($x = 0; $x -lt $pg.W - $period; $x++) {
            $same = $true
            for ($y = 0; $y -lt $pg.H; $y++) {
                if ($pg.P[$y * $pg.W + $x] -ne $pg.P[$y * $pg.W + $x + $period]) { $same = $false; break }
            }
            if ($same) { $ok += $x }
        }
        "  period ${period}: full-height matching columns: " + ($ok -join ",")

        # restricted to the box interior rows only
        foreach ($rows in @(@(6, 39), @(5, 40), @(4, 41))) {
            $ok2 = @()
            for ($x = 0; $x -lt $pg.W - $period; $x++) {
                $same = $true
                for ($y = $rows[0]; $y -le $rows[1]; $y++) {
                    if ($pg.P[$y * $pg.W + $x] -ne $pg.P[$y * $pg.W + $x + $period]) { $same = $false; break }
                }
                if ($same) { $ok2 += $x }
            }
            "  period ${period} rows $($rows[0])..$($rows[1]): " + ($ok2 -join ",")
        }
    }
}
