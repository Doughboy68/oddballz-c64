/* hud.h - Level, Skill, Balls and Score in the four corner wedges.
 *
 * Layout follows the original Windows playfield (PLAY256.BMP): the hexagon
 * leaves four triangular gaps and the readouts live in them.
 *
 *   top-left     LEVEL     top-right    SKILL
 *   bottom-left  BALLS     bottom-right SCORE
 *
 * BALLS is ball_count -- how many pieces have been in play.
 *
 * The bottom wedges are smaller than the top ones and that is structural: the
 * hexagon's widest row is 11, above centre, so the lower half is still broad at
 * row 14. The bottom panels start at character row 20 to clear it.
 *
 * Text is drawn as bitmap graphics, not characters. In multicolour bitmap a
 * character cell is 8 physical px = 4 LOGICAL px wide, so an ordinary 8x8 font
 * would come out four pixels across and be unreadable. The font here is 3x5
 * logical px in a 4x6 cell, which is chunky but legible -- and a raster split
 * to get real text is not available, because the corners sit inside the board's
 * vertical span.
 *
 * These cells hold no balls, so the parity slot scheme in stamp.s does not
 * apply and plot()'s ordinary allocation is fine. Redrawing is per-panel and
 * only when a value actually changes: at ~300 cycles a pixel, repainting all
 * four every landing would cost about four frames.
 */

#ifndef HUD_H
#define HUD_H

#include "../spikes/common/board.h"

/* Labels picked out in their own colour, values in white -- the original does
 * the same. Anything but cyan: cyan labels vanished into the cyan wallpaper
 * behind them. */
#ifndef HUD_COL
#define HUD_COL       1     /* white  */
#endif
#ifndef HUD_LABEL_COL
#define HUD_LABEL_COL 3     /* cyan */
#endif

/* 3x5 glyphs, five rows of three bits. Only the letters the four labels need. */
#define G_0 0
#define G_A 10
#define G_B 11
#define G_C 12
#define G_E 13
#define G_I 14
#define G_K 15
#define G_L 16
#define G_O 17
#define G_R 18
#define G_S 19
#define G_V 20
#define G_D 21
#define G_Z 22
#define G_G 23
#define G_M 24
#define G_P 25
#define G_SP 26     /* blank */
#define G_N 27
#define G_T 28
#define G_U 29
#define G_Y 30
#define G_W 31
#define G_H 32
#define G_J 33
#define G_F 34
#define G_X 35
#define G_COUNT 36

static const unsigned char FONT[G_COUNT * 5] = {
    7,5,5,5,7,   2,6,2,2,7,   7,1,7,4,7,   7,1,7,1,7,   5,5,7,1,1,
    7,4,7,1,7,   7,4,7,5,7,   7,1,1,1,1,   7,5,7,5,7,   7,5,7,1,7,
    7,5,7,5,5,   6,5,6,5,6,   7,4,4,4,7,   7,4,6,4,7,   7,2,2,2,7,
    5,5,6,5,5,   4,4,4,4,7,   7,5,5,5,7,   6,5,6,5,5,   7,4,7,1,7,
    5,5,5,5,2,   6,5,5,5,6,   7,1,2,4,7,
    7,4,5,5,7,   5,7,7,5,5,   7,5,7,4,4,   0,0,0,0,0,
    5,7,7,7,5,   7,2,2,2,2,   5,5,5,5,7,   5,5,7,2,2,
    5,5,7,7,5,   5,5,7,5,5,
    1,1,1,5,7,   7,4,6,4,4,   5,5,2,5,5
};

