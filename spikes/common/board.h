/* board.h - shared board geometry and multicolour bitmap plotting.
 *
 * Header-only on purpose: cc65 links fine either way, and keeping it in one
 * file means the spikes cannot drift apart on geometry.
 *
 * Defaults are variant E, the chosen layout (see README):
 *   ball 8 logical px across by 10 tall, row pitch 10, all 20 rows,
 *   board 136 x 200 of the 160 x 200 multicolour screen. The 24 logical px
 *   left over on the right are the HUD strip.
 *
 * Geometry mirrors oddballz-app.js:
 *   isInBoard(x,y) = x in [4,20] && y in [0,19] &&
 *                    ((y < 12 && x < y + 10) || (y >= 12 && x > y - 8))
 *   screen x is sheared half a cell per row, so odd rows sit half a ball left.
 */

#ifndef BOARD_H
#define BOARD_H

#include <string.h>

#ifndef PITCH_X
#define PITCH_X   8
#endif
#ifndef PITCH_Y
#define PITCH_Y   10
#endif
#ifndef ROW_FIRST
#define ROW_FIRST 0
#endif

#define BALL_W  PITCH_X
#ifndef BALL_H
#define BALL_H  PITCH_Y   /* must stay <= PITCH_Y or rows overlap and clash */
#endif

#define MIN_X 4
#define MAX_X 20
#define MAX_Y 19

/* First row that gets DRAWN. See the note by on_field() below -- rows 0-2 are
 * the entry area and are masked. Declared up here because ORIGIN_Y centres the
 * drawn rows rather than all of them. */
#define FIELD_TOP 3
#define SPLIT 12
#define UPPER 10
#define LOWER 8

/* VIC bank 2 ($8000-$BFFF).
 *
 * This has moved twice, both times because BSS grew into the VIC's memory and
 * the two silently overwrote each other -- black screens, corrupted balls and
 * crashes, with a linker that reported nothing wrong. It ran through a bitmap
 * at $2000 in bank 0, then through screens at $4400 and $5C00 in bank 1, where
 * the last of them left only 93 bytes of headroom.
 *
 * Bank 2 ends that: cc65's c64 target banks BASIC out (__HIMEM__ is $D000), so
 * $A000-$BFFF is ordinary RAM, and the whole of $0801-$83FF -- about 33 KB --
 * is free for code and data. The VIC sees the character ROM at $9000-$9FFF in
 * this bank, which both structures avoid.
 *
 * tools/build-game.ps1 fails the build if BSS reaches the screen. */
#define BITMAP  ((unsigned char *)0xA000)   /* bank + $2000 */
#define SCREEN  ((unsigned char *)0x8400)   /* bank + $0400 */
#define COLRAM  ((unsigned char *)0xD800)   /* never banked */
#define VIC     ((unsigned char *)0xD000)
#define CIA2P   ((unsigned char *)0xDD00)

/* Row 11 starts at MIN_X and the shear carries it 5.5 cells left of column
 * zero, so shift the whole board right by that much plus half a ball -- then
 * centre it, which splits the leftover 24 logical px evenly and makes the four
 * corner wedges symmetric. The original Windows game puts Level, Skill and
 * Score in exactly those wedges (PLAY256.BMP).
 *
 * The centring term must stay a multiple of 4: the blit relies on every ball
 * centre landing on a 4 px boundary so a ball covers exactly two byte-aligned
 * columns. 17 balls at pitch 8 is 136 wide, so the margin is 12. */
#define BOARD_W  (17 * PITCH_X)
#define ORIGIN_X ((11 * PITCH_X) / 2 + BALL_W / 2 + (160 - BOARD_W) / 2)
/* Rows 0-2 are masked, so the 20-row board would sit 30 px low in the frame with
 * all the slack above it. Lifting everything by half the hidden height puts the
 * 17 DRAWN rows in the middle, with the same black margin above the hexagon as
 * below -- and the margin above is where a piece comes in from.
 *
 * This makes CELL_Y negative for the masked rows, which is fine: nothing draws
 * them, and the sprite position is computed in ints before the VIC's offset is
 * added. The blit would clamp them away in any case.
 *
 * The board's widest drawn row reaches character column 30 and its topmost
 * starts at column 11, so the lift does not bring it into the HUD wedges at
 * columns 0-5 and 31-37. */
