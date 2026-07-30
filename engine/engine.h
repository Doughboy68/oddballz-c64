/* engine.h - Oddballz game engine, ported from oddballz-app.js section 2.
 *
 * The JS engine is integer-only by design -- byte grid, additive neighbour
 * offsets, run-length match counting, no multiply, divide or float on any hot
 * path -- which is what made the C64 port viable in the first place. This is a
 * direct transliteration; where it deviates, the deviation is commented.
 *
 * Line references are to D:\Claude\oddballz-hd\oddballz-app.js, which stays the
 * read-only reference spec and behavioural oracle.
 *
 * Board storage differs from the JS on purpose. The JS keeps {inMap, bzMap} per
 * cell and calls checkInMap(); here out-of-board cells hold OUT, so "in board
 * and empty" is a single byte compare and checkInMap disappears from every
 * inner loop. Rows are padded by two above and below because moveInDirection
 * can step two rows off the board (dir 8 and dir 9) before the sentinel stops
 * the walk.
 */

#ifndef ENGINE_H
#define ENGINE_H

#include <string.h>
#include "../spikes/common/board.h"

#define BSTRIDE 32                  /* power of two: index is a shift */
#define BROWS   24
#define OUT     0xFF                /* outside the hexagon */

/* y is offset by 2 so a two-row overstep lands in padding, not before the
 * array. x needs no offset: MIN_X is 4 and the furthest left step is dir 6,
 * x-2, which cannot go below 2. */
#define AT(x, y) (((unsigned int)((y) + 2) << 5) + (unsigned char)(x))

unsigned char BMAP[BSTRIDE * BROWS];
static unsigned char MARK[BSTRIDE * BROWS];

/* moveInDirection, :85. Dir 9 = (x-1,y-2) is vertical on screen, dir 8 its
 * opposite; the shear means "down" is dirs 2 and 5, not a single direction. */
static const signed char DIR_DX[12] = { -1, -1,  0,  0,  1,  1, -2, -1,  1, -1,  1,  2 };
static const signed char DIR_DY[12] = { -1,  0,  1, -1,  0,  1, -1,  1,  2, -2, -1,  1 };

/* Match axes, :890 and :903. Parallel axes need FIVE in a row, perpendicular
 * axes need THREE. PREV is the opposite direction: a run is only counted from
 * its own start, so each run is found exactly once. */
static const unsigned char PAR_DIR[3]   = {  4,  0,  3 };
static const unsigned char PAR_PREV[3]  = {  1,  5,  2 };
static const unsigned char PERP_DIR[3]  = { 11,  9, 10 };
static const unsigned char PERP_PREV[3] = {  6,  8,  7 };

/* ballShapes, :172. Ball 0 sits at the root; these are balls 1-3. A connected
 * four-ball piece can never lie along a perpendicular axis. */
static const signed char SHAPE_DX[7][3] = {
    { -1, -1,  0 }, { -1, -2,  1 }, { -1,  1,  0 }, { -1, -2,  1 },
    { -1, -2,  1 }, { -1, -2, -2 }, { -1,  0,  1 }
};
static const signed char SHAPE_DY[7][3] = {
    {  0, -1, -1 }, {  0,  0,  0 }, {  0,  0, -1 }, {  0, -1,  0 },
    { -1, -1,  0 }, {  0, -1, -2 }, {  0, -1,  1 }
};

/* levAttr, :118. lDelay is dead data in the JS and is not ported. */
static const unsigned char LEV_SHAPES[20] = { 2,2,3,3,4,4,5,5,6,7,7,7,7,7,7,7,7,7,7,7 };
static const unsigned char LEV_COLORS[20] = { 3,3,3,4,4,4,4,5,5,5,5,6,6,6,6,6,6,6,6,6 };

