; sockets.s - the empty playfield, a whole grid row at a time.
;
; draw_playfield() used to call stamp_ball_fast once per cell: 271 sockets at
; about 4,000 cycles each, 1,090,001 in total, 1.1 seconds at the start of every
; game. That is not the C loop -- it is stamp.s run 271 times, and stamp.s is
; already assembly. So this is an algorithm change rather than a language one.
;
; Everything stamp.s re-derives per ball is CONSTANT for a row of sockets:
;
;   the colour is the same for every socket on the board
;   the colour slot is fixed by the grid row's parity, so it is the same for the
;     whole row
;   the eleven pairs of mask bytes are the same, because every socket is the
;     same shape
;   the scanlines are the same, because the row is level
;
; So the two packed bytes for a scanline are worked out ONCE and then stored
; straight across the row. Sockets sit 8 logical pixels apart and are 7 wide on
; two byte columns, so they never touch: the pointer simply steps 16 bytes per
; socket, and no socket can overlap the next.
;
; Plain stores, not read-modify-write. The board is drawn onto a bitmap
; vic_bitmap_mode() has just cleared, so there is nothing underneath to preserve.
;
; Contract, via globals as elsewhere:
;
;   _sr_bc0   byte column of the leftmost socket
;   _sr_n     sockets in this row, all contiguous -- a hexagon has no gaps
;   _sr_y0    topmost scanline
;   _sr_slot  1 for an even grid row, 2 for an odd one
;   _sr_col   the socket colour

        .include "zeropage.inc"

        .export _socket_row
        .import _BALL_MASK, _EXPAND
        .import _ROW_ADDR_LO, _ROW_ADDR_HI
        .import _ROW_CELL_LO, _ROW_CELL_HI
        .import _sr_bc0, _sr_n, _sr_y0, _sr_slot, _sr_col

BALL_ROWS = 11
BITMAP_HI = $A0
SCREEN_HI = $84

zy      = tmp2          ; current scanline
zv0     = tmp3          ; packed byte, left column
zv1     = tmp4          ; packed byte, right column
zb8l    = sreg          ; bc0 * 8
zb8h    = sreg+1
                        ; ptr1 bitmap, ptr2 screen, tmp1 scratch

        .bss
zr:     .res 1          ; scanline within the ball, 0..BALL_ROWS-1
zidx:   .res 1          ; index into BALL_MASK
zi:     .res 1          ; socket counter
znib:   .res 1          ; the colour, pre-shifted for this row's slot
zmask:  .res 1          ; which nibble to keep when writing it

        .code

_socket_row:
        lda _sr_n
        bne go
        rts
go:
        lda _sr_y0
        sta zy
        lda #0
        sta zr
        sta zidx

        lda _sr_bc0             ; bc0 * 8
        sta zb8l
        lda #0
        sta zb8h
        asl zb8l
        rol zb8h
        asl zb8l
        rol zb8h
        asl zb8l
        rol zb8h

        ; The slot decides which nibble of screen RAM the colour goes in, and it
        ; is the same for every cell in the row. Work it out once.
        lda _sr_slot
        cmp #1
        bne slot2
        lda _sr_col
        asl
        asl
        asl
        asl
        sta znib
        lda #$0F                ; keep the low nibble
        sta zmask
        jmp rowloop
slot2:  lda _sr_col
        and #$0F
        sta znib
        lda #$F0                ; keep the high nibble
        sta zmask

rowloop:
        lda zy
        cmp #200
        bcc inrange
        jmp nextrow
inrange:

        ; Colour RAM is untouched: the board only ever uses the two screen
        ; nibbles, which is what leaves pattern 11 free for the overlays.
        lda zr
        beq docells
        lda zy
        and #7
        bne nocells
docells:
        jsr cells

nocells:
        ldx zidx                ; this scanline's two packed bytes
        lda _BALL_MASK,x
        beq zero0
        ldy _sr_slot
        ora nibtab,y
        tax
        lda _EXPAND,x
        jmp have0
zero0:  lda #0
have0:  sta zv0

        ldx zidx
        lda _BALL_MASK+1,x
        beq zero1
        ldy _sr_slot
        ora nibtab,y
        tax
        lda _EXPAND,x
        jmp have1
zero1:  lda #0
have1:  sta zv1

        inc zidx
        inc zidx

        ldy zy                  ; bitmap pointer for this scanline
        clc
        lda _ROW_ADDR_LO,y
        adc zb8l
        sta ptr1
        lda _ROW_ADDR_HI,y
        adc zb8h
        clc
        adc #BITMAP_HI
        sta ptr1+1

        ; The whole point: n sockets, two stores each, pointer stepping 16.
        lda _sr_n
        sta zi
across: ldy #0
        lda zv0
        sta (ptr1),y
        ldy #8
        lda zv1
        sta (ptr1),y

        clc                     ; next socket is 16 bytes on
        lda ptr1
        adc #16
        sta ptr1
        bcc noc
        inc ptr1+1
noc:    dec zi
        bne across

nextrow:
        inc zy
        inc zr
        lda zr
        cmp #BALL_ROWS
        beq done
        jmp rowloop
done:   rts

; EXPAND's index is (pattern << 4) | mask, and the pattern is the slot.
nibtab: .byte $00, $10, $20, $30

; ---------------------------------------------------------------------------
; cells: put the colour in this row of cells' nibble, for all n sockets. Called
; when the scanline run enters a new character row, so twice per grid row rather
; than once per socket.
;
; Read-modify-write, because a character row is eight scanlines and sockets sit
; on a ten-pixel pitch: a cell can hold two sockets from CONSECUTIVE grid rows,
; which are always opposite parity and so always want different nibbles. That is
; the same argument stamp.s relies on to make colour clash impossible.
; ---------------------------------------------------------------------------
cells:  ldy zy
        clc
        lda _ROW_CELL_LO,y
        adc _sr_bc0
        sta ptr2
        lda _ROW_CELL_HI,y
        adc #0
        clc
        adc #SCREEN_HI
        sta ptr2+1

        lda _sr_n
        sta tmp1
cloop:  ldy #0
        lda (ptr2),y
        and zmask
        ora znib
        sta (ptr2),y
        iny
        lda (ptr2),y
        and zmask
        ora znib
        sta (ptr2),y

        clc                     ; two cells per socket
        lda ptr2
        adc #2
        sta ptr2
        bcc cnoc
        inc ptr2+1
cnoc:   dec tmp1
        bne cloop
        rts