#define ORIGIN_Y (BALL_H / 2 - (FIELD_TOP * PITCH_Y) / 2)

/* Background and border. There is ONE background register for the whole screen
 * -- multicolour bit pattern 00 everywhere -- so this tints the gaps between
 * balls, the space outside the hexagon and the HUD panels all at once. It costs
 * no colour slots, which is why it is the one thing that can be recoloured
 * freely. The original Windows playfield is blue (PLAY256.BMP). */
#ifndef BG_COL
#define BG_COL     0      /* black */
#endif
#ifndef BORDER_COL
#define BORDER_COL 0
#endif

/* Six ball colours, legible on black and distinct from each other. The engine
 * never uses more than six (levAttr, oddballz-app.js:118). */
static const unsigned char BALL_COLS[6] = { 2, 7, 5, 4, 3, 1 };

static unsigned char in_board(signed char x, signed char y)
{
    if (x < MIN_X || x > MAX_X || y < 0 || y > MAX_Y) return 0;
    if (y < SPLIT) return (unsigned char)(x < y + UPPER);
    return (unsigned char)(x > y - LOWER);
}

/* The playfield as DRAWN, which is not quite the playfield as PLAYED.
 *
 * Rows 0-2 hold 6, 7 and 8 cells and exist only for a piece to enter through.
 * Nothing can ever come to rest in them: every cell they contain lies inside
 * check_game_over()'s danger zone (rows 0-3 of columns 4-12, and those rows
 * reach no further right than column 11), so a ball arriving in one ends the
 * game that instant.
 *
 * Drawing sockets there made the board an irregular hexagon with a six-cell top
 * edge against nine everywhere else. The original is regular -- nine a side --
 * so the three rows are left black, which is also somewhere for the piece to
 * come in from rather than sliding down unusable playfield.
 *
 * Only the drawing looks at this. in_board() is unchanged, so the board the
 * engine plays on, the danger zone and the spawn point are all exactly as they
 * were. FIELD_TOP itself is up with MIN_X, because ORIGIN_Y needs it. */
#define on_field(x, y) (in_board((x), (y)) && (y) >= FIELD_TOP)

/* Logical screen position of the ball at grid (x, y). */
#define CELL_X(gx, gy) (ORIGIN_X + ((gx) - MIN_X) * PITCH_X - ((gy) * PITCH_X) / 2)
#define CELL_Y(gy)     (ORIGIN_Y + ((gy) - ROW_FIRST) * PITCH_Y)

/* Plot one multicolour pixel, allocating a colour slot in its attribute cell.
 * Three ball colours per cell (screen hi nibble = 01, lo nibble = 10, colour
 * RAM = 11) over a shared background. Slots start at 0 and 0 is never a ball
 * colour, so zero means free. A fourth colour has nowhere to go and falls back
 * to slot 1 -- real VIC-II clash, left visible rather than hidden. */
static void plot(unsigned int x, unsigned int y, unsigned char col)
{
    unsigned int  cell = (y >> 3) * 40 + (x >> 2);
    unsigned int  off  = (y >> 3) * 320 + (x >> 2) * 8 + (y & 7);
    unsigned char sc   = SCREEN[cell];
    unsigned char pat, shift;

    if      ((sc >> 4)    == col) pat = 1;
    else if ((sc & 0x0F)  == col) pat = 2;
    else if (COLRAM[cell] == col) pat = 3;
    else if ((sc >> 4)    == 0)   { SCREEN[cell] = (unsigned char)(sc | (col << 4)); pat = 1; }
    else if ((sc & 0x0F)  == 0)   { SCREEN[cell] = (unsigned char)(sc | col);        pat = 2; }
    else if (COLRAM[cell] == 0)   { COLRAM[cell] = col;                              pat = 3; }
    else                          pat = 1;   /* clash */

    shift = (unsigned char)((3 - (x & 3)) << 1);
    BITMAP[off] = (unsigned char)((BITMAP[off] & ~(3 << shift)) | (pat << shift));
}

