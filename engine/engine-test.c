/* engine-test.c - self-test for the ported engine.
 *
 * The match rules are the part of the port that must not drift, and they are
 * not the sort of thing a screenshot can confirm. Each case sets up a board by
 * hand, runs the engine, and checks a result that can be reasoned out from the
 * rules in oddballz-app.js:
 *
 *   parallel axes,      dirs [4, 0, 3]   need FIVE in a row
 *   perpendicular axes, dirs [11, 9, 10] need THREE in a row
 *   dir 9 = (x-1, y-2) is vertical on screen
 *
 * Results land at $0350 for tools/peek.ps1:
 *   $0350        number of tests run
 *   $0351        number passed
 *   $0352..      one byte per test, 1 = pass
 *
 * Border goes green if every test passed, red otherwise, so a screenshot gives
 * the verdict without a memory read.
 */

#include "../engine/engine.h"
#include "../spikes/common/timer.h"

static unsigned char n_tests = 0, n_pass = 0;

static void check(unsigned char ok)
{
    RESULTS[2 + n_tests] = ok;
    ++n_tests;
    if (ok) ++n_pass;
}

/* Lay a run of `len` balls of colour `col` from (x,y) along `dir`. */
static void lay(signed char x, signed char y, unsigned char dir,
                unsigned char len, unsigned char col)
{
    while (len--) {
        BMAP[AT(x, y)] = col;
        x = (signed char)(x + DIR_DX[dir]);
        y = (signed char)(y + DIR_DY[dir]);
    }
}

static void clear_board(void)
{
    engine_init_board();
    match_count = 0; matches_done = 0; same_bonus = 0;
}

