/* stamp.h - fast ball blit into the multicolour bitmap.
 *
 * Replaces draw_ball() on the hot path. draw_ball walks the ellipse pixel by
 * pixel and recomputes its half-width with 32-bit multiplication on every
 * scanline; that measured at 1,994,071 cycles for a four-ball landing, about
 * 101 PAL frames.
 *
 * The geometry is far kinder than that:
 *
 *   Ball centres always land on a multiple of 4 logical px (ORIGIN_X is 48,
 *   PITCH_X is 8 and the shear is PITCH_X/2), and the ball is 7 px across. So
 *   a ball ALWAYS covers exactly two byte-aligned columns -- no shifting, no
 *   straddling. A stamp is 11 scanlines x 2 bytes.
 *
 * Three precomputed tables remove the remaining arithmetic:
 *
 *   BALL_MASK  which pixels are set, per scanline, per byte column
 *   EXPAND     4-bit mask + colour slot -> the packed 2-bit multicolour byte
 *   ROW_ADDR   scanline -> bitmap offset, and ROW_CELL -> attribute cell index,
 *              so the per-pixel (y>>3)*320 multiply disappears
 *
 * Colour slots are allocated once per cell per ball rather than once per pixel,
 * which is where most of the rest of the saving comes from.
 *
 * Requires board.h.
 */

#ifndef STAMP_H
#define STAMP_H

#include "board.h"

#define BALL_ROWS (BALL_H + 1)   /* draw_ball runs dy = -ry..ry inclusive */

/* All flattened to 1-D. cc65 turns a[i][j] into a real multiply, which on the
 * hot path cost more than the work itself: the 2-D version measured 113,090
 * cycles where the byte writes alone should be a few thousand. EXPAND is
 * indexed (slot << 4) | mask so the subscript is a shift and an or. */
/* Not static: stamp.s imports these by their cc65-mangled names (_BALL_MASK
 * and friends). Safe because this header is included in exactly one
 * translation unit. */
unsigned char BALL_MASK[BALL_ROWS * 2];
unsigned char EXPAND[64];

/* Byte-split copies of the same two tables. 6502 indexed addressing loads a
 * byte, so splitting lo/hi turns a 16-bit table lookup into two `lda tbl,y`. */
unsigned char ROW_ADDR_LO[200], ROW_ADDR_HI[200];
unsigned char ROW_CELL_LO[200], ROW_CELL_HI[200];

/* Parameters for the assembly blit. Passed as globals rather than arguments:
 * cc65's calling convention would push them on the C stack, which costs more
 * than the routine saves. */
unsigned char stamp_bc;     /* byte column = (cx - 4) >> 2 */
unsigned char stamp_y0;     /* topmost scanline = cy - BALL_H/2 */
unsigned char stamp_col;
unsigned char stamp_slot;   /* 1 = even grid row, 2 = odd; see stamp.s */

extern void stamp_ball_asm(void);

static void stamp_init(void)
{
    int dy, dx, y, ry = BALL_H / 2;
    unsigned char pat, m, b;

    for (y = 0; y < BALL_ROWS * 2; ++y) BALL_MASK[y] = 0;

    /* Mask, in the same shape draw_ball produces so stamped and drawn balls are
     * pixel-identical. Pixel p runs 0..7 across the two byte columns, with the
     * ball centred at p = 4; within a byte, pixel j sits at bit 3-j. */
    for (dy = -ry; dy <= ry; ++dy) {
        int w = ball_halfwidth(dy);
        for (dx = -w; dx <= w; ++dx) {
            int p = dx + 4;
            if (p < 0 || p > 7) continue;
            BALL_MASK[((dy + ry) << 1) + (p >> 2)] |= (unsigned char)(1 << (3 - (p & 3)));
        }
    }

    /* EXPAND[slot][mask]: mask bit b becomes the 2-bit slot value at shift 2b.
     * EXPAND[3] doubles as the clear mask, since slot 3 is 11 in every set
     * position. */
    for (pat = 0; pat < 4; ++pat) {
        for (m = 0; m < 16; ++m) {
            unsigned char v = 0;
            for (b = 0; b < 4; ++b)
                if (m & (1 << b)) v = (unsigned char)(v | (pat << (b << 1)));
            EXPAND[(pat << 4) | m] = v;
        }
    }

    for (y = 0; y < 200; ++y) {
        unsigned int a = (unsigned int)((y >> 3) * 320 + (y & 7));
        unsigned int c = (unsigned int)((y >> 3) * 40);
        ROW_ADDR_LO[y] = (unsigned char)a; ROW_ADDR_HI[y] = (unsigned char)(a >> 8);
        ROW_CELL_LO[y] = (unsigned char)c; ROW_CELL_HI[y] = (unsigned char)(c >> 8);
    }
}

/* Assembly blit. cx must be a multiple of 4, and `odd` is the parity of the
 * ball's GRID ROW, which picks the colour slot -- see stamp.s for why that is
 * both sufficient and clash-proof. Colour 0 erases: the routine skips the slot
 * write and masks the ball's pixels out instead of oring them in. */
static void stamp_ball_fast(int cx, int cy, unsigned char col, unsigned char odd)
{
    stamp_bc   = (unsigned char)((cx - 4) >> 2);
    stamp_y0   = (unsigned char)(cy - BALL_H / 2);
    stamp_col  = col;
    stamp_slot = (unsigned char)(odd ? 2 : 1);
    stamp_ball_asm();
}

#define erase_ball_fast(cx, cy, odd) stamp_ball_fast((cx), (cy), 0, (odd))

/* A whole grid row of empty sockets at once -- see sockets.s. The board is 271
 * cells and every one of them is the same shape and colour, so stamping them
 * individually re-derives the same answer 271 times. */
unsigned char sr_bc0, sr_n, sr_y0, sr_slot, sr_col;
void socket_row(void);

/* With slots fixed by row parity there is nothing to release: erasing is just
 * erasing. The old release_cells() walked every cell the ball touched looking
 * for one left completely empty, cost ~6,780 cycles per ball -- roughly twice
 * the blit it followed -- and could not fix the leak anyway once every empty
 * cell was drawn as a socket, because then no cell was ever empty. */
#define erase_ball_clean(cx, cy, odd) erase_ball_fast((cx), (cy), (odd))

/* The C stamp_ball() and claim_slot() that used to live here are gone. They
 * were superseded by stamp.s and had no callers left, but their two 16-bit row
 * tables were still 800 bytes of BSS -- which is what pushed BSS through the
 * screen at $5C00 and produced glitches and crashes. */

#endif /* STAMP_H */