/* Half-width of the ball ellipse at vertical offset dy from centre.
 * Shared with the sprite builder so falling balls match landed ones exactly. */
static int ball_halfwidth(int dy)
{
    int ry = BALL_H / 2, rx = BALL_W / 2;
    long t = (long)rx * rx * ((long)ry * ry - (long)dy * dy);
    int  w = 0;
    if (t < 0) return -1;
    while ((long)(w + 1) * (w + 1) * ry * ry <= t) ++w;
    return w;
}

/* Per-pixel ball drawing, superseded by stamp.s. Still used by the spikes, so
 * it stays, but the game compiles it out: it is a few hundred bytes of code and
 * code size is what pushes BSS toward the screen. */
#ifndef BOARD_DRAW_HELPERS
#define BOARD_DRAW_HELPERS 1
#endif
#if BOARD_DRAW_HELPERS
static void draw_ball(int cx, int cy, unsigned char col)
{
    int dy, dx, ry = BALL_H / 2;
    for (dy = -ry; dy <= ry; ++dy) {
        int w = ball_halfwidth(dy);
        for (dx = -w; dx <= w; ++dx) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < 160 && py >= 0 && py < 200) plot((unsigned int)px, (unsigned int)py, col);
        }
    }
}

#endif /* BOARD_DRAW_HELPERS -- vic_bitmap_mode is always needed */

static void vic_bitmap_mode(void)
{
    memset(BITMAP, 0, 8000);
    memset(SCREEN, 0, 1000);
    memset(COLRAM, 0, 1000);
    CIA2P[0x02] = (unsigned char)(CIA2P[0x02] | 0x03);          /* port A bits 0-1 out */
    CIA2P[0x00] = (unsigned char)((CIA2P[0x00] & 0xFC) | 0x01); /* %01 = bank 2 */
    VIC[0x20] = BORDER_COL;
    VIC[0x21] = BG_COL;
    VIC[0x18] = 0x18;                                 /* +$0400 screen, +$2000 bitmap */
    VIC[0x11] = (unsigned char)(VIC[0x11] | 0x20);    /* bitmap mode */
    VIC[0x16] = (unsigned char)(VIC[0x16] | 0x10);    /* multicolour */
}

#if BOARD_DRAW_HELPERS
/* Draws the full 238-cell board, cycling colours so neighbours differ -- the
 * worst case for colour clash. */
static void draw_board(void)
{
    signed char x, y;
    unsigned char ci = 0;
    for (y = ROW_FIRST; y <= MAX_Y; ++y) {
        for (x = MIN_X; x <= MAX_X; ++x) {
            if (in_board(x, y)) {
                draw_ball(CELL_X(x, y), CELL_Y(y), BALL_COLS[ci]);
                ci = (unsigned char)((ci + 1) % 6);
            }
        }
        ci = (unsigned char)((ci + 2) % 6);
    }
}

/* Every cell in one dim colour: the empty playfield. Used by the piece spike so
 * a brightly-coloured falling piece is actually visible against it -- on a full
 * board the sprites are perfectly camouflaged. */
static void draw_board_empty(unsigned char col)
{
    signed char x, y;
    for (y = ROW_FIRST; y <= MAX_Y; ++y)
        for (x = MIN_X; x <= MAX_X; ++x)
            if (in_board(x, y)) draw_ball(CELL_X(x, y), CELL_Y(y), col);
}
#endif /* BOARD_DRAW_HELPERS */

#endif /* BOARD_H */
