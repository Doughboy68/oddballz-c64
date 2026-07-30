# make-disk.ps1 [-Out <d64>]
#
# Packages build/game.prg into a .d64 disk image for real hardware.
#
# The .prg alone is already a genuine C64 program -- cc65's c64 target emits the
# standard $0801 load address and a BASIC stub -- so anything that can send a
# PRG (Kung Fu Flash, Ultimate-II, a PC-to-C64 link) can run it directly. A .d64
# is the more universal container: an SD2IEC or Ultimate mounts it as drive 8
# and the C64 loads from it exactly as it would from a 1541.
#
#   LOAD"ODDBALLZ",8,1
#   RUN
#
# The ,1 matters. Without it the KERNAL relocates the file to the BASIC start
# instead of honouring the address in the file, and the program lands in the
# wrong place.

param([string]$Out = "")

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$dist = Join-Path $root 'dist'
if (-not (Test-Path $dist)) { New-Item -ItemType Directory -Path $dist | Out-Null }
if (-not $Out) { $Out = Join-Path $dist 'oddballz.d64' }

$c1541 = "C:\Users\Brian\AppData\Local\Microsoft\WinGet\Packages\VICE-Team.VICE.GTK3_Microsoft.Winget.Source_8wekyb3d8bbwe\GTK3VICE-3.10-win64\bin\c1541.exe"
if (-not (Test-Path $c1541)) { throw "c1541 not found (it ships with VICE)" }

& (Join-Path $PSScriptRoot 'build-game.ps1')
if ($LASTEXITCODE -ne 0) { throw "game build failed" }

$prg = Join-Path $root 'build\game.prg'
if (Test-Path $Out) { Remove-Item $Out -Force }

& $c1541 -format "oddballz,01" d64 $Out -write $prg "oddballz"
if ($LASTEXITCODE -ne 0) { throw "c1541 failed" }

Copy-Item $prg (Join-Path $dist 'oddballz.prg') -Force

Write-Output ("wrote {0} ({1} bytes)" -f $Out, (Get-Item $Out).Length)
Write-Output ("wrote {0} ({1} bytes)" -f (Join-Path $dist 'oddballz.prg'), (Get-Item $prg).Length)
& $c1541 $Out -dir
