param(
    [string]$In,
    [string]$Out,
    [int]$X, [int]$Y, [int]$W, [int]$H, [int]$Zoom = 3
)
Add-Type -AssemblyName System.Drawing
$src = [System.Drawing.Bitmap]::FromFile($In)
$rect = New-Object System.Drawing.Rectangle $X, $Y, $W, $H
$crop = $src.Clone($rect, $src.PixelFormat)
$dst = New-Object System.Drawing.Bitmap ($W * $Zoom), ($H * $Zoom)
$g = [System.Drawing.Graphics]::FromImage($dst)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g.DrawImage($crop, 0, 0, ($W * $Zoom), ($H * $Zoom))
$dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $dst.Dispose(); $crop.Dispose(); $src.Dispose()
Write-Output "wrote $Out"