int main(void)
{
    unsigned char cleared;

    vic_bitmap_mode();
    n_colors = 6;

    /* --- parallel axis, dir 4: five match, four do not ----------------- */
    clear_board();
    lay(8, 10, 4, 5, 3);
    check(match_colors() == 5);

    clear_board();
    lay(8, 10, 4, 4, 3);
    check(match_colors() == 0);

    /* Six in a row is still one match, and clears all six. */
    clear_board();
    lay(8, 10, 4, 6, 3);
    cleared = match_colors();
    check(cleared == 6 && match_count == 1);

    /* sameBonus is len-3 for a parallel run (:898). */
    clear_board();
    lay(8, 10, 4, 6, 3);
    match_colors();
    check(same_bonus == 3);

    /* --- perpendicular axis, dir 11: three match, two do not ----------- */
    clear_board();
    lay(8, 8, 11, 3, 5);
    check(match_colors() == 3);

    clear_board();
    lay(8, 8, 11, 2, 5);
    check(match_colors() == 0);

    /* --- dir 9, the on-screen vertical -------------------------------- */
    clear_board();
    lay(12, 12, 9, 3, 2);
    check(match_colors() == 3);

    /* sameBonus is len-2 for a perpendicular run (:911). */
    clear_board();
    lay(12, 12, 9, 3, 2);
    match_colors();
    check(same_bonus == 1);

    /* --- a run must not be counted twice from both ends ---------------- */
    clear_board();
    lay(8, 10, 4, 5, 3);
    match_colors();
    check(match_count == 1);

    /* --- a gap must break a parallel run ------------------------------- */
    /* Six same-coloured balls on one screen row, but a hole at x=11, so the
     * longest run is 3. Parallel needs 5, and runs must not bridge gaps. */
    clear_board();
    lay(8, 10, 4, 3, 3);
    lay(12, 10, 4, 3, 3);
    check(match_colors() == 0);

    /* A different colour in the middle breaks it too. */
    clear_board();
    lay(8, 10, 4, 2, 3);
    BMAP[AT(10, 10)] = 4;
    lay(11, 10, 4, 2, 3);
    check(match_colors() == 0);

    /* --- perpendicular runs legitimately span non-touching balls ------- *
     * The perpendicular axes connect cells sqrt(3) apart -- the hex lattice's
     * second ring -- so the balls have a visible gap between them and the
     * cells in between are empty. That is the rule, not a fault: parallel axes
     * join touching balls and need 5, perpendicular axes join separated ones
     * and need 3. */
    clear_board();
    lay(8, 8, 11, 3, 5);
    check(BMAP[AT(9, 8)] == 0 && BMAP[AT(9, 9)] == 0 &&   /* nothing between */
          match_colors() == 3);

    /* --- different colours adjacent must not match --------------------- */
    clear_board();
    lay(8, 10, 4, 3, 3);
    lay(11, 10, 4, 2, 4);
    check(match_colors() == 0);

    /* --- support rule: a ball rests on TWO (:778) ---------------------- */
    clear_board();
    BMAP[AT(10, 10)] = 3;                    /* both diagonals empty */
    check(supported(10, 10) == 0);

    clear_board();
    BMAP[AT(10, 10)] = 3;
    BMAP[AT(10, 11)] = 4;                    /* down-left blocked */
    check(supported(10, 10) == 1);

    clear_board();
    BMAP[AT(10, 10)] = 3;
    BMAP[AT(11, 11)] = 4;                    /* down-right blocked */
    check(supported(10, 10) == 1);

    /* The board edge supports for free: below the bottom row is OUT. */
    clear_board();
    BMAP[AT(14, 19)] = 3;
    check(supported(14, 19) == 1);

    /* One settle step moves an unsupported ball exactly one row, and it lands
     * on one of the two down-diagonals. */
    clear_board();
    BMAP[AT(10, 10)] = 3;
    settle_from(10, 10);
    engine_settle_step();
    check(n_moves == 1 && BMAP[AT(10, 10)] == 0 &&
          (BMAP[AT(10, 11)] == 3 || BMAP[AT(11, 11)] == 3));

    /* A supported ball does not move. */
    clear_board();
    BMAP[AT(10, 10)] = 3;
    BMAP[AT(10, 11)] = 4;
    settle_from(10, 10);
    check(engine_settle_step() == 0);

    /* --- game over only triggers in rows 0-3, columns 4-12 (:1053) ----- */
    clear_board();
    end_game = 0;
    BMAP[AT(8, 2)] = 3;
    check(check_game_over() == 1);

    clear_board();
    end_game = 0;
    BMAP[AT(8, 10)] = 3;                     /* well below the danger zone */
    check(check_game_over() == 0);

    /* --- scoring: 2^count, capped at 10 (:1026) ------------------------ */
    clear_board();
    score = 0; match_count = 3; same_bonus = 4;
    check_advance();
    check(score == 8 + 4);

    /* --- level ramp: 6 colours by level 12 (:118) ---------------------- */
    check(LEV_COLORS[11] == 6 && LEV_COLORS[0] == 3);

    /* --- flip mirrors a bar and re-normalises rel[0] to (0,0) (:551) ---
     * flipX row my=2 is (2,0),(1,0),(0,0),(-1,0),(-2,0), so the bar
     * [-2,-1,0,1] becomes [2,1,0,-1]. */
    clear_board();
    piece_x = 12; piece_y = 8;
    rel_x[0] = 0;  rel_y[0] = 0;
    rel_x[1] = -1; rel_y[1] = 0;
    rel_x[2] = -2; rel_y[2] = 0;
    rel_x[3] = 1;  rel_y[3] = 0;
    check(piece_flip(1) == 1 &&
          rel_x[0] == 0 && rel_x[1] == 1 && rel_x[2] == 2 && rel_x[3] == -1 &&
          rel_y[0] == 0);

    /* flipY leaves a horizontal bar alone -- it mirrors the other axis. */
    clear_board();
    piece_x = 12; piece_y = 8;
    rel_x[0] = 0;  rel_y[0] = 0;
    rel_x[1] = -1; rel_y[1] = 0;
    rel_x[2] = -2; rel_y[2] = 0;
    rel_x[3] = 1;  rel_y[3] = 0;
    check(piece_flip(0) == 1 &&
          rel_x[1] == -1 && rel_x[2] == -2 && rel_x[3] == 1);

    /* --- rotation is (x,y) -> (x-y, x) (:539) -------------------------- */
    clear_board();
    piece_x = 12; piece_y = 8;
    rel_x[0] = 0; rel_y[0] = 0;
    rel_x[1] = 1; rel_y[1] = 0;
    rel_x[2] = 2; rel_y[2] = 0;
    rel_x[3] = 0; rel_y[3] = 1;
    check(piece_rotate_dir(1) == 1 &&
          rel_x[1] == 1 && rel_y[1] == 1 &&      /* (1,0) -> (1,1) */
          rel_x[3] == -1 && rel_y[3] == 0);      /* (0,1) -> (-1,0) */

    /* Counter-clockwise is (x,y) -> (y, y-x) (:545), and undoes clockwise. */
    clear_board();
    piece_x = 12; piece_y = 8;
    rel_x[0] = 0; rel_y[0] = 0;
    rel_x[1] = 1; rel_y[1] = 0;
    rel_x[2] = 2; rel_y[2] = 0;
    rel_x[3] = 0; rel_y[3] = 1;
    check(piece_rotate_dir(0) == 1 &&
          rel_x[1] == 0 && rel_y[1] == -1 &&     /* (1,0) -> (0,-1) */
          rel_x[3] == 1 && rel_y[3] == 1);       /* (0,1) -> (1,1) */

    clear_board();
    piece_x = 12; piece_y = 8;
    rel_x[0] = 0; rel_y[0] = 0;
    rel_x[1] = 1; rel_y[1] = 0;
    rel_x[2] = 2; rel_y[2] = 0;
    rel_x[3] = 0; rel_y[3] = 1;
    piece_rotate_dir(1);
    piece_rotate_dir(0);
    check(rel_x[1] == 1 && rel_y[1] == 0 && rel_x[3] == 0 && rel_y[3] == 1);

    /* --- row clearing (:930, :970) ------------------------------------ */
    clear_board();
    build_row_tables();
    { signed char x; for (x = 12; x <= 20; ++x) BMAP[AT(x, 19)] = 3; }
    check(row_full(12, 19, 4) == 1);

    clear_board();
    { signed char x; for (x = 12; x <= 19; ++x) BMAP[AT(x, 19)] = 3; }
    check(row_full(12, 19, 4) == 0);             /* one gap is enough */

    clear_board();
    row_count = 0;
    { signed char x; for (x = 12; x <= 20; ++x) BMAP[AT(x, 19)] = 3; }
    check_rows();
    check(row_count > 0 && BMAP[AT(16, 19)] == 0);

    /* find_full_row reports a full row WITHOUT deleting it, so the renderer can
     * flash it first. */
    clear_board();
    { signed char x; for (x = 12; x <= 20; ++x) BMAP[AT(x, 19)] = 3; }
    check(find_full_row() == 1 && frow_y == 19 && BMAP[AT(16, 19)] == 3);

    clear_board();
    { signed char x; for (x = 12; x <= 18; ++x) BMAP[AT(x, 19)] = 3; }
    check(find_full_row() == 0);

    /* An incomplete row survives. */
    clear_board();
    row_count = 0;
    { signed char x; for (x = 12; x <= 18; ++x) BMAP[AT(x, 19)] = 3; }
    check_rows();
    check(row_count == 0 && BMAP[AT(16, 19)] == 3);

    RESULTS[0] = n_tests;
    RESULTS[1] = n_pass;
    VIC[0x20] = (unsigned char)(n_pass == n_tests ? 5 : 2);   /* green / red */

    for (;;) { }
}