/* --- game state ------------------------------------------------------- */
unsigned int  score;
unsigned int  skill;                /* score per ball x10, :1035 */
unsigned char level;
unsigned int  ball_count;
unsigned char rows_done, row_count, matches_done, match_count;
unsigned int  same_bonus;
unsigned char n_shapes, n_colors;
unsigned char end_game;

/* matcher chooses the game mode, :1007. True = clear by colour match, false =
 * clear complete rows. They are alternatives, not stages. */
unsigned char matcher = 1;

/* Piece: root cell plus four relative offsets and four colours. */
signed char   piece_x, piece_y;
signed char   rel_x[4], rel_y[4];
unsigned char img[4];
unsigned char direction;            /* 2 = down-left, 5 = down-right */

static void build_row_tables(void);     /* defined with the row-clearing code */

static unsigned int color_count[4], color_mod[4];
static unsigned char color_inc[5];
static unsigned char rng_state = 1;

/* xorshift: the JS uses Math.random, which has no C64 equivalent. Any decent
 * byte PRNG preserves the feel; nothing about the rules depends on the source
 * of randomness. */
static unsigned char rnd(unsigned char n)
{
    rng_state ^= (unsigned char)(rng_state << 3);
    rng_state ^= (unsigned char)(rng_state >> 5);
    rng_state ^= (unsigned char)(rng_state << 1);
    if (!rng_state) rng_state = 0x2B;
    return (unsigned char)(rng_state % n);
}

/* initColorInc, :310. colorInc[i+1] = colors^(i+1) + 1.
 *
 * The JS lets colorCount grow without bound because it holds doubles. Here it
 * is kept modulo colors^(i+1), which is the period of
 * (count / (inc-1)) % colors and keeps everything inside 16 bits. */
static void init_color_inc(void)
{
    unsigned char i, j;
    unsigned int  t;
    color_inc[0] = 1;
    for (i = 0; i <= 3; ++i) {
        t = 1;
        for (j = 0; j <= i; ++j) t *= n_colors;
        color_mod[i] = t * n_colors;
        color_inc[i + 1] = (unsigned char)(t + 1);
        color_count[i] = 0;
    }
    color_mod[0] = n_colors;
}

static void engine_init_board(void)
{
    signed char x, y;
    memset(BMAP, OUT, sizeof BMAP);
    for (y = 0; y <= MAX_Y; ++y)
        for (x = MIN_X; x <= MAX_X; ++x)
            if (in_board(x, y)) BMAP[AT(x, y)] = 0;
}

static void engine_init_game(void)
{
    score = 0; skill = 0; level = 1; ball_count = 0;
    rows_done = row_count = matches_done = match_count = 0;
    same_bonus = 0; end_game = 0; direction = 2;
    n_shapes = LEV_SHAPES[0];
    n_colors = LEV_COLORS[0];
    engine_init_board();
    build_row_tables();
    init_color_inc();
}

/* --- piece ------------------------------------------------------------ */

/* newBall, :340. Colours advance on fixed strides so the sequence is
 * deterministic and evenly spread rather than clumping the way plain random
 * would. */
static void piece_spawn(void)
{
    unsigned char cfg = rnd(n_shapes);
    unsigned char i, r;

    if (matcher) {
        img[0] = (unsigned char)((color_count[0] % n_colors) + 1);
        color_count[0] = (color_count[0] + color_inc[0]) % color_mod[0];
        for (i = 1; i <= 3; ++i) {
            img[i] = (unsigned char)(((color_count[i] / (color_inc[i] - 1)) % n_colors) + 1);
            color_count[i] = (color_count[i] + color_inc[i]) % color_mod[i];
        }
    } else {
        /* Row Build: the whole piece is ONE colour, keyed to the shape (:353).
         * Colour carries no meaning in this mode -- rows clear when full -- so
         * a solid piece just makes the shape readable. Missing this branch was
         * what gave Row Build four-coloured pieces. */
        unsigned char same = (unsigned char)(cfg < 6 ? cfg + 1 : rnd(6) + 1);
        for (i = 0; i <= 3; ++i) img[i] = same;
    }

    piece_x = (signed char)(6 + rnd(4));        /* startPos, :262 */
    piece_y = 3;
    rel_x[0] = rel_y[0] = 0;
    for (i = 1; i <= 3; ++i) {
        rel_x[i] = SHAPE_DX[cfg][i - 1];
        rel_y[i] = SHAPE_DY[cfg][i - 1];
    }

    /* transform(rotCW), :539 -- (x,y) becomes (x-y, x). */
    r = rnd(6);
    while (r--) {
        for (i = 0; i <= 3; ++i) {
            signed char nx = (signed char)(rel_x[i] - rel_y[i]);
            rel_y[i] = rel_x[i];
            rel_x[i] = nx;
        }
    }
    ++ball_count;
}