/* Scaled draw, for the title and game-over screens. Each 3x5 glyph pixel
 * becomes an sc x sc block, so one font serves both the HUD and a title.
 *
 * --- why this is not a loop over plot() -----------------------------------
 *
 * It used to be, and the title screen took 4,561,385 cycles -- 4.6 seconds --
 * measured with TEST_TIMING=1. ODDBALLZ alone was 1,848,973 cycles for 693
 * plotted pixels: about 2,670 cycles a pixel. stamp.s draws a ball at roughly 60
 * cycles a pixel, so the text was some 45x off the assembly sitting next to it.
 *
 * plot() is a general per-pixel routine and pays for it every time: two 16-bit
 * multiplies for the addresses, arbitration for one of the cell's three colour
 * slots, then a read-modify-write of one bitmap byte -- all reached through
 * cc65's stack calling convention. At triple size every font pixel became NINE
 * of those calls.
 *
 * Two things are wrong with that, and both are structural rather than a matter
 * of shaving cycles:
 *
 *   A character cell holds one colour per slot, so the slot only has to be
 *   decided once per CELL, not once per pixel. plot() re-derived the same answer
 *   for every pixel in the cell.
 *
 *   Four logical pixels share one bitmap byte. Writing them one at a time means
 *   reading and writing that byte four times over, with the addresses
 *   recomputed each time.
 *
 * So: build the byte mask for a font row once, resolve the slot once per cell,
 * and OR whole bytes in. EXPAND and the ROW_ADDR/ROW_CELL tables already exist
 * in stamp.h for exactly this -- the ball blit had the same problem first.
 */
#ifdef STAMP_H

/* Parameters for the assembly column blit, and the routine itself. Globals
 * rather than arguments for the reason stamp.s gives: cc65 would push them on
 * its software stack, which costs more than the routine saves. Not static --
 * blit.s imports them by their cc65-mangled names. */
unsigned char bl_bc, bl_y, bl_n, bl_rep, bl_col, bl_ov;
const unsigned char *bl_src;
void blit_col(void);

/* And the same for the whole-string entry point. */
unsigned char bt_n, bt_lx, bt_y, bt_col, bt_lead, bt_lcol, bt_ov;
const unsigned char *bt_src;
void blit_text1(void);

/* A 3-wide glyph at scale sc spans at most this many byte columns. */
#define GLYPH_BC 8


/* overlay != 0 forces pattern 11 and colour RAM -- the game-over path, which has
 * to write over a live board without disturbing the balls' own slots. */
/* Masks for the whole glyph, built once: mk[r * GLYPH_BC + k] is the 4-pixel
 * mask that font row r contributes to byte column k. File scope rather than
 * local because cc65 indexes a static array far more cheaply than one on its
 * software stack. */
static unsigned char mk[5 * GLYPH_BC];

/* Shift-by-variable replacements: the font's three columns, and the four pixel
 * positions within a byte. */
static const unsigned char BIT3[3] = { 4, 2, 1 };
static const unsigned char BIT4[4] = { 8, 4, 2, 1 };

/* Triple-size masks, keyed on the font row's three bits rather than on the
 * glyph. A row is only ever one of eight patterns, so eight of them times four
 * alignments times three byte columns covers every glyph in 128 bytes -- against
 * 2,880 if it were keyed on the glyph the way SM0/SM1 below are. (Which is how
 * those could have been built too; they work, so they stay.) */
static unsigned char S3[8 * 4 * 4];

