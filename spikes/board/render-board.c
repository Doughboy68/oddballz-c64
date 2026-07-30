/* render-board.c - static board render, for choosing ball size by eye.
 *
 * Draws the full 238-cell hex board in multicolour bitmap mode, one of six
 * ball colours per cell, and then stops. Nothing animates: the only question
 * this answers is what the board LOOKS like at a given ball size, and how bad
 * VIC-II colour clash gets when six colours meet on a hex grid.
 *
 * Built with cc65 purely because it is quick to author. Speed is irrelevant for
 * a still frame -- whether the real renderer needs assembly is a separate
 * question the animation spike will answer.
 *
 * Geometry mirrors oddballz-app.js exactly (see tools/board-geometry.ps1):
 *   isInBoard(x,y) = x in [4,20] && y in [0,19] &&
 *                    ((y < 12 && x < y + 10) || (y >= 12 && x > y - 8))
 *   screen x is sheared by half a cell per row, so odd rows sit half a ball
 *   left of even rows.
 *
 * Tunables, overridden per variant with -D on the cl65 command line:
 *   PITCH_X   logical px between ball centres across  (MC px are 2 phys wide)
 *   PITCH_Y   px between rows
 *   ROW_FIRST first board row to draw (4 skips the spawn rows)
 *
 * NOTE on aspect: a multicolour logical pixel is 2 physical px wide, and the
 * C64's 320x200 on a 4:3 screen already makes physical px 0.833 wide per unit
 * tall. So an MC logical px is ~1.667 wide per unit tall. A ball PITCH_X across
 * is therefore 1.667*PITCH_X units wide visually, and true hex row spacing is
 * 0.866 of that -- about 1.443*PITCH_X. That is what forces PITCH_X down to 6
 * if all 20 rows must fit in 200 px.
 */

#include <string.h>

#ifndef PITCH_X
#define PITCH_X   6
#endif
#ifndef PITCH_Y
#define PITCH_Y   9
#endif
#ifndef ROW_FIRST
#define ROW_FIRST 0
#endif

#define BALL_W  PITCH_X              /* balls touch across */
#ifndef BALL_H
#define BALL_H  ((PITCH_X * 3) / 2)  /* 0.9 * 1.667 * PITCH_X, rounded */
#endif
/* BALL_H > PITCH_Y makes rows overlap, which is what drives colour clash: an
 * 8px-tall attribute cell then meets two ball rows as well as two ball columns,
 * i.e. four colours competing for three slots. Keeping BALL_H <= PITCH_Y is the
 * cheap fix. */

#define MIN_X 4
#define MAX_X 20
#define MAX_Y 19
#define SPLIT 12
#define UPPER 10
#define LOWER 8

#define BITMAP  ((unsigned char *)0x2000)
#define SCREEN  ((unsigned char *)0x0400)
#define COLRAM  ((unsigned char *)0xD800)
#define VICBASE ((unsigned char *)0xD000)

/* Six ball colours, all legible against black and distinct from each other. */
static const unsigned char BALL_COLS[6] = { 2, 7, 5, 4, 3, 1 };
                            /* red, yellow, green, purple, cyan, white */

static unsigned char in_board(signed char x, signed char y)
{
    if (x < MIN_X || x > MAX_X || y < 0 || y > MAX_Y) return 0;
    if (y < SPLIT) return (unsigned char)(x < y + UPPER);
    return (unsigned char)(x > y - LOWER);
}

/* Plot one multicolour pixel, allocating a colour slot in the 4x8 attribute
 * cell that covers it.
 *
 * Each cell offers three ball colours -- screen hi nibble (bit pattern 01),
 * screen lo nibble (10) and colour RAM (11) -- over a shared background (00).
 * Slots start at 0 (black) and 0 is never a ball colour, so zero means free.
 * When a fourth colour wants into the same cell there is nowhere to put it and
 * we fall back to slot 1: that is genuine VIC-II colour clash, left visible on
 * purpose so the screenshot shows how often it really happens.
 */
