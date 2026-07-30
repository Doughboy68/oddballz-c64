; stamp.s - ball blit in 6502, called from C.
;
; The C version in stamp.h got a four-ball landing from 1,994,071 cycles down to
; 50,300 -- 2.56 PAL frames. That is survivable for a one-off landing but not
; for cascades, where twenty freed balls would stall a quarter of a second.
; What was left was not algorithm but cc65 code generation: ~22 byte writes and
; ~6 colour-slot claims per ball should not cost 12,575 cycles.
;
; ca65 rather than KickAssembler, deliberately: ca65 is cc65's own assembler, so
; this links natively against the C object and shares its symbols. KickAssembler
; builds standalone .prg files and cannot do that.
;
; Contract, via globals rather than the C stack (pushing three arguments costs
; more than this routine saves):
;
;   _stamp_bc    byte column, (cx - 4) >> 2      cx is always a multiple of 4,
;   _stamp_y0    topmost scanline, cy - BALL_H/2 so a ball covers exactly two
;   _stamp_col   ball colour 1..15               byte-aligned columns
;
; Tables are built once by stamp_init() in C.

        .include "zeropage.inc"

        .export _stamp_ball_asm
        .import _BALL_MASK, _EXPAND
        .import _ROW_ADDR_LO, _ROW_ADDR_HI
        .import _ROW_CELL_LO, _ROW_CELL_HI
        .import _stamp_bc, _stamp_y0, _stamp_col, _stamp_slot

BALL_ROWS = 11
BITMAP_HI = $A0         ; bitmap at $A000  (VIC bank 2 + $2000)
SCREEN_HI = $84         ; video matrix at $8400  (bank 2 + $0400)
COLRAM_HI = $D8         ; colour RAM at $D800, never banked

; cc65's runtime already fills the zero page on the c64 target -- adding even
; one byte to the ZEROPAGE segment overflows it. So the hot variables are
; aliased onto cc65's own scratch locations, which are free to clobber inside a
; leaf routine that never calls back into C. ptr1/2/3 are the live pointers.
zy      = tmp2          ; current scanline
zm0     = tmp3          ; mask, left byte column
zm1     = tmp4          ; mask, right byte column
zp0     = ptr4          ; claimed colour slot, left  (0 = not yet)
zp1     = ptr4+1        ; claimed colour slot, right
zclr    = sreg          ; inverted clear mask
zval    = sreg+1        ; packed pixel value
                        ; tmp1 is scratch inside claim

; Touched a few times per ball rather than per pixel, so absolute addressing
; costs nothing measurable and keeps the zero page within budget.
        .bss
zr:     .res 1          ; row counter, 0..BALL_ROWS-1
zidx:   .res 1          ; index into BALL_MASK
zb8l:   .res 1          ; bc * 8, low
zb8h:   .res 1          ; bc * 8, high

        .code

_stamp_ball_asm:
        lda _stamp_y0
        sta zy
        lda #0
        sta zr
        sta zidx

        ; bc * 8, the byte offset of the left column within a character row
        lda _stamp_bc
        sta zb8l
        lda #0
        sta zb8h
        asl zb8l
        rol zb8h
        asl zb8l
        rol zb8h
        asl zb8l
        rol zb8h

loop:   ldx zidx
        lda _BALL_MASK,x
        sta zm0
        lda _BALL_MASK+1,x
        sta zm1
        inx
        inx
        stx zidx

        ; Unsigned compare catches both y > 199 and any wrapped negative y.
        ; Branched around a jmp: the draw body is well over the 6502's +/-127
        ; byte relative branch range.
        lda zy
        cmp #200
        bcc inrange
        jmp nextrow
inrange:

        ; Recompute pointers only when crossing into a new character row --
        ; within one, consecutive scanlines are consecutive bytes.
        lda zr
        beq setuprow
        lda zy
        and #7
        bne samerow
setuprow:
        jsr setup

samerow:
        lda zm0
        beq skip0
        lda _stamp_col          ; colour 0 means erase: no slot to claim
        beq have0
        lda zp0
        bne have0
        ldy #0
        jsr setslot
        sta zp0