static void glyph_bytes(int lx, int ly, unsigned char g, unsigned char col,
                        unsigned char sc, unsigned char overlay)
{
    unsigned char r, c, i, k, nbc;
    int bc0 = lx >> 2;

    nbc = (unsigned char)(((lx + 3 * (int)sc - 1) >> 2) - bc0 + 1);
    if (nbc > GLYPH_BC) nbc = GLYPH_BC;

    /* Pass 1: the byte masks. Bit b is the pixel at (x & 3) == 3 - b, matching
     * plot()'s shift and EXPAND's packing. Done once per glyph, not once per
     * scanline -- the mask does not change down a scaled row.
     *
     * Both shifts here are by a VARIABLE amount, which the 6502 cannot do and
     * cc65 turns into a loop or a helper call. BIT3 and BIT4 replace them with
     * an indexed load. `px` walks rather than being recomputed, which drops a
     * multiply per pixel as well. */
    if (sc == 3) {
        /* Straight out of the table: five rows copied, no pixel walk at all.
         * ODDBALLZ and PAUSED are the only triple-size text in the game, but
         * between them they were half the cost of drawing the title. */
        const unsigned char *fp = FONT + (unsigned int)g * 5;
        unsigned char a = (unsigned char)(lx & 3);
        for (r = 0; r < 5; ++r) {
            const unsigned char *s = S3 + ((unsigned int)fp[r] * 4 + a) * 4;
            for (k = 0; k < nbc; ++k) mk[k * 5 + r] = s[k];
        }
    } else {
        const unsigned char *fp = FONT + (unsigned int)g * 5;
        unsigned char lx8 = (unsigned char)lx, bc8 = (unsigned char)bc0;

        /* All of this fits in a byte -- lx is 0..159 and a tripled glyph is nine
         * wide -- and cc65 costs several times more for the same arithmetic on
         * an int. Clear only the columns in use, not all 40. */
        for (k = 0; k < nbc * 5; ++k) mk[k] = 0;
        for (r = 0; r < 5; ++r) {
            unsigned char bits = fp[r], px = lx8;
            if (!bits) continue;
            for (c = 0; c < 3; ++c) {
                if (!(bits & BIT3[c])) { px = (unsigned char)(px + sc); continue; }
                for (i = 0; i < sc; ++i) {
                    unsigned char b = (unsigned char)((px >> 2) - bc8);
                    if (b < nbc) mk[b * 5 + r] |= BIT4[px & 3];
                    ++px;
                }
            }
        }
    }

    /* Pass 2: hand each byte column's five masks straight to the assembly, which
     * repeats each one down `sc` scanlines itself. mk is laid out column-major
     * for exactly this -- a column's masks are contiguous, so nothing has to be
     * copied into a buffer first. */
    if (ly < 0 || ly + 5 * (int)sc > 200) return;
    bl_y   = (unsigned char)ly;
    bl_n   = 5;
    bl_rep = sc;
    bl_col = col;
    bl_ov  = overlay;

    for (k = 0; k < nbc; ++k) {
        int bci = bc0 + (int)k;
        const unsigned char *m = mk + (unsigned int)k * 5;
        if (bci < 0 || bci >= 40) continue;
        if (!(m[0] | m[1] | m[2] | m[3] | m[4])) continue;
        bl_bc  = (unsigned char)bci;
        bl_src = m;
        blit_col();
    }
}

/* --- the size-1 fast path ----------------------------------------------- *
 *
 * Almost every glyph on screen is unscaled: 93 of the title's 104, and both
 * option lines on the pause screen. At scale 1 the mask pass above costs more
 * than the drawing does -- clearing 40 bytes and walking three font columns to
 * work out something that depends only on the glyph and where it lands in a
 * byte. There are only 36 glyphs and 4 alignments, so all of it can be worked
 * out once at startup.
 *
 * Split into one array per byte column rather than interleaved, so a glyph's
 * five masks for a column are CONTIGUOUS and the assembly can be handed a
 * pointer straight into the table. Interleaved would have meant copying five
 * bytes into a buffer per column, per glyph, which is most of the C work the
 * assembly exists to remove.
 *
 * 36 glyphs x 4 alignments x 5 rows x 2 arrays = 1,440 bytes. Not static:
 * blit.s indexes them directly. */
unsigned char SM0[G_COUNT * 4 * 5];
unsigned char SM1[G_COUNT * 4 * 5];

/* Whether a (glyph, alignment) pair puts anything in that byte column at all.
 * Two cases make this worth 144 bytes each: a blank fits entirely in the left
 * column whenever the alignment is 0 or 1, so the right column is empty for
 * most of the screen; and G_SP is empty in both. Without this the assembly sets
 * up and walks a five-scanline run to write nothing. */
unsigned char SMANY0[G_COUNT * 4];
unsigned char SMANY1[G_COUNT * 4];


