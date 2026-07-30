# zoom.ps1 -In <png> [-Out <png>] [-X <n> -Y <n> -W <n> -H <n>] [-Scale <n>]
#
# Crops a region of a VICE screenshot and scales it up with nearest-neighbour,
# so individual C64 pixels stay square blocks instead of being blurred away.
# Needed because ball art and colour clash are decided at the pixel level and a
# 384x272 screenshot is too small to judge them by eye.
#
# Default crop is the top-left quadrant of the visible board area.

param(
    [Parameter(Mandatory = $true)][string]$In,
    [string]$Out,
    [int]$X = 32, [int]$Y = 30, [int]$W = 120, [int]$H = 90,
    [int]$Scale = 5
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$In = (Resolve-Path $In).Path
if (-not $Out) { $Out = [IO.Path]::ChangeExtension($In, $null) + "zoom.png" }

$src = [System.Drawing.Image]::FromFile($In)
try {
    if ($X + $W -gt $src.Width)  { $W = $src.Width  - $X }
    if ($Y + $H -gt $src.Height) { $H = $src.Height - $Y }

    $dst = New-Object System.Drawing.Bitmap ($W * $Scale), ($H * $Scale)
    $g = [System.Drawing.Graphics]::FromImage($dst)
    try {
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
        $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
        $g.DrawImage($src, (New-Object System.Drawing.Rectangle 0, 0, ($W * $Scale), ($H * $Scale)),
                     (New-Object System.Drawing.Rectangle $X, $Y, $W, $H),
                     [System.Drawing.GraphicsUnit]::Pixel)
    } finally { $g.Dispose() }

    $dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $dst.Dispose()
    Write-Output "wrote $Out  ($($W)x$($H) -> $($W*$Scale)x$($H*$Scale))"
} finally { $src.Dispose() }