have0:  lda #$30                ; slot 3 = 11 everywhere = the clear mask
        ora zm0
        tax
        lda _EXPAND,x
        eor #$FF
        sta zclr
        lda _stamp_col
        beq clear0
        lda zp0
        asl
        asl
        asl
        asl
        ora zm0
        tax
        lda _EXPAND,x
        sta zval
        jmp write0
clear0: lda #0                  ; erase: mask out, or in nothing
        sta zval
write0: ldy #0
        lda (ptr1),y
        and zclr
        ora zval
        sta (ptr1),y

skip0:  lda zm1
        beq skip1
        lda _stamp_col
        beq have1
        lda zp1
        bne have1
        ldy #1
        jsr setslot
        sta zp1
have1:  lda #$30
        ora zm1
        tax
        lda _EXPAND,x
        eor #$FF
        sta zclr
        lda _stamp_col
        beq clear1
        lda zp1
        asl
        asl
        asl
        asl
        ora zm1
        tax
        lda _EXPAND,x
        sta zval
        jmp write1
clear1: lda #0
        sta zval
write1: ldy #8                  ; right column is 8 bytes on in the same row
        lda (ptr1),y
        and zclr
        ora zval
        sta (ptr1),y

skip1:  inc ptr1
        bne nextrow
        inc ptr1+1

nextrow:
        inc zy
        inc zr
        lda zr
        cmp #BALL_ROWS
        beq done
        jmp loop                ; too far back for a relative branch
done:   rts

; ---------------------------------------------------------------------------
; setup: point ptr1 at the bitmap byte, ptr2/ptr3 at screen and colour RAM for
; this character row, and forget any slots claimed in the previous one.
; ---------------------------------------------------------------------------
setup:  ldy zy
        clc
        lda _ROW_ADDR_LO,y
        adc zb8l
        sta ptr1
        lda _ROW_ADDR_HI,y
        adc zb8h
        clc
        adc #BITMAP_HI
        sta ptr1+1

        clc
        lda _ROW_CELL_LO,y
        adc _stamp_bc
        sta ptr2
        sta ptr3
        lda _ROW_CELL_HI,y
        adc #0
        pha
        clc
        adc #SCREEN_HI
        sta ptr2+1
        pla
        clc
        adc #COLRAM_HI
        sta ptr3+1

        lda #0
        sta zp0
        sta zp1
        rts

; ---------------------------------------------------------------------------
; setslot: write _stamp_col into this cell's nibble for _stamp_slot, and return
; the 2-bit pattern (1 or 2) in A.
;
; Slots are assigned STATICALLY by the ball's grid-row parity rather than
; allocated first-come-first-served, which makes colour clash structurally
; impossible rather than merely rare:
;
;   even ball rows -> slot 1 -> screen hi nibble -> bit pattern 01
;   odd  ball rows -> slot 2 -> screen lo nibble -> bit pattern 10
;
; That is sound because of the geometry. Balls are byte-aligned and 8 logical px
; apart, so a cell holds exactly one ball per ball-row; and a cell spans 8
; scanlines while balls sit on a 10-pixel pitch, so it can only ever meet two
; CONSECUTIVE ball rows -- which are always opposite parity. One even ball and
; one odd ball per cell, each with its own nibble, permanently.
;
; The previous dynamic allocator leaked: it only released a slot when a cell
; became completely empty, and once every empty cell was drawn as a socket no
; cell was ever empty again, so a third and fourth colour accumulated and the
; fourth was drawn in the wrong colour.
;
; Colour RAM (pattern 11) is now unused by the board and free for effects.
; ---------------------------------------------------------------------------
setslot:
        lda _stamp_slot
        cmp #1
        bne ss_lo

        lda (ptr2),y            ; slot 1: replace the high nibble
        and #$0F
        sta tmp1
        lda _stamp_col
        asl
        asl
        asl
        asl
        ora tmp1
        sta (ptr2),y
        lda #1
        rts

ss_lo:  lda (ptr2),y            ; slot 2: replace the low nibble
        and #$F0
        sta tmp1
        lda _stamp_col
        and #$0F
        ora tmp1
        sta (ptr2),y
        lda #2
        rts