static void font_init(void)
{
    unsigned char g, a, r, c;
    unsigned int i = 0, j = 0;
    for (g = 0; g < G_COUNT; ++g)
        for (a = 0; a < 4; ++a) {
            unsigned char any0 = 0, any1 = 0;
            for (r = 0; r < 5; ++r) {
                unsigned char bits = FONT[g * 5 + r], m0 = 0, m1 = 0;
                for (c = 0; c < 3; ++c) {
                    unsigned char px;
                    if (!(bits & BIT3[c])) continue;
                    px = (unsigned char)(a + c);
                    if (px < 4) m0 |= BIT4[px];
                    else        m1 |= BIT4[px - 4];
                }
                SM0[i] = m0;  any0 |= m0;
                SM1[i] = m1;  any1 |= m1;
                ++i;
            }
            SMANY0[j] = any0;
            SMANY1[j] = any1;
            ++j;
        }

    /* Triple size. Nine pixels starting at alignment a reach px 3+8 = 11 at
     * worst, so three byte columns is always enough. */
    for (g = 0; g < 8; ++g)                     /* g is the row pattern here */
        for (a = 0; a < 4; ++a) {
            unsigned char m[4], px = a, k;
            for (k = 0; k < 4; ++k) m[k] = 0;
            for (c = 0; c < 3; ++c)
                for (r = 0; r < 3; ++r) {
                    if (g & BIT3[c]) m[px >> 2] |= BIT4[px & 3];
                    ++px;
                }
            for (k = 0; k < 4; ++k) S3[((unsigned int)g * 4 + a) * 4 + k] = m[k];
        }
}

/* A whole size-1 string, straight into assembly. `lead` glyphs take lead_col,
 * the rest take col -- that is big_text2's highlighted key, handled here rather
 * than by splitting the string, so a two-colour line costs no more than a
 * one-colour one. */
static void text1(int lx, int ly, const unsigned char *g, unsigned char n,
                  unsigned char col, unsigned char lead, unsigned char lead_col,
                  unsigned char overlay)
{
    if (ly < 0 || ly > 194 || lx < 0 || lx > 159) return;
    bt_src  = g;
    bt_n    = n;
    bt_lx   = (unsigned char)lx;
    bt_y    = (unsigned char)ly;
    bt_col  = col;
    bt_lead = lead;
    bt_lcol = lead_col;
    bt_ov   = overlay;
    blit_text1();
}

static void big_glyph(int lx, int ly, unsigned char g, unsigned char col, unsigned char sc)
{
    if (sc == 1) text1(lx, ly, &g, 1, col, 0, col, 0);
    else         glyph_bytes(lx, ly, g, col, sc, 0);
}

#else   /* no stamp.h: no EXPAND or row tables, so fall back to the slow path */

static void big_glyph(int lx, int ly, unsigned char g, unsigned char col, unsigned char sc)
{
    unsigned char r, c, i, j;
    for (r = 0; r < 5; ++r) {
        unsigned char bits = FONT[g * 5 + r];
        for (c = 0; c < 3; ++c) {
            if (!((bits >> (2 - c)) & 1)) continue;
            for (j = 0; j < sc; ++j)
                for (i = 0; i < sc; ++i)
                    plot((unsigned int)(lx + (int)c * sc + i),
                         (unsigned int)(ly + (int)r * sc + j), col);
        }
    }
}

#endif

/* Centred horizontally. Advance is 4 * sc, one blank column between glyphs. */
static void big_text(int ly, const unsigned char *g, unsigned char n,
                     unsigned char col, unsigned char sc)
{
    unsigned char i;
    int lx = (160 - (int)n * 4 * (int)sc) / 2;
    if (sc == 1) { text1(lx, ly, g, n, col, 0, col, 0); return; }
    for (i = 0; i < n; ++i) big_glyph(lx + (int)i * 4 * (int)sc, ly, g[i], col, sc);
}

/* As big_text, but the first `nlead` glyphs take a second colour. Centring is
 * done on the whole string, so the line sits where big_text would have put it --
 * the highlight changes the colour of the key you press, not the layout. */
static void big_text2(int ly, const unsigned char *g, unsigned char n,
                      unsigned char nlead, unsigned char lead_col,
                      unsigned char col, unsigned char sc)
{
    unsigned char i;
    int lx = (160 - (int)n * 4 * (int)sc) / 2;
    if (sc == 1) { text1(lx, ly, g, n, col, nlead, lead_col, 0); return; }
    for (i = 0; i < n; ++i)
        big_glyph(lx + (int)i * 4 * (int)sc, ly, g[i],
                  (unsigned char)(i < nlead ? lead_col : col), sc);
}

