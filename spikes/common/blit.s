; blit.s - text blit in 6502, called from C.
;
; The C renderer in hud.h had already taken the title screen from 4,561,385
; cycles to 1,238,832 by drawing whole bitmap bytes instead of pixels and
; resolving the colour slot once per cell instead of once per pixel. What was
; left was not algorithm, it was cc65: about a thousand byte writes still costing
; 700-1,200 cycles each in 16-bit address arithmetic and call overhead.
;
; Moving only the inner write loop down here got the title to 592,369 -- and then
; the C that remained AROUND it became the cost. Five arguments pushed on cc65's
; software stack, twice per glyph, plus the index arithmetic, came to roughly
; 2,500 cycles a glyph against about 700 spent drawing it. So the glyph loop is
; here too, and C hands over a whole string at once.
;
; Two entry points:
;
;   _blit_text1   a whole string at size 1, which is almost everything on screen
;   _blit_col     one byte column of one glyph, for the scaled sizes
;
; Contract, via globals rather than the C stack, for the reason stamp.s gives:
; pushing the arguments costs more than the routine saves.
;
;   _blit_col     _bl_bc  byte column 0..39      _bl_y   first scanline
;                 _bl_n   mask count             _bl_rep scanlines per mask
;                 _bl_src mask run               _bl_col colour
;                 _bl_ov  non-zero forces colour RAM and pattern 11
;
;   _blit_text1   _bt_src  glyph indices         _bt_n     glyph count
;                 _bt_lx   left edge 0..159      _bt_y     top scanline
;                 _bt_col  colour                _bt_lead  leading glyphs in
;                 _bt_lcol colour for those        the lead colour
;                 _bt_ov   overlay flag
;
; Tables are built once by stamp_init() and font_init() in C.

        .include "zeropage.inc"

        .export _blit_col, _blit_text1
        .import _EXPAND, _SM0, _SM1, _SMANY0, _SMANY1
        .import _ROW_ADDR_LO, _ROW_ADDR_HI
        .import _ROW_CELL_LO, _ROW_CELL_HI
        .import _bl_bc, _bl_y, _bl_n, _bl_rep, _bl_col, _bl_ov, _bl_src
        .import _bt_src, _bt_n, _bt_lx, _bt_y, _bt_col, _bt_lead, _bt_lcol, _bt_ov

BITMAP_HI = $A0         ; bitmap at $A000  (VIC bank 2 + $2000)
SCREEN_HI = $84         ; video matrix at $8400  (bank 2 + $0400)
COLRAM_HI = $D8         ; colour RAM at $D800, never banked

GLYPH_STRIDE = 20       ; SM0/SM1: 4 alignments x 5 rows per glyph

; Same trick as stamp.s: cc65's runtime already fills the zero page on the c64
; target, so the hot variables are aliased onto its scratch locations. Safe
; because nothing here calls back into C, and it never runs at the same time as
; the ball blit.
zy      = tmp2          ; current scanline
zmask   = tmp3          ; this scanline's 4-pixel mask
zpat16  = tmp4          ; cached colour slot, pre-shifted into EXPAND's index
zb8l    = sreg          ; bc * 8, the byte offset within a character row
zb8h    = sreg+1
zsrc    = ptr4          ; mask run pointer
                        ; tmp1 is scratch inside resolve
                        ; ptr1/2/3 are bitmap, screen and colour RAM

        .bss
zi:     .res 1          ; index into the mask run
zrep:   .res 1          ; scanlines left for the current mask
zcrow:  .res 1          ; character row the cached slot belongs to, $FF = none
bti:    .res 1          ; glyph counter
btbc:   .res 1          ; byte column of the glyph being drawn
bta:    .res 1          ; alignment 0..3, the same for every glyph in a string
btx:    .res 1          ; glyph * 4 + alignment
btg:    .res 2          ; that, times 5: the offset into SM0/SM1
btsrc:  .res 2          ; saved glyph-array pointer, since docol clobbers ptr4

        .code

; ---------------------------------------------------------------------------
; _blit_text1 - a whole string, size 1.
;
; The advance is exactly 4 logical pixels, which is one byte column, so the
; alignment within a byte is the SAME for every glyph in the string and the byte
; column simply increments. That is what makes this worth doing as a string
; rather than a glyph: the only per-glyph arithmetic left is the table offset.
; ---------------------------------------------------------------------------
_blit_text1:
        lda _bt_src
        sta btsrc
        lda _bt_src+1
        sta btsrc+1

        lda _bt_lx              ; alignment, fixed for the whole string
        and #3
        sta bta

        lda _bt_lx              ; first byte column
        lsr
        lsr
        sta btbc

        lda #0
        sta bti

        lda _bt_y               ; the parts of the column contract that do not
        sta _bl_y               ; change from glyph to glyph
        lda #5
        sta _bl_n
        lda #1
        sta _bl_rep
        lda _bt_ov
        sta _bl_ov

btloop: lda bti
        cmp _bt_n
        bcc btgo                ; btdone is out of branch range from here
        jmp btdone
btgo:
        cmp _bt_lead            ; lead colour for the first _bt_lead glyphs
        bcs btnorm
        lda _bt_lcol
        jmp btsetc