/* True if all four balls fit at this root: in board and empty. */
static unsigned char piece_fits(signed char rx, signed char ry)
{
    unsigned char i;
    for (i = 0; i <= 3; ++i)
        if (BMAP[AT(rx + rel_x[i], ry + rel_y[i])]) return 0;
    return 1;
}

/* Lateral steering. In the JS this is targetFloatX (:414), moved by player
 * input and independent of which diagonal the piece falls along. Without it a
 * piece can only ever reach the columns its spawn point happens to lead to,
 * and the board fills in a narrow wedge. Returns 0 if blocked. */
static unsigned char piece_move(signed char dx)
{
    if (piece_fits((signed char)(piece_x + dx), piece_y)) {
        piece_x = (signed char)(piece_x + dx);
        return 1;
    }
    return 0;
}

/* Rotate if there is room, reverting wholesale if the result does not fit.
 *   clockwise        transform(rotCW),  :539 -- (x,y) becomes (x-y, x)
 *   counter-clockwise transform(rotCCW), :545 -- (x,y) becomes (y, y-x) */
static unsigned char piece_rotate_dir(unsigned char cw)
{
    signed char sx[4], sy[4];
    unsigned char i;
    for (i = 0; i <= 3; ++i) {
        sx[i] = rel_x[i]; sy[i] = rel_y[i];
        if (cw) {
            rel_x[i] = (signed char)(sx[i] - sy[i]);
            rel_y[i] = sx[i];
        } else {
            rel_x[i] = sy[i];
            rel_y[i] = (signed char)(sy[i] - sx[i]);
        }
    }
    if (piece_fits(piece_x, piece_y)) return 1;
    for (i = 0; i <= 3; ++i) { rel_x[i] = sx[i]; rel_y[i] = sy[i]; }
    return 0;
}

#define piece_rotate() piece_rotate_dir(1)

/* rotColors, :753. Rotates the four ball colours within the piece, which is
 * the main tactical control -- you place the shape and choose which colour
 * lands where. A no-op in Row Build mode, where colour does not matter. */
static void piece_cycle_colors(void)
{
    unsigned char save = img[0], i;
    if (!matcher) return;
    for (i = 0; i <= 2; ++i) img[i] = img[i + 1];
    img[3] = save;
}

/* stamp, :768 */
static void piece_stamp(void)
{
    unsigned char i;
    for (i = 0; i <= 3; ++i) {
        unsigned int a = AT(piece_x + rel_x[i], piece_y + rel_y[i]);
        if (BMAP[a] != OUT) BMAP[a] = img[i];
    }
}

/* --- matching --------------------------------------------------------- */

/* rowLength, :860. col is always 1..n_colors, and both 0 and OUT differ from
 * it, so the sentinel terminates the walk with no bounds test. */
static unsigned char run_len(signed char x, signed char y, unsigned char dir, unsigned char col)
{
    unsigned char n = 1;
    for (;;) {
        x = (signed char)(x + DIR_DX[dir]);
        y = (signed char)(y + DIR_DY[dir]);
        if (BMAP[AT(x, y)] != col) return n;
        ++n;
    }
}

