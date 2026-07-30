/* cascade.c - cascade settling, animated one cell per step.
 *
 * The last real unknown. When a match clears, the balls above it fall to fill
 * the gap, and that is the one place the C64 has to move a lot of board cells
 * at once rather than a handful of sprites.
 *
 * Fall rules are ported exactly from oddballz-app.js:
 *
 *   supported(x,y)  = NOT (down-left empty AND down-right empty)   (:778)
 *   down-left  = dir 2 = (x,   y+1)
 *   down-right = dir 5 = (x+1, y+1)
 *
 * So a ball rests on TWO balls beneath it and only falls when both diagonals
 * are in-board and empty -- it never slides into a single-width gap. When it
 * does fall, both sides are free by definition, so the choice is purely
 * `flipGate`, which alternates globally across every move and is what produces
 * the zig-zag settle.
 *
 * checkGaps() in the JS settles each ball all the way down inside one pass.
 * Here each pass advances every unsupported ball by ONE cell, which is what
 * makes it an animation rather than a snap. Scanning bottom-to-top while moving
 * downward guarantees a ball cannot move twice in the same pass.
 *
 * Measured per step, readable at $0350:
 *   $0350-$0353  cycles for the last step      $0354  balls moved
 *   $0355-$0358  worst step seen               $0359  balls moved that step
 */

#include "../common/board.h"
#include "../common/stamp.h"
#include "../common/timer.h"

#define BMW 32                      /* row stride: a shift, not a multiply */
#define OUT 0xFF                    /* sentinel: cell is outside the hexagon */
static unsigned char BMAP[BMW * 24];

#define AT(x, y) (((unsigned int)(y) << 5) + (unsigned char)(x))

/* Out-of-board cells hold OUT rather than 0, which collapses the whole
 * support test to two array reads:
 *
 *   "in board AND empty"  becomes  BMAP[i] == 0
 *
 * The first version called in_board() twice per occupied cell and cost 195,973
 * cycles to scan a settled board and find nothing to do -- ten frames of pure
 * bookkeeping, dwarfing the blitting it was there to schedule. */
#define FALLS_INTO(i) (BMAP[i] == 0)

static unsigned char flipgate = 1;
static unsigned long worst = 0;

/* Rows worth scanning. A cascade only disturbs cells near the balls that just
 * moved: a ball leaving row y can unsupport the row above it, and itself keeps
 * falling below. So the next pass only needs [min_moved-1, max_moved+1].
 *
 * Without this the scan is the whole board every step -- 75,229 cycles, nearly
 * four frames, most of it spent confirming that untouched rows are untouched.
 * It dwarfed the blitting it existed to schedule. */
static signed char scan_lo, scan_hi;

/* Cap on balls moved per step, so one enormous clear cannot blow the frame
 * budget. The leftovers are simply picked up by the next step; a cascade that
 * staggers slightly reads as natural rather than broken. Each moved ball costs
 * two blits -- erase then stamp -- at 3,657 cycles each. */
#ifndef MAX_MOVES
#define MAX_MOVES 6
#endif

#ifndef STOP_AFTER
#define STOP_AFTER 0
#endif
#if STOP_AFTER
static unsigned int steps = 0;
#endif

static void board_init(void)
{
    signed char x, y;
    unsigned char ci = 0;
    memset(BMAP, OUT, sizeof BMAP);
    for (y = 0; y <= MAX_Y; ++y) {
        for (x = MIN_X; x <= MAX_X; ++x)
            if (in_board(x, y)) {
                BMAP[AT(x, y)] = BALL_COLS[ci];
                ci = (unsigned char)((ci + 1) % 6);
            }
        ci = (unsigned char)((ci + 2) % 6);
    }
}

static void board_draw(void)
{
    signed char x, y;
    for (y = 0; y <= MAX_Y; ++y)
        for (x = MIN_X; x <= MAX_X; ++x) {
            unsigned char c = BMAP[AT(x, y)];
            if (c && c != OUT) stamp_ball_fast(CELL_X(x, y), CELL_Y(y), c, (unsigned char)(y & 1));
        }
}

static void carve_begin(void) { scan_lo = MAX_Y; scan_hi = 0; }

