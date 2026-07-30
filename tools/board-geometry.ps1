# board-geometry.ps1
#
# Mirrors isInBoard() and gridToWorld() from oddballz-app.js for the classic
# 9-wide preset, and reports the on-screen extents the C64 renderer has to fit.
#
# JS reference (oddballz-app.js:20, :56, :61):
#   9: { MIN_X: 4, MAX_X: 20, MAX_Y: 19, SPLIT: 12, UPPER: 10, LOWER: 8 }
#   isInBoard(x,y) = x in [MIN_X,MAX_X] && y in [0,MAX_Y] &&
#                    ((y <  SPLIT && x < y + UPPER) ||
#                     (y >= SPLIT && x > y - LOWER))
#   screenX proportional to (x - CENTER_X) - (y - CENTER_Y) * 0.5
#
# This is the authoritative geometry for the port. Cross-check against the JS
# oracle before trusting any C64 output.

$MIN_X = 4; $MAX_X = 20; $MAX_Y = 19; $SPLIT = 12; $UPPER = 10; $LOWER = 8

function Test-InBoard([int]$x, [int]$y) {
    if ($x -lt $MIN_X -or $x -gt $MAX_X -or $y -lt 0 -or $y -gt $MAX_Y) { return $false }
    return (($y -lt $SPLIT -and $x -lt ($y + $UPPER)) -or
            ($y -ge $SPLIT -and $x -gt ($y - $LOWER)))
}

$total = 0
$widest = 0; $widestRow = -1
$sxMin = [double]::MaxValue; $sxMax = [double]::MinValue
$rows = @()

for ($y = 0; $y -le $MAX_Y; $y++) {
    $cells = @(); for ($x = $MIN_X; $x -le $MAX_X; $x++) { if (Test-InBoard $x $y) { $cells += $x } }
    $total += $cells.Count
    if ($cells.Count -gt $widest) { $widest = $cells.Count; $widestRow = $y }
    # screenX in cell-widths, half-cell shear per row
    $lo = $cells[0] - 0.5 * $y
    $hi = $cells[-1] - 0.5 * $y
    if ($lo -lt $sxMin) { $sxMin = $lo }
    if ($hi -gt $sxMax) { $sxMax = $hi }
    $rows += [pscustomobject]@{ Y = $y; Count = $cells.Count; XLo = $cells[0]; XHi = $cells[-1]; SxLo = $lo; SxHi = $hi }
}

Write-Output "row  cells  grid-x      screen-x (cell widths)   offset"
foreach ($r in $rows) {
    $off = if (($r.Y % 2) -eq 0) { 'aligned' } else { 'half-cell' }
    Write-Output ("{0,3}  {1,5}  {2,2}..{3,-2}     {4,6:N1}..{5,-6:N1}          {6}" -f `
        $r.Y, $r.Count, $r.XLo, $r.XHi, $r.SxLo, $r.SxHi, $off)
}

Write-Output ""
Write-Output "live cells      : $total"
Write-Output "widest row      : $widest balls (row $widestRow)"
Write-Output ("screen width    : {0} cell widths + 1 ball = {1} cells" -f ($sxMax - $sxMin), ($sxMax - $sxMin + 1))
Write-Output ("screen height   : {0} rows" -f ($MAX_Y + 1))
Write-Output ""
Write-Output "ASCII map (o = live cell, half-cell shear shown):"
foreach ($r in $rows) {
    $pad = [int](($r.SxLo - $sxMin) * 2)
    Write-Output ((' ' * $pad) + (('o ' * $r.Count).TrimEnd()))
}
