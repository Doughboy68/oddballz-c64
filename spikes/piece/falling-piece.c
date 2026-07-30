/* falling-piece.c - sprite-driven falling piece over the static board.
 *
 * The first thing that moves, and the first real measurement of the frame
 * budget. No match logic, no cascades.
 *
 * Two costs are measured separately, using the standard C64 trick of changing
 * the border colour around the work. The border band's height IS the cycle
 * count, which makes raster timing visible in a screenshot -- the one profiling
 * method that survives not being able to see the emulator window.
 *
 *   WHITE border  per-frame work: repositioning four sprites
 *   RED border    landing: stamping four balls into the bitmap
 *
 * A PAL frame is 312 raster lines / ~19,656 cycles. If a red band covers whole
 * frames, the naive approach has blown the budget and the stamp needs a
 * precomputed blit instead of per-pixel ellipse maths.
 *
 * Sprite plan: four balls = four hardware sprites sharing one shape, each with
 * its own colour from the per-sprite register. Position is a register write,
 * so smooth motion costs almost nothing -- this is the mechanism that should
 * make the C64 version smoother than the 1992 original.
 */

#include "../common/board.h"
#include "../common/stamp.h"

/* Sprite shape sits immediately above the bitmap, inside VIC bank 1. The bitmap
 * is $6000..$7F3F, so $7F40 is free and 64-byte aligned; the pointer value is
 * the offset within the bank, ($7F40 - $4000) / 64 = 253. */
#define SPRITE_DATA ((unsigned char *)0x7F40)
#define SPRITE_PTR  253
#define NBALLS      4

/* Where the piece resets to, and how far it falls before "landing". */
#define PIECE_X     6
#define START_ROW   0
#define LAND_ROW    14

static const unsigned char PIECE_COLS[NBALLS] = { 2, 7, 5, 3 };

/* Measured cycle counts, parked in the tape buffer where nothing else lives so
 * peek.ps1 can read them straight out of a running emulator:
 *   $0350-$0353  per-frame sprite reposition, 32-bit little endian
 *   $0354-$0357  landing stamp of four balls
 * A PAL frame is 19,656 cycles; the visible portion is about 15,700. */
#define RESULTS ((unsigned char *)0x0350)
#define CIA1    ((unsigned char *)0xDC00)
#define CIA2    ((unsigned char *)0xDD00)

/* CIA2 timer A counts system cycles and timer B counts A's underflows, giving
 * a 32-bit cycle counter. Timer A alone wraps at 65,536 -- under four frames --
 * and the stamp is expected to overrun that. */
static void timer_start(void)
{
    CIA2[0x0E] = 0x00; CIA2[0x0F] = 0x00;         /* stop both */
    CIA2[0x0D] = 0x7F;                            /* no CIA2 NMIs on underflow */
    CIA2[0x04] = 0xFF; CIA2[0x05] = 0xFF;         /* TA latch $FFFF */
    CIA2[0x06] = 0xFF; CIA2[0x07] = 0xFF;         /* TB latch $FFFF */
    CIA2[0x0F] = 0x41;                            /* TB: count TA underflows */
    CIA2[0x0E] = 0x01;                            /* TA: count phi2 */
}

static unsigned long timer_read(void)
{
    unsigned int tb = (unsigned int)CIA2[0x06] | ((unsigned int)CIA2[0x07] << 8);
    unsigned int ta = (unsigned int)CIA2[0x04] | ((unsigned int)CIA2[0x05] << 8);
    return ((unsigned long)(0xFFFFU - tb) << 16) | (unsigned long)(0xFFFFU - ta);
}

static void store_result(unsigned char slot, unsigned long v)
{
    RESULTS[slot + 0] = (unsigned char)(v);
    RESULTS[slot + 1] = (unsigned char)(v >> 8);
    RESULTS[slot + 2] = (unsigned char)(v >> 16);
    RESULTS[slot + 3] = (unsigned char)(v >> 24);
}

/* Build the ball shape into sprite memory, reusing ball_halfwidth() so a
 * falling ball is pixel-identical to a landed one. Multicolour sprite: 12
 * double-width px across, 21 tall. Bit pattern 10 selects the per-sprite
 * colour, so each of the four balls can differ. */
static void build_sprite(void)
{
    int dy, dx, ry = BALL_H / 2;
    unsigned char i;

    for (i = 0; i < 63; ++i) SPRITE_DATA[i] = 0;

    for (dy = -ry; dy <= ry; ++dy) {
        int w = ball_halfwidth(dy);
        int row = 5 + dy;
        if (row < 0 || row > 20) continue;
        for (dx = -w; dx <= w; ++dx) {
            int px = 4 + dx;
            if (px < 0 || px > 11) continue;
            SPRITE_DATA[row * 3 + (px >> 2)] |=
                (unsigned char)(2 << ((3 - (px & 3)) << 1));
        }
    }
}

