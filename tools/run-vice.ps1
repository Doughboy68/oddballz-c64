# run-vice.ps1 -Prg <path> [-Cycles <n>] [-Shot <path>]
#
# Runs a .prg headlessly in VICE for a bounded number of cycles, then exits and
# dumps a PNG screenshot. This is the verification loop: Claude can read the PNG
# even though it cannot see native Windows GUI windows.
#
# ~985,248 cycles = 1 second of emulated PAL time.

param(
    [Parameter(Mandatory = $true)][string]$Prg,
    [int]$Cycles = 8000000,
    [string]$Shot
)

$ErrorActionPreference = 'Stop'

$exe = "C:\Users\Brian\AppData\Local\Microsoft\WinGet\Packages\VICE-Team.VICE.GTK3_Microsoft.Winget.Source_8wekyb3d8bbwe\GTK3VICE-3.10-win64\bin\x64sc.exe"
if (-not (Test-Path $exe)) { throw "x64sc.exe not found at $exe" }

$Prg = (Resolve-Path $Prg).Path
if (-not $Shot) {
    $shotDir = Join-Path $PSScriptRoot '..\shots'
    if (-not (Test-Path $shotDir)) { New-Item -ItemType Directory -Path $shotDir | Out-Null }
    $Shot = Join-Path (Resolve-Path $shotDir).Path ((Get-Item $Prg).BaseName + '.png')
}
if (Test-Path $Shot) { Remove-Item $Shot -Force }

$argList = @(
    '-autostart', "`"$Prg`"",
    '-limitcycles', $Cycles,
    '-exitscreenshot', "`"$Shot`"",
    '-warp',
    '-autostart-warp',
    '-sounddev', 'dummy'
)

$p = Start-Process -FilePath $exe -ArgumentList $argList -PassThru
if (-not $p.WaitForExit(120000)) {
    $p.Kill()
    throw "VICE did not exit within 120s (limitcycles may not have applied)"
}

if (Test-Path $Shot) {
    Write-Output "OK  exit=$($p.ExitCode)  shot=$Shot  bytes=$((Get-Item $Shot).Length)"
} else {
    Write-Output "FAIL exit=$($p.ExitCode)  no screenshot written to $Shot"
}