static void bd_plot(unsigned int x, unsigned int y, unsigned char col);  /* below */

static const unsigned char L_LEVEL[5] = { G_L, G_E, G_V, G_E, G_L };
static const unsigned char L_SKILL[5] = { G_S, G_K, G_I, G_L, G_L };
static const unsigned char L_BALLS[5] = { G_B, G_A, G_L, G_L, G_S };
static const unsigned char L_SCORE[5] = { G_S, G_C, G_O, G_R, G_E };

#ifndef USE_BACKDROP
#define USE_BACKDROP 0
#endif

/* Draws in COLOUR RAM -- multicolour bit pattern 11 -- which the board never
 * uses: stamp.s assigns balls the two screen-RAM nibbles by row parity. So a
 * pixel drawn here and a ball can share a character cell with no conflict at
 * all. That is what lets the game-over text overlay a live playfield, and what
 * let the backdrop clip per pixel rather than skipping whole glyphs.
 *
 * Outside the guard below because the overlay needs it even when the wallpaper
 * is compiled out. */
static void bd_plot(unsigned int x, unsigned int y, unsigned char col)
{
    unsigned int  off   = (y >> 3) * 320 + (x >> 2) * 8 + (y & 7);
    unsigned char shift = (unsigned char)((3 - (x & 3)) << 1);
    COLRAM[(y >> 3) * 40 + (x >> 2)] = col;
    BITMAP[off] = (unsigned char)(BITMAP[off] | (3 << shift));
}

/* --- overlay text ------------------------------------------------------ *
 * The same draw, but through bd_plot -- multicolour pattern 11, colour RAM.
 * The board only ever uses the two screen-RAM nibbles (by row parity), so an
 * overlay can be written straight over a live playfield without touching a
 * single ball's colour. Drawing it through plot() instead would allocate slots
 * in board cells and recolour the balls underneath.
 *
 * One colour per character cell, which is fine for single-colour text. */
static void ov_glyph(int lx, int ly, unsigned char g, unsigned char col, unsigned char sc)
{
#ifdef STAMP_H
    if (sc == 1) text1(lx, ly, &g, 1, col, 0, col, 1);  /* colour RAM, pattern 11 */
    else         glyph_bytes(lx, ly, g, col, sc, 1);
#else
    unsigned char r, c, i, j;
    for (r = 0; r < 5; ++r) {
        unsigned char bits = FONT[g * 5 + r];
        for (c = 0; c < 3; ++c) {
            if (!((bits >> (2 - c)) & 1)) continue;
            for (j = 0; j < sc; ++j)
                for (i = 0; i < sc; ++i) {
                    int x = lx + (int)c * sc + i, y = ly + (int)r * sc + j;
                    if (x >= 0 && x < 160 && y >= 0 && y < 200)
                        bd_plot((unsigned int)x, (unsigned int)y, col);
                }
        }
    }
#endif
}

static void ov_text(int ly, const unsigned char *g, unsigned char n,
                    unsigned char col, unsigned char sc)
{
    unsigned char i;
    int lx = (160 - (int)n * 4 * (int)sc) / 2;
#ifdef STAMP_H
    if (sc == 1) { text1(lx, ly, g, n, col, 0, col, 1); return; }
#endif
    for (i = 0; i < n; ++i) ov_glyph(lx + (int)i * 4 * (int)sc, ly, g[i], col, sc);
}

static void ov_number(int ly, unsigned int v, unsigned char n,
                      unsigned char col, unsigned char sc)
{
    unsigned char d[6], i = 0, k;
    int lx;
    do { d[i++] = (unsigned char)(v % 10); v /= 10; } while (v && i < n);
    lx = (160 - (int)i * 4 * (int)sc) / 2;
    for (k = 0; k < i; ++k)
        ov_glyph(lx + (int)k * 4 * (int)sc, ly, d[i - 1 - k], col, sc);
}