btnorm: lda _bt_col
btsetc: sta _bl_col

        lda btsrc               ; docol clobbers ptr4, so reload each time
        sta ptr4
        lda btsrc+1
        sta ptr4+1
        ldy bti
        lda (ptr4),y            ; the glyph index

        ; idx = glyph * 4 + alignment, which fits in a byte (36 glyphs x 4), and
        ; is what indexes the two "is this column empty" tables. The table offset
        ; is then idx * 5.
        asl
        asl
        clc
        adc bta                 ; 36 * 4 = 144, so no carry out of the byte
        sta btx

        ; btg = idx * 5
        sta btg
        lda #0
        sta btg+1
        asl btg
        rol btg+1               ; x2
        asl btg
        rol btg+1               ; x4
        clc
        lda btg
        adc btx
        sta btg
        lda btg+1
        adc #0
        sta btg+1               ; x5

        ldx btx
        lda _SMANY0,x           ; left column, out of SM0
        beq btskip0
        lda btbc
        cmp #40
        bcs btskip0
        sta _bl_bc
        clc
        lda #<_SM0
        adc btg
        sta _bl_src
        lda #>_SM0
        adc btg+1
        sta _bl_src+1
        jsr docol
btskip0:
        ldx btx
        lda _SMANY1,x           ; right column, out of SM1 -- empty for most of
        beq btskip1             ; the screen, since a 3-wide glyph at alignment
        lda btbc                ; 0 or 1 fits inside one byte
        clc
        adc #1
        cmp #40
        bcs btskip1
        sta _bl_bc
        clc
        lda #<_SM1
        adc btg
        sta _bl_src
        lda #>_SM1
        adc btg+1
        sta _bl_src+1
        jsr docol
btskip1:
        inc btbc
        inc bti
        jmp btloop

btdone: rts

; ---------------------------------------------------------------------------
; _blit_col - one byte column, for the scaled sizes. C entry point.
; ---------------------------------------------------------------------------
_blit_col:
        jsr docol
        rts

; ---------------------------------------------------------------------------
; docol - the actual column blit. Reads the _bl_* contract.
; ---------------------------------------------------------------------------
docol:  lda _bl_src
        sta zsrc
        lda _bl_src+1
        sta zsrc+1

        lda _bl_y
        sta zy
        lda #0
        sta zi

        lda _bl_bc              ; bc * 8
        sta zb8l
        lda #0
        sta zb8h
        asl zb8l
        rol zb8h
        asl zb8l
        rol zb8h
        asl zb8l
        rol zb8h

        lda #$FF                ; no slot cached yet
        sta zcrow

loop:   ldy zi
        cpy _bl_n
        bcs done

        lda (zsrc),y
        sta zmask

        lda _bl_rep             ; one mask covers `rep` scanlines, which is how
        sta zrep                ; a scaled glyph avoids being expanded in C

rloop:  lda zmask
        beq rnext               ; blank, and most of a glyph is blank

        ; The colour slot only changes when the run crosses into the next
        ; character row -- once every eight scanlines rather than once per
        ; write. That saving is the whole reason this is column-major.
        lda zy
        lsr
        lsr
        lsr
        cmp zcrow
        beq havepat
        sta zcrow
        jsr resolve

havepat:
        ldy zy
        clc
        lda _ROW_ADDR_LO,y
        adc zb8l
        sta ptr1
        lda _ROW_ADDR_HI,y
        adc zb8h
        clc
        adc #BITMAP_HI
        sta ptr1+1

        lda zpat16
        ora zmask
        tax
        lda _EXPAND,x
        ldy #0
        ora (ptr1),y
        sta (ptr1),y

rnext:  inc zy
        dec zrep
        bne rloop

        inc zi
        jmp loop

done:   rts

; ---------------------------------------------------------------------------
; resolve: point ptr2/ptr3 at this character row's screen and colour RAM cell,
; work out which of the cell's three colour slots holds _bl_col -- claiming a
; free one if none does -- and leave the pattern, pre-shifted into EXPAND's
; index, in zpat16.
;
; These are plot()'s rules, not stamp.s's. The board assigns slots statically by
; row parity, which is what makes ball colour clash impossible; text is drawn
; either on a screen with no balls on it or over a board through colour RAM, so
; ordinary first-come allocation never competes for a ball's nibble.
; ---------------------------------------------------------------------------
resolve:
        ldy zy
        clc
        lda _ROW_CELL_LO,y
        adc _bl_bc
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

        ldy #0
        lda _bl_ov
        beq rn

        lda _bl_col             ; overlay: colour RAM, pattern 11, always
        sta (ptr3),y
        lda #$30
        sta zpat16
        rts

rn:     lda (ptr2),y            ; already in the high nibble?
        lsr
        lsr
        lsr
        lsr
        cmp _bl_col
        beq pat1

        lda (ptr2),y            ; the low nibble?
        and #$0F
        cmp _bl_col
        beq pat2

        lda (ptr3),y            ; colour RAM?
        and #$0F
        cmp _bl_col
        beq pat3

        lda (ptr2),y            ; no -- claim the first free slot
        and #$F0
        beq claim1
        lda (ptr2),y
        and #$0F
        beq claim2
        lda (ptr3),y
        and #$0F
        beq claim3
                                ; all three taken by other colours: a clash, and
                                ; slot 1 is what plot() falls back to
pat1:   lda #$10
        sta zpat16
        rts
pat2:   lda #$20
        sta zpat16
        rts
pat3:   lda #$30
        sta zpat16
        rts

claim1: lda (ptr2),y
        and #$0F
        sta tmp1
        lda _bl_col
        asl
        asl
        asl
        asl
        ora tmp1
        sta (ptr2),y
        jmp pat1

claim2: lda (ptr2),y
        and #$F0
        sta tmp1
        lda _bl_col
        and #$0F
        ora tmp1
        sta (ptr2),y
        jmp pat2

claim3: lda _bl_col
        sta (ptr3),y
        jmp pat3