/* add2List, :874 -- marks rather than deletes, so a run already found does not
 * break detection of runs that cross it. */
static void mark_run(signed char x, signed char y, unsigned char dir, unsigned char col)
{
    while (BMAP[AT(x, y)] == col) {
        MARK[AT(x, y)] = 1;
        x = (signed char)(x + DIR_DX[dir]);
        y = (signed char)(y + DIR_DY[dir]);
    }
}

/* matchColors, :857. Returns the number of balls cleared. */
static unsigned char match_colors(void)
{
    signed char x, y;
    unsigned char d, col, len, cleared = 0;

    memset(MARK, 0, sizeof MARK);

    for (x = MIN_X; x <= MAX_X; ++x) {
        for (y = 0; y <= MAX_Y; ++y) {
            col = BMAP[AT(x, y)];
            if (col == 0 || col == OUT) continue;

            for (d = 0; d < 3; ++d) {           /* parallel axes: need 5 */
                unsigned char p = PAR_PREV[d];
                if (BMAP[AT(x + DIR_DX[p], y + DIR_DY[p])] == col) continue;
                len = run_len(x, y, PAR_DIR[d], col);
                if (len >= 5) {
                    mark_run(x, y, PAR_DIR[d], col);
                    ++matches_done; ++match_count;
                    same_bonus += len - 3;
                }
            }
            for (d = 0; d < 3; ++d) {           /* perpendicular axes: need 3 */
                unsigned char p = PERP_PREV[d];
                if (BMAP[AT(x + DIR_DX[p], y + DIR_DY[p])] == col) continue;
                len = run_len(x, y, PERP_DIR[d], col);
                if (len >= 3) {
                    mark_run(x, y, PERP_DIR[d], col);
                    ++matches_done; ++match_count;
                    same_bonus += len - 2;
                }
            }
        }
    }

    for (y = 0; y <= MAX_Y; ++y)
        for (x = MIN_X; x <= MAX_X; ++x)
            if (MARK[AT(x, y)]) { BMAP[AT(x, y)] = 0; ++cleared; }

    return cleared;
}

/* --- flip ------------------------------------------------------------- */

/* flipY (:198) and flipX (:206), 5x5 tables indexed [ry+2][rx+2] and flattened.
 * Some entries are (0,0) filler: the reachable relative offsets form a hexagon
 * inside the 5x5 square, and the JS does not guard the corners either. */
static const signed char FLIPY_DX[25] = {
     0,  1,  2,  0,  0,   -1,  0,  1,  2,  0,   -2, -1,  0,  1,  2,
     0, -2, -1,  0,  1,    0,  0, -2, -1,  0
};
static const signed char FLIPY_DY[25] = {
     2,  2,  2,  0,  0,    1,  1,  1,  1,  0,    0,  0,  0,  0,  0,
     0, -1, -1, -1, -1,    0,  0, -2, -2, -2
};
static const signed char FLIPX_DX[25] = {
     0, -1, -2,  0,  0,    1,  0, -1, -2,  0,    2,  1,  0, -1, -2,
     0,  2,  1,  0, -1,    0,  0,  2,  1,  0
};
static const signed char FLIPX_DY[25] = {
    -2, -2, -2,  0,  0,   -1, -1, -1, -1,  0,    0,  0,  0,  0,  0,
     0,  1,  1,  1,  1,    0,  0,  2,  2,  2
};

/* transform(flipX|flipY), :551. Reflect, then try five root shifts and keep the
 * one overlapping the piece's own cells most, ties broken by least movement --
 * a kick system, so a flip against a wall shuffles clear instead of failing.
 * Finally re-normalise so rel[0] is (0,0) again (:609). */
