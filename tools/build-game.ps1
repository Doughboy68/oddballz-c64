# build-game.ps1 [-Defines @("BG_COL=0", ...)] [-Out <prg>]
#
# Builds the game AND checks the memory map, because the same bug has now bitten
# three times and is invisible until the machine misbehaves:
#
#   BSS ran $1F03-$289F, through a bitmap at $2000   -> black screen
#   BSS ran $3F37-$4C74, through a screen at $4400   -> caught before shipping
#   BSS ran $4F2D-$5C94, through a screen at $5C00   -> glitches and crashes
#
# Nothing warns about it: the linker is happy, the program links, and the VIC
# and the C variables simply share memory. So the check belongs in the build.

param(
    [string[]]$Defines = @(),
    [string]$Out = ""
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
if (-not $Out) { $Out = Join-Path $root 'build\game.prg' }
$map = [IO.Path]::ChangeExtension($Out, '.map')

# Must match board.h. The screen is the lowest VIC structure, so BSS has to
# finish below it; the bitmap sits above at $6000.
$SCREEN = 0x8400
$BITMAP = 0xA000

$args = @('-t', 'c64', '-Oirs')
foreach ($d in $Defines) { $args += "-D$d" }
$args += @(
    (Join-Path $root 'engine\game.c'),
    (Join-Path $root 'spikes\common\stamp.s'),
    (Join-Path $root 'spikes\common\blit.s'),
    (Join-Path $root 'spikes\common\sockets.s'),
    '-o', $Out, '-m', $map
)

# No 2>&1 here: in Windows PowerShell that wraps a native command's stderr in
# ErrorRecords and trips $ErrorActionPreference even on a plain warning.
& (Join-Path $root 'vendor\cc65\bin\cl65.exe') @args
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

$line = Get-Content $map | Select-String -Pattern '^BSS\s+([0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)\s'
if (-not $line) { throw "no BSS line in $map" }
$start = [Convert]::ToInt32($line.Matches[0].Groups[1].Value, 16)
$end   = [Convert]::ToInt32($line.Matches[0].Groups[2].Value, 16)

$hdr = $SCREEN - $end - 1
Write-Output ("BSS  {0:X4}-{1:X4}   screen {2:X4}   headroom {3} bytes" -f $start, $end, $SCREEN, $hdr)

if ($end -ge $SCREEN) {
    throw ("BSS ends at `${0:X4} and overruns the screen at `${1:X4} by {2} bytes. " -f $end, $SCREEN, ($end - $SCREEN + 1)) +
          "The VIC and the C variables are sharing memory: expect graphical corruption and crashes. " +
          "Either shrink BSS or move the VIC structures (see board.h)."
}
if ($end -ge $BITMAP) { throw "BSS overruns the bitmap as well" }
if ($hdr -lt 256) { Write-Warning "under 256 bytes of headroom below the screen -- this will bite again soon" }

Write-Output ("wrote {0} ({1} bytes)" -f $Out, (Get-Item $Out).Length)
