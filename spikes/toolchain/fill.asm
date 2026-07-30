// fill.asm - KickAssembler toolchain check.
//
// Same idea as the hand-assembled loop test, but built by a real assembler and
// in deliberately different colours (cyan balls on purple) so a screenshot
// proves THIS build ran, not a stale one.

.pc = $0801 "BASIC upstart"
:BasicUpstart2(start)

.pc = $0810 "Main"
start:
        lda #$00
        sta $d020               // border: black
        lda #$04
        sta $d021               // background: purple

        ldx #$00
loop:
        lda #$51                // PETSCII filled circle
        sta $0400,x
        sta $0500,x
        sta $0600,x
        sta $06e8,x
        lda #$03                // cyan
        sta $d800,x
        sta $d900,x
        sta $da00,x
        sta $dae8,x
        inx
        bne loop

spin:   jmp spin