static unsigned char piece_flip(unsigned char use_x)
{
    static const signed char SH_X[5] = { 0, -1, 1, 0, 0 };
    static const signed char SH_Y[5] = { 0, 0, 0, -1, 1 };
    signed char rfx[4], rfy[4], bsx = 0, bsy = 0, s0x, s0y;
    signed char best_ov = -1, best_disp = 0;
    unsigned char i, j, s, found = 0;

    for (i = 0; i <= 3; ++i) {
        signed char mx = (signed char)(rel_x[i] + 2), my = (signed char)(rel_y[i] + 2);
        unsigned char k;
        if (mx < 0 || mx > 4 || my < 0 || my > 4) return 0;
        k = (unsigned char)(my * 5 + mx);
        rfx[i] = use_x ? FLIPX_DX[k] : FLIPY_DX[k];
        rfy[i] = use_x ? FLIPX_DY[k] : FLIPY_DY[k];
    }

    for (s = 0; s < 5; ++s) {
        signed char tx = (signed char)(piece_x + SH_X[s]);
        signed char ty = (signed char)(piece_y + SH_Y[s]);
        signed char ov = 0, disp;
        unsigned char ok = 1;

        for (i = 0; i <= 3; ++i)
            if (BMAP[AT(tx + rfx[i], ty + rfy[i])]) { ok = 0; break; }
        if (!ok) continue;

        for (i = 0; i <= 3; ++i)
            for (j = 0; j <= 3; ++j)
                if (tx + rfx[i] == piece_x + rel_x[j] &&
                    ty + rfy[i] == piece_y + rel_y[j]) { ++ov; break; }

        disp = (signed char)(SH_X[s] * SH_X[s] + SH_Y[s] * SH_Y[s]);
        if (ov > best_ov || (ov == best_ov && disp < best_disp)) {
            best_ov = ov; best_disp = disp; bsx = SH_X[s]; bsy = SH_Y[s]; found = 1;
        }
    }
    if (!found) return 0;

    s0x = rfx[0]; s0y = rfy[0];
    piece_x = (signed char)(piece_x + bsx + s0x);
    piece_y = (signed char)(piece_y + bsy + s0y);
    for (i = 0; i <= 3; ++i) {
        rel_x[i] = (signed char)(rfx[i] - s0x);
        rel_y[i] = (signed char)(rfy[i] - s0y);
    }
    return 1;
}

/* --- row clearing (Row Build mode) ------------------------------------ */

/* buildRowTables, :70. midRow is the leftmost cell of each row from the bottom
 * up to y = 4 -- the spawn rows are deliberately excluded from row clearing --
 * and the bottom row is scanned in both directions. */
static signed char MIDROW_X[MAX_Y + 1], MIDROW_Y[MAX_Y + 1];
static signed char BOT_X[24];
static unsigned char n_midrow, n_bot;

static void build_row_tables(void)
{
    signed char x, y;
    n_midrow = 0;
    for (y = MAX_Y; y >= 4; --y)
        for (x = MIN_X; x <= MAX_X; ++x)
            if (in_board(x, y)) {
                MIDROW_X[n_midrow] = x; MIDROW_Y[n_midrow] = y; ++n_midrow;
                break;
            }
    n_bot = 0;
    for (x = MIN_X; x <= MAX_X; ++x)
        if (in_board(x, MAX_Y)) BOT_X[n_bot++] = x;
}

/* rowFull, :930 */
static unsigned char row_full(signed char x, signed char y, unsigned char dir)
{
    do {
        if (BMAP[AT(x, y)] == 0) return 0;
        x = (signed char)(x + DIR_DX[dir]);
        y = (signed char)(y + DIR_DY[dir]);
    } while (BMAP[AT(x, y)] != OUT);
    return 1;
}

/* deleteRow, :941. Walks the row, and for each cell pulls its column along
 * cdir until it runs out of balls or off the board. */
