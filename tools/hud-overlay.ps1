# hud-overlay.ps1 -In <png> -Out <png>
#
# Draws the four proposed HUD panels onto a C64 screenshot, in the corner wedges
# the hexagon leaves empty. This is a mock-up for judging layout, not something
# the C64 build produces.
#
# The original Windows game (PLAY256.BMP) uses exactly these four corners:
# Level top-left, Skill top-right, Score bottom-right, a fourth box bottom-left.
#
# Coordinates are in MULTICOLOUR LOGICAL pixels and converted here:
#   PNG x = 32 + 2 * logical_x      (32 px left border, MC px are 2 phys wide)
#   PNG y = 36 + y                  (36 px top border)
#
# Board edges per grid row r, with the board centred (ORIGIN_X = 60):
#   r < 12   left = 56 - 4r          right = 4r + 104
#   r >= 12  left = 4r - 32          right = 192 - 4r
# so the free width in each corner is symmetric.

param(
    [Parameter(Mandatory = $true)][string]$In,
    [Parameter(Mandatory = $true)][string]$Out
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# label, logical x, y, logical width, height
# The bottom wedges are SMALLER than the top ones. The hexagon's widest row is
# 11, above centre, so the lower half is still broad at row 14 -- left edge is
# only 24 there, against 36 at row 5. Panels have to start at row 16 to clear it.
$panels = @(
    @{ T = 'LEVEL';  X = 2;   Y = 2;   W = 34; H = 56 },
    @{ T = 'SKILL';  X = 124; Y = 2;   W = 34; H = 56 },
    @{ T = '(free)'; X = 1;   Y = 161; W = 30; H = 37 },
    @{ T = 'SCORE';  X = 129; Y = 161; W = 30; H = 37 }
)

$src = [System.Drawing.Image]::FromFile((Resolve-Path $In).Path)
$scale = 3
$dst = New-Object System.Drawing.Bitmap ($src.Width * $scale), ($src.Height * $scale)
$g = [System.Drawing.Graphics]::FromImage($dst)
try {
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $g.DrawImage($src, 0, 0, $dst.Width, $dst.Height)

    $pen  = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 255, 80, 200)), 3
    $fill = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(70, 255, 80, 200))
    $txt  = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 255, 160, 220))
    $font = New-Object System.Drawing.Font 'Consolas', 13, ([System.Drawing.FontStyle]::Bold)

    foreach ($p in $panels) {
        $px = (32 + 2 * $p.X) * $scale
        $py = (36 + $p.Y) * $scale
        $pw = (2 * $p.W) * $scale
        $ph = $p.H * $scale
        $g.FillRectangle($fill, $px, $py, $pw, $ph)
        $g.DrawRectangle($pen, $px, $py, $pw, $ph)
        $g.DrawString($p.T, $font, $txt, ($px + 4), ($py + 3))
    }
} finally { $g.Dispose() }

$dst.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$dst.Dispose(); $src.Dispose()
Write-Output "wrote $Out"
