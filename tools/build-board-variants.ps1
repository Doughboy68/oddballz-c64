# build-board-variants.ps1
#
# Builds and screenshots the three board-size variants that the ball-size
# decision comes down to. See spikes/board/render-board.c for the geometry.
#
#   A  correct proportions, full board, small balls
#   B  bigger balls, full board, hexagon squashed vertically
#   C  bigger balls, correct proportions, spawn rows not drawn

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

$variants = @(
    @{ Name = 'A-full-correct';   PitchX = 6; PitchY = 9;  RowFirst = 0 },
    @{ Name = 'B-full-squashed';  PitchX = 8; PitchY = 9;  RowFirst = 0 },
    @{ Name = 'C-cropped-bigger'; PitchX = 8; PitchY = 11; RowFirst = 4 }
)

foreach ($v in $variants) {
    $prg = Join-Path $root "build\board-$($v.Name).prg"
    # Quote the -D args: unquoted, PowerShell splits them on '=' and cl65 sees
    # the value as a stray filename.
    & "$root\vendor\cc65\bin\cl65.exe" -t c64 -O `
        "-DPITCH_X=$($v.PitchX)" "-DPITCH_Y=$($v.PitchY)" "-DROW_FIRST=$($v.RowFirst)" `
        "$root\spikes\board\render-board.c" -o $prg
    if ($LASTEXITCODE -ne 0) { throw "build failed for $($v.Name)" }
    Write-Output ("built {0}  pitch {1}x{2}  rows {3}-19  ({4} bytes)" -f `
        $v.Name, $v.PitchX, $v.PitchY, $v.RowFirst, (Get-Item $prg).Length)

    # The renderer plots ~238 ellipses in C using long multiplication, which is
    # brutally slow on a 6502 -- it needs well over 60 s of emulated time. The
    # border turns white only when the last ball is drawn, so a black border in
    # the screenshot means this budget was still too small.
    & "$root\tools\run-vice.ps1" -Prg $prg -Cycles 400000000
}