static void delete_row(signed char x, signed char y, unsigned char rdir, unsigned char cdir)
{
    ++row_count;
    do {
        signed char cx = x, cy = y;
        for (;;) {
            signed char px = cx, py = cy;
            cx = (signed char)(cx + DIR_DX[cdir]);
            cy = (signed char)(cy + DIR_DY[cdir]);
            if (BMAP[AT(cx, cy)] == OUT) { BMAP[AT(px, py)] = 0; break; }
            BMAP[AT(px, py)] = BMAP[AT(cx, cy)];
            if (BMAP[AT(cx, cy)] == 0) break;
        }
        x = (signed char)(x + DIR_DX[rdir]);
        y = (signed char)(y + DIR_DY[rdir]);
    } while (BMAP[AT(x, y)] != OUT);
}

/* Finds the first full row without deleting it, so the renderer can flash it
 * before it goes -- the same feedback a colour match gets. checkRows below
 * finds and deletes in one step, which leaves nothing to flash.
 *
 * The row is described by its first cell and the direction along it, plus the
 * direction its columns collapse when deleted. */
signed char   frow_x, frow_y;
unsigned char frow_rdir, frow_cdir;

static unsigned char find_full_row(void)
{
    unsigned char r, coldir = (unsigned char)(rnd(2) ? 3 : 0);

    for (r = 0; r < n_midrow; ++r)
        if (row_full(MIDROW_X[r], MIDROW_Y[r], 4)) {
            frow_x = MIDROW_X[r]; frow_y = MIDROW_Y[r];
            frow_rdir = 4; frow_cdir = coldir; return 1;
        }
    for (r = 0; r < n_bot; ++r)              /* ltRow */
        if (row_full(BOT_X[r], MAX_Y, 0)) {
            frow_x = BOT_X[r]; frow_y = MAX_Y;
            frow_rdir = 0; frow_cdir = 3; return 1;
        }
    for (r = n_bot; r-- > 0; )               /* rtRow */
        if (row_full(BOT_X[r], MAX_Y, 3)) {
            frow_x = BOT_X[r]; frow_y = MAX_Y;
            frow_rdir = 3; frow_cdir = 0; return 1;
        }
    return 0;
}

/* checkRows, :970 */
static unsigned char check_rows(void)
{
    unsigned char any = 0, r, coldir = (unsigned char)(rnd(2) ? 3 : 0);

    for (r = 0; r < n_midrow; ++r)
        while (row_full(MIDROW_X[r], MIDROW_Y[r], 4)) {
            any = 1; delete_row(MIDROW_X[r], MIDROW_Y[r], 4, coldir);
        }
    for (r = 0; r < n_bot; ++r)              /* ltRow */
        while (row_full(BOT_X[r], MAX_Y, 0)) {
            any = 1; delete_row(BOT_X[r], MAX_Y, 0, 3);
        }
    for (r = n_bot; r-- > 0; )               /* rtRow: the same row reversed */
        while (row_full(BOT_X[r], MAX_Y, 3)) {
            any = 1; delete_row(BOT_X[r], MAX_Y, 3, 0);
        }
    return any;
}

/* --- settling --------------------------------------------------------- */

/* supported, :778. A ball rests on TWO: it is unsupported only when BOTH
 * down-diagonals are in-board and empty, so it never slides into a
 * single-width gap. The OUT sentinel makes the board edge supporting for
 * free, since OUT is non-zero. */
static unsigned char supported(signed char x, signed char y)
{
    return (unsigned char)!(BMAP[AT(x, y + 1)] == 0 && BMAP[AT(x + 1, y + 1)] == 0);
}

/* Moves produced by one settle step, for the renderer to draw. */
#define MAX_MOVES 6
unsigned char n_moves;
signed char move_fx[MAX_MOVES], move_fy[MAX_MOVES];
signed char move_tx[MAX_MOVES], move_ty[MAX_MOVES];
unsigned char move_col[MAX_MOVES];

static unsigned char flip_gate = 1;
static signed char scan_lo, scan_hi;