/* Punch a hole where a match would have cleared. */
static void carve(signed char y, signed char x0, signed char x1)
{
    signed char x;
    for (x = x0; x <= x1; ++x)
        if (BMAP[AT(x, y)] && BMAP[AT(x, y)] != OUT) {
            BMAP[AT(x, y)] = 0;
            erase_ball_clean(CELL_X(x, y), CELL_Y(y), (unsigned char)(y & 1));
        }
    /* Only the row directly above the hole can fall into it -- a ball moves one
     * row per step. The window then walks upward on its own, one row per step,
     * which is exactly how the cascade propagates. Starting at row 0 instead
     * made the first step a near-full-board scan: 113,141 cycles against
     * 16,985 for a steady-state one. Accumulates across several carves. */
    if (y - 1 < scan_lo) scan_lo = (signed char)(y > 0 ? y - 1 : 0);
    if (y > scan_hi)     scan_hi = y;
}

/* Advance every unsupported ball by one cell. Returns how many moved. */
static unsigned char cascade_step(void)
{
    signed char x, y, tx;
    signed char lo = MAX_Y, hi = 0;
    unsigned char moved = 0, bailed = 0;

    if (scan_hi > MAX_Y - 1) scan_hi = MAX_Y - 1;

    for (y = scan_hi; y >= scan_lo; --y) {
        /* Walk the row with a moving pointer. Indexing BMAP with a 16-bit
         * subscript cost cc65 333 cycles per cell; p[BMW] is a constant offset,
         * which compiles to ldy #32 / lda (ptr),y. */
        unsigned char *p = BMAP + AT(MIN_X, y);

        for (x = MIN_X; x <= MAX_X; ++x, ++p) {
            unsigned char col = *p;
            if (!col || col == OUT) continue;

            /* Falls only when BOTH down-diagonals are in-board and empty. */
            if (p[BMW])     continue;
            if (p[BMW + 1]) continue;

            tx = flipgate ? x : (signed char)(x + 1);
            flipgate ^= 1;

            *p = 0;
            p[BMW + (tx - x)] = col;
            erase_ball_clean(CELL_X(x, y), CELL_Y(y), (unsigned char)(y & 1));
            stamp_ball_fast(CELL_X(tx, y + 1), CELL_Y(y + 1), col, (unsigned char)((y + 1) & 1));
            if (y < lo) lo = y;
            if (y > hi) hi = y;
            if (++moved >= MAX_MOVES) { bailed = 1; goto out; }
        }
    }

out:
    if (moved) {
        scan_hi = (signed char)(hi + 1);
        /* On a bail the rows above the one we stopped at were never scanned,
         * so scan_lo must stay put and let the next step pick them up. */
        if (!bailed) scan_lo = (signed char)(lo > 0 ? lo - 1 : 0);
    }
    return moved;
}

/* One cell per frame reads as a blur, so a cell takes several.
 *
 * Six, not four, because release_cells() is still C and costs ~6,780 cycles per
 * ball -- nearly twice the assembly blit it follows. The worst capped step is
 * 105,096 cycles, which over six frames is 89% of one frame's budget. Moving
 * release_cells to assembly should buy back the two frames. */
#define FRAMES_PER_STEP 6

static void wait_frames(unsigned char n)
{
    while (n--) {
        while (VIC[0x12] == 250) { }
        while (VIC[0x12] != 250) { }
    }
}

int main(void)
{
    unsigned char n;
    unsigned long c;

    vic_bitmap_mode();
    stamp_init();
    silence_kernal_irq();

    board_init();
    board_draw();                       /* via the blit: ~240x faster than draw_board */
    carve_begin();
    /* Deliberately far bigger than any real match, which clears 3 or 5. This is
     * the worst case the frame budget has to survive, not the typical one. */
    carve(14, 8, 16);
    carve(15, 8, 16);
    carve(16, 9, 17);

    for (;;) {
        wait_frames(FRAMES_PER_STEP);

        VIC[0x20] = 2;                  /* red band = cascade work */
        timer_start();
        n = cascade_step();
        c = timer_read();
        VIC[0x20] = 0;

        store_result(0, c);
        RESULTS[4] = n;
        if (n && c > worst) { worst = c; store_result(5, c); RESULTS[9] = n; }

        /* Screenshots fire at a fixed cycle count, which makes catching a
         * specific moment of the animation a guessing game. Building with
         * -DSTOP_AFTER=n freezes the demo after exactly n steps instead, so a
         * sequence can be captured deterministically. Border turns white when
         * frozen. STOP_AFTER=0 just loops. */
#if STOP_AFTER
        if (++steps >= STOP_AFTER) { VIC[0x20] = 1; for (;;) { } }
#endif

        if (!n) {                       /* settled -- reset and run it again */
            board_init();
            board_draw();
            carve_begin();
    /* Deliberately far bigger than any real match, which clears 3 or 5. This is
     * the worst case the frame budget has to survive, not the typical one. */
    carve(14, 8, 16);
    carve(15, 8, 16);
    carve(16, 9, 17);
        }
    }
}