static void plot(unsigned int x, unsigned int y, unsigned char col)
{
    unsigned int  cell = (y >> 3) * 40 + (x >> 2);
    unsigned int  off  = (y >> 3) * 320 + (x >> 2) * 8 + (y & 7);
    unsigned char sc   = SCREEN[cell];
    unsigned char pat;
    unsigned char shift;

    if      ((sc >> 4)      == col) pat = 1;
    else if ((sc & 0x0F)    == col) pat = 2;
    else if (COLRAM[cell]   == col) pat = 3;
    else if ((sc >> 4)      == 0)   { SCREEN[cell] = (unsigned char)(sc | (col << 4)); pat = 1; }
    else if ((sc & 0x0F)    == 0)   { SCREEN[cell] = (unsigned char)(sc | col);        pat = 2; }
    else if (COLRAM[cell]   == 0)   { COLRAM[cell] = col;                              pat = 3; }
    else                            pat = 1;   /* clash */

    shift = (unsigned char)((3 - (x & 3)) << 1);
    BITMAP[off] = (unsigned char)((BITMAP[off] & ~(3 << shift)) | (pat << shift));
}

/* Filled ellipse, BALL_W across by BALL_H tall, centred on (cx, cy). */
static void ball(int cx, int cy, unsigned char col)
{
    int dy, dx;
    int ry = BALL_H / 2, rx = BALL_W / 2;

    for (dy = -ry; dy <= ry; ++dy) {
        /* half-width at this scanline, from (dx/rx)^2 + (dy/ry)^2 <= 1 */
        long t = (long)rx * rx * ((long)ry * ry - (long)dy * dy);
        int  w = 0;
        while ((long)(w + 1) * (w + 1) * ry * ry <= t) ++w;

        for (dx = -w; dx <= w; ++dx) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < 160 && py >= 0 && py < 200) plot((unsigned int)px, (unsigned int)py, col);
        }
    }
}

int main(void)
{
    signed char x, y;
    unsigned char ci = 0;
    int originX, originY;

    /* Multicolour bitmap: bitmap at $2000, video matrix at $0400. */
    memset(BITMAP, 0, 8000);
    memset(SCREEN, 0, 1000);
    memset(COLRAM, 0, 1000);
    VICBASE[0x20] = 0;                       /* border     black */
    VICBASE[0x21] = 0;                       /* background black */
    VICBASE[0x18] = 0x18;                    /* screen $0400, bitmap $2000 */
    VICBASE[0x11] = (unsigned char)(VICBASE[0x11] | 0x20);   /* bitmap mode */
    VICBASE[0x16] = (unsigned char)(VICBASE[0x16] | 0x10);   /* multicolour */

    /* Columns are measured from MIN_X, so the shear (-y/2 cells) sends row 11 --
     * the widest row, starting at x = MIN_X -- 5.5 cells left of column zero.
     * Shift right by 5.5 cells plus half a ball to bring the hexagon on screen.
     * Getting this wrong clips the entire left flank and the board reads as a
     * trapezoid rather than a hexagon. */
    originX = (11 * PITCH_X) / 2 + BALL_W / 2;
    originY = BALL_H / 2;

    for (y = ROW_FIRST; y <= MAX_Y; ++y) {
        for (x = MIN_X; x <= MAX_X; ++x) {
            if (in_board(x, y)) {
                /* screen x = (x - MIN_X) - y/2, in whole and half cells */
                int cx = originX + (x - MIN_X) * PITCH_X - (y * PITCH_X) / 2;
                int cy = originY + (y - ROW_FIRST) * PITCH_Y;
                ball(cx, cy, BALL_COLS[ci]);
                ci = (unsigned char)((ci + 1) % 6);
            }
        }
        /* nudge the colour cycle each row so neighbours differ, worst case for clash */
        ci = (unsigned char)((ci + 2) % 6);
    }

    /* Completion flag. The screenshot is taken at a fixed cycle count, so a
     * half-drawn board and a wrongly-drawn board look identical. Turning the
     * border white only after the last ball tells the two apart at a glance. */
    VICBASE[0x20] = 1;

    for (;;) { }
}
