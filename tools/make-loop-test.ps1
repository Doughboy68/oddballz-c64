# make-loop-test.ps1
#
# Hand-assembled 6502 for the toolchain loop test. This exists ONLY to prove the
# assemble -> run -> screenshot pipeline works; once a real assembler is approved
# this file is deleted and replaced by proper .asm source.
#
# Program: fill the whole 1000-byte screen with PETSCII $51 (filled circle) in
# yellow on a blue background. Chosen because a screen of balls is instantly
# recognisable in a screenshot -- if the PNG shows it, the whole loop works.
#
#   BASIC stub at $0801:  10 SYS 2061
#   Machine code at $080D.
#
#         lda #$00 : sta $d020      ; black border
#         lda #$06 : sta $d021      ; blue background
#         ldx #$00
#   loop: lda #$51 : sta $0400,x / $0500,x / $0600,x / $06e8,x
#         lda #$07 : sta $d800,x / $d900,x / $da00,x / $dae8,x
#         inx : bne loop
#         jmp *

$ErrorActionPreference = 'Stop'
$outDir = Join-Path $PSScriptRoot '..\build'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
$outFile = Join-Path $outDir 'loop-test.prg'

$bytes = [byte[]]@(
    # --- PRG load address ---
    0x01, 0x08,                   # $0801

    # --- BASIC stub: 10 SYS 2061 ---
    0x0B, 0x08,                   # link to next line ($080B)
    0x0A, 0x00,                   # line number 10
    0x9E,                         # SYS token
    0x32, 0x30, 0x36, 0x31,       # "2061"
    0x00,                         # end of line
    0x00, 0x00,                   # end of program

    # --- machine code at $080D ---
    0xA9, 0x00,                   # lda #$00
    0x8D, 0x20, 0xD0,             # sta $d020   border = black
    0xA9, 0x06,                   # lda #$06
    0x8D, 0x21, 0xD0,             # sta $d021   background = blue
    0xA2, 0x00,                   # ldx #$00
                                  # loop: ($0819)
    0xA9, 0x51,                   # lda #$51    filled circle
    0x9D, 0x00, 0x04,             # sta $0400,x
    0x9D, 0x00, 0x05,             # sta $0500,x
    0x9D, 0x00, 0x06,             # sta $0600,x
    0x9D, 0xE8, 0x06,             # sta $06e8,x
    0xA9, 0x07,                   # lda #$07    yellow
    0x9D, 0x00, 0xD8,             # sta $d800,x
    0x9D, 0x00, 0xD9,             # sta $d900,x
    0x9D, 0x00, 0xDA,             # sta $da00,x
    0x9D, 0xE8, 0xDA,             # sta $dae8,x
    0xE8,                         # inx
    0xD0, 0xE1,                   # bne loop    ($0838 - 31 = $0819)
    0x4C, 0x38, 0x08              # jmp $0838   spin forever
)

[System.IO.File]::WriteAllBytes($outFile, $bytes)
Write-Output "wrote $outFile ($($bytes.Length) bytes)"
