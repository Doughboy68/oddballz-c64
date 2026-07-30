/* demo-perp.c - shows what a perpendicular match actually looks like.
 *
 * Draws two three-ball perpendicular runs on an otherwise empty board and
 * stops, without clearing them. Both are legal matches under the rules in
 * oddballz-app.js:903.
 *
 *   dir 11 = (x+2, y+1)  green   -- a shallow diagonal, about 30 degrees
 *   dir 9  = (x-1, y-2)  cyan    -- vertical on screen
 *
 * The point is the spacing. Perpendicular axes join cells sqrt(3) ~ 1.73 cell
 * widths apart, the hex lattice's second ring, so the balls do NOT touch and
 * the cells between them stay empty. Parallel axes join touching balls and
 * need five; perpendicular axes join separated ones and need three.
 */

#include "../engine/engine.h"
#include "../spikes/common/stamp.h"

#define EMPTY_COL 11
static const unsigned char BALL_C64[7] = { 0, 3, 2, 5, 7, 4, 10 };

static void lay(signed char x, signed char y, unsigned char dir,
                unsigned char len, unsigned char col)
{
    while (len--) {
        BMAP[AT(x, y)] = col;
        stamp_ball_fast(CELL_X(x, y), CELL_Y(y), BALL_C64[col], (unsigned char)(y & 1));
        x = (signed char)(x + DIR_DX[dir]);
        y = (signed char)(y + DIR_DY[dir]);
    }
}

int main(void)
{
    signed char x, y;

    vic_bitmap_mode();
    stamp_init();
    engine_init_game();

    for (y = 0; y <= MAX_Y; ++y)
        for (x = MIN_X; x <= MAX_X; ++x)
            if (in_board(x, y)) stamp_ball_fast(CELL_X(x, y), CELL_Y(y), EMPTY_COL, (unsigned char)(y & 1));

    lay(7,  6, 11, 3, 3);      /* green,  shallow diagonal */
    lay(14, 16, 9, 3, 1);      /* cyan,   vertical         */

    VIC[0x20] = 1;             /* white border = finished  */
    for (;;) { }
}