#if USE_BACKDROP
static const unsigned char L_ODD[8] = { G_O, G_D, G_D, G_B, G_A, G_L, G_L, G_Z };

/* --- backdrop ---------------------------------------------------------- *
 * The original Windows playfield tiles the word "Oddballz" around the hexagon,
 * which is what stops the background reading as a flat field.
 *
 * Board edges in logical px at grid row r, with the board centred:
 *   r < 12   left = 56 - 4r      right = 4r + 104
 *   r >= 12  left = 4r - 32      right = 192 - 4r
 */
static int board_left(unsigned char r)  { return r < 12 ? 56 - 4 * (int)r : 4 * (int)r - 32; }
static int board_right(unsigned char r) { return r < 12 ? 4 * (int)r + 104 : 192 - 4 * (int)r; }

/* A backdrop pixel shows only outside the hexagon, with a clear margin so the
 * wallpaper does not crowd the board edge. Butted right up against it, the
 * pattern competed with the playfield instead of sitting behind it. */
#define BD_MARGIN 7

static unsigned char bd_visible(int x, int y)
{
    unsigned char r;
    if (x < 0 || x > 159 || y < 0 || y > 199) return 0;
    r = (unsigned char)(y / PITCH_Y);
    if (r > MAX_Y) return 1;
    return (unsigned char)(x < board_left(r) - BD_MARGIN ||
                           x > board_right(r) + BD_MARGIN);
}

static void bd_glyph(int lx, int ly, unsigned char g, unsigned char col)
{
    unsigned char r, c;
    for (r = 0; r < 5; ++r) {
        unsigned char bits = FONT[g * 5 + r];
        for (c = 0; c < 3; ++c)
            if ((bits >> (2 - c)) & 1) {
                int x = lx + (int)c, y = ly + (int)r;
                if (bd_visible(x, y)) bd_plot((unsigned int)x, (unsigned int)y, col);
            }
    }
}

#endif /* USE_BACKDROP */

/* Panel bounds in CHARACTER cells (40 across, 25 down), chosen to clear the
 * hexagon at every row they span. Sized to the CONTENT -- a label row and a number row -- not to the wedge.
 * The panels are cleared to background, so making them the full height of the
 * available space punched large empty blocks out of the backdrop. Two character
 * rows and six columns is all the text needs.
 *
 *   label  char row 0 / 20      number  char row 1 / 21
 *   5 glyphs at 4 logical px = 20 px = 5 cells, plus one for the left inset */
#define TL_C0  0
#define TL_C1  5
#define TR_C0 31
#define TR_C1 37
#define TOP_R0 0
#define TOP_R1 1

/* BR starts at 33, not 32. Character rows 20-21 cover grid row 16, whose right
 * edge is logical x 128 -- exactly cell 32. A panel starting there had
 * hud_clear() zeroing the screen and colour RAM of a cell the BOARD uses, so
 * balls on the lower-right edge lost their colour nibble and rendered black,
 * vanishing the next time the score changed. */
#define BL_C0  0
#define BL_C1  5
#define BR_C0 33
#define BR_C1 37
#define BOT_R0 20
#define BOT_R1 21

static void hud_clear(unsigned char c0, unsigned char c1,
                      unsigned char r0, unsigned char r1)
{
    unsigned char cc, cr, k;
    for (cr = r0; cr <= r1; ++cr) {
        unsigned int base = (unsigned int)cr * 320;
        unsigned int cell = (unsigned int)cr * 40;
        for (cc = c0; cc <= c1; ++cc) {
            unsigned char *p = BITMAP + base + ((unsigned int)cc << 3);
            for (k = 0; k < 8; ++k) p[k] = 0;
            SCREEN[cell + cc] = 0;
            COLRAM[cell + cc] = 0;
        }
    }
}

static void hud_glyph(unsigned int lx, unsigned int ly, unsigned char g, unsigned char col)
{
    unsigned char r;
    for (r = 0; r < 5; ++r) {
        unsigned char bits = FONT[g * 5 + r];
        if (bits & 4) plot(lx,     ly + r, col);
        if (bits & 2) plot(lx + 1, ly + r, col);
        if (bits & 1) plot(lx + 2, ly + r, col);
    }
}