/* Reset the settle window after clearing cells at rows [ylo, yhi]. Only the row
 * directly above a hole can fall into it, and the window walks upward on its
 * own one row per step. */
static void settle_from(signed char ylo, signed char yhi)
{
    scan_lo = (signed char)(ylo > 0 ? ylo - 1 : 0);
    scan_hi = yhi;
}

/* checkGaps, :790, but advancing every unsupported ball by ONE cell instead of
 * settling each all the way down, which is what makes it animatable. Scanning
 * bottom-to-top while moving downward guarantees no ball moves twice in a pass.
 *
 * When a ball does fall both diagonals are free by definition, so the choice is
 * purely flipGate alternating globally -- that is what produces the zig-zag. */
static unsigned char engine_settle_step(void)
{
    signed char x, y, tx;
    signed char lo = MAX_Y, hi = 0;
    unsigned char bailed = 0;

    n_moves = 0;
    if (scan_hi > MAX_Y - 1) scan_hi = MAX_Y - 1;

    for (y = scan_hi; y >= scan_lo; --y) {
        unsigned char *p = BMAP + AT(MIN_X, y);
        for (x = MIN_X; x <= MAX_X; ++x, ++p) {
            unsigned char col = *p;
            if (!col || col == OUT) continue;
            if (p[BSTRIDE])     continue;       /* dir 2  = (x,   y+1) */
            if (p[BSTRIDE + 1]) continue;       /* dir 5  = (x+1, y+1) */

            tx = flip_gate ? x : (signed char)(x + 1);
            flip_gate ^= 1;

            *p = 0;
            p[BSTRIDE + (tx - x)] = col;

            move_fx[n_moves] = x;  move_fy[n_moves] = y;
            move_tx[n_moves] = tx; move_ty[n_moves] = (signed char)(y + 1);
            move_col[n_moves] = col;

            if (y < lo) lo = y;
            if (y > hi) hi = y;
            if (++n_moves >= MAX_MOVES) { bailed = 1; goto out; }
        }
    }
out:
    if (n_moves) {
        scan_hi = (signed char)(hi + 1);
        if (!bailed) scan_lo = (signed char)(lo > 0 ? lo - 1 : 0);
    }
    return n_moves;
}

/* --- scoring ---------------------------------------------------------- */

/* checkAdvance, :1016. Math.pow(2, cnt) with cnt capped at 10 is a shift. */
static void check_advance(void)
{
    if (row_count) {
        unsigned char c = row_count > 10 ? 10 : row_count;
        score += (unsigned int)(1u << c);
        rows_done = (unsigned char)(rows_done + row_count);
        row_count = 0;
    }
    if (match_count) {
        unsigned char c = match_count > 10 ? 10 : match_count;
        score += (unsigned int)(1u << c);
        match_count = 0;
        score += same_bonus;
        same_bonus = 0;
    }
    /* skill, :1035 -- score per ball, scaled by ten. Widened to 32 bits for the
     * multiply, since score alone reaches five digits. */
    if (ball_count) skill = (unsigned int)(((unsigned long)score * 10) / ball_count);

    if ((matches_done > 11 || rows_done > 5) && level < 50) {
        ++level;
        matches_done = 0; rows_done = 0;
        n_shapes = LEV_SHAPES[level > 20 ? 19 : level - 1];
        n_colors = LEV_COLORS[level > 20 ? 19 : level - 1];
        init_color_inc();
    }
}

/* checkGameOver, :1053. Rows 0-3 in columns 4-12 are the danger zone -- which
 * is exactly why they must stay on screen. */
static unsigned char check_game_over(void)
{
    signed char x, y;
    for (x = 4; x <= 12; ++x)
        for (y = 0; y <= 3; ++y) {
            unsigned char c = BMAP[AT(x, y)];
            if (c && c != OUT) { end_game = 1; return 1; }
        }
    return 0;
}

#endif /* ENGINE_H */