static void sprites_init(void)
{
    unsigned char i;
    for (i = 0; i < NBALLS; ++i) {
        SCREEN[0x3F8 + i] = SPRITE_PTR;      /* all four share one shape */
        VIC[0x27 + i]     = PIECE_COLS[i];
    }
    VIC[0x1C] = 0x0F;    /* multicolour sprites 0-3 */
    VIC[0x25] = 0x0B;    /* shared MC colours, unused by this shape */
    VIC[0x26] = 0x0C;
    VIC[0x1B] = 0x00;    /* sprites in front of the bitmap */
    VIC[0x15] = 0x0F;    /* enable 0-3 */
}

/* Ball centre sits at sprite MC pixel 4 (hires offset 8) and sprite row 5.
 * Screen x 0 is sprite x 24; bitmap y 0 is raster 51. */
static void sprite_pos(unsigned char i, int lx, int ly)
{
    unsigned int sx = (unsigned int)(16 + 2 * lx);
    VIC[0x00 + i * 2] = (unsigned char)(sx & 0xFF);
    VIC[0x01 + i * 2] = (unsigned char)(45 + ly);
    if (sx > 255) VIC[0x10] = (unsigned char)(VIC[0x10] |  (1 << i));
    else          VIC[0x10] = (unsigned char)(VIC[0x10] & ~(1 << i));
}

static void wait_raster(unsigned char line)
{
    while (VIC[0x12] != line) { }
}

/* Precomputed per-row geometry. CELL_X/CELL_Y are fine for a one-off board
 * draw but ruinous per frame: they cost cc65 a 16-bit multiply and divide each,
 * which measured at 10,671 cycles for four sprites -- 54% of a PAL frame just
 * to reposition them. Tables turn that into indexed loads.
 *
 * ROW_SHEAR is the sub-row horizontal slide, (off * PITCH_X/2) / PITCH_Y,
 * tabulated so the per-frame path contains no division at all. */
static int ROW_BASE_X[MAX_Y + 1];
static int ROW_BASE_Y[MAX_Y + 1];
static int ROW_SHEAR[PITCH_Y];

static void build_tables(void)
{
    signed char y;
    int o;
    for (y = 0; y <= MAX_Y; ++y) {
        ROW_BASE_X[y] = CELL_X(PIECE_X, y);
        ROW_BASE_Y[y] = CELL_Y(y);
    }
    for (o = 0; o < PITCH_Y; ++o) ROW_SHEAR[o] = (o * (PITCH_X / 2)) / PITCH_Y;
}

int main(void)
{
    signed char row = START_ROW;
    int off = 0;
    unsigned char i;

    vic_bitmap_mode();
    draw_board_empty(11);      /* dark grey: the empty playfield */
    build_sprite();
    sprites_init();
    build_tables();
    stamp_init();

    /* Silence the KERNAL's 60 Hz IRQ so it cannot inflate the measurements. */
    CIA1[0x0D] = 0x7F;

    VIC[0x20] = 1;      /* board finished -- see README on the completion flag */

    for (;;) {
        wait_raster(250);

        /* --- per-frame work ------------------------------------------- */
        VIC[0x20] = 1;
        timer_start();
        {
            /* The shear means falling a row also slides the piece half a ball
             * left, so interpolate x as well as y. Otherwise the piece snaps
             * sideways once per row instead of tracking the hex diagonal. */
            int bx = ROW_BASE_X[row] - ROW_SHEAR[off];
            int by = ROW_BASE_Y[row] + off;
            for (i = 0; i < NBALLS; ++i) sprite_pos(i, bx + i * PITCH_X, by);
        }
        store_result(0, timer_read());
        VIC[0x20] = 0;

        if (++off >= PITCH_Y) { off = 0; ++row; }

        if (row >= LAND_ROW) {
            /* --- landing: stamp the piece into the bitmap -------------- */
            VIC[0x20] = 2;
            timer_start();
            for (i = 0; i < NBALLS; ++i)
                stamp_ball_fast(CELL_X(PIECE_X + i, row), CELL_Y(row), PIECE_COLS[i], (unsigned char)(row & 1));
            store_result(4, timer_read());
            VIC[0x20] = 0;

            row = START_ROW;
            off = 0;
        }
    }
}