static void hud_label(unsigned int lx, unsigned int ly, const unsigned char *g)
{
    unsigned char i;
    for (i = 0; i < 5; ++i) hud_glyph(lx + i * 4, ly, g[i], HUD_LABEL_COL);
}

/* Right-aligned, leading zeros left blank. */
static void hud_number(unsigned int lx, unsigned int ly, unsigned int v, unsigned char n)
{
    unsigned char d[6], i = 0;
    do { d[i++] = (unsigned char)(v % 10); v /= 10; } while (v && i < n);
    while (i--) hud_glyph(lx + (unsigned int)(n - 1 - i) * 4, ly, d[i], HUD_COL);
}

#if USE_BACKDROP
/* Tile "ODDBALLZ" around the hexagon, one glyph at a time so partial words trim
 * against the board edge exactly as they do in the original. */
static void draw_backdrop(unsigned char col)
{
    int ty, tx, ox;
    unsigned char i, row = 0;

    /* Regular columns, only a half-word stagger on alternate rows. A running
     * diagonal offset made the trimmed fragments near the hexagon land in
     * different places on every row, which reads as scattered letters rather
     * than wallpaper. */
    for (ty = 2; ty < 199; ty += 10, ++row) {
        ox = (row & 1) ? 18 : 0;
        for (tx = ox - 36; tx < 160; tx += 36) {
            for (i = 0; i < 8; ++i)
                bd_glyph(tx + (int)i * 4, ty, L_ODD[i], col);
        }
    }
}
#endif /* USE_BACKDROP */

/* Labels never change, so they are painted once. */
static void hud_init(void)
{
    hud_clear(TL_C0, TL_C1, TOP_R0, TOP_R1);
    hud_clear(TR_C0, TR_C1, TOP_R0, TOP_R1);
    hud_clear(BL_C0, BL_C1, BOT_R0, BOT_R1);
    hud_clear(BR_C0, BR_C1, BOT_R0, BOT_R1);

    /* Labels and numbers must sit in DIFFERENT character rows: clearing a
     * number wipes whole cells, and a label starting at y=4 spills its bottom
     * row into the next cell row down, where the clear then eats it. Glyphs are
     * 5 tall, so a label at y=1 stays inside one 8-row cell. */
    hud_label(2,   1,   L_LEVEL);       /* char row 0  */
    hud_label(126, 1,   L_SKILL);
    hud_label(2,   162, L_BALLS);       /* char row 20 */
    hud_label(132, 162, L_SCORE);
}

/* Each value repaints only when it changes. Balls moves every piece; score and
 * level only on a match, so most landings repaint one number. */
static unsigned int hud_lvl = 0xFFFF, hud_skl = 0xFFFF;
static unsigned int hud_bal = 0xFFFF, hud_scr = 0xFFFF;

static void hud_update(unsigned int level_v, unsigned int skill_v,
                       unsigned int balls_v, unsigned int score_v)
{
    if (level_v != hud_lvl) {
        hud_lvl = level_v;
        hud_clear(TL_C0, TL_C1, 1, 1);
        hud_number(2, 10, level_v, 3);
    }
    if (skill_v != hud_skl) {
        hud_skl = skill_v;
        hud_clear(TR_C0, TR_C1, 1, 1);
        hud_number(126, 10, skill_v, 4);
    }
    if (balls_v != hud_bal) {
        hud_bal = balls_v;
        hud_clear(BL_C0, BL_C1, 21, 21);        /* char row 21 = y 168..175 */
        hud_number(2, 171, balls_v, 5);
    }
    if (score_v != hud_scr) {
        hud_scr = score_v;
        hud_clear(BR_C0, BR_C1, 21, 21);
        hud_number(132, 171, score_v, 5);
    }
}

static void hud_reset(void)
{
    hud_lvl = hud_skl = hud_bal = hud_scr = 0xFFFF;
}

#endif /* HUD_H */
