/* game.c - the ported engine, playable.
 *
 * The falling piece rides four hardware sprites, so it moves at pixel
 * resolution rather than snapping a whole cell at a time; position is a
 * register write, which is why this costs almost nothing. On landing it is
 * stamped into the bitmap and the sprites are hidden.
 *
 * Keyboard, read straight off the CIA1 matrix:
 *
 *   J / L          move laterally
 *   I / K          rotate
 *   F              cycle the piece's colours
 *   X / Y          flip
 *   SPACE          drop fast
 *   P              pause -- wipes the screen, and offers E to end the game
 *
 * A piece nobody touches falls where it spawned. Build with -DDEMO_STEER=1 to
 * put the old self-steering back, which is only useful headlessly.
 *
 * Readable at $0350:
 *   $0350-$0351 score   $0352 level   $0353-$0354 balls
 *   $0355 landed        $0356 piece row  $0357 game overs
 *   $0358-$0359 resolve passes          $035A-$035B balls cleared
 *   $035C game overs that were "no room for the next piece"
 *   $035D pauses taken
 */

#define BOARD_DRAW_HELPERS 0   /* the game draws through stamp.s, not draw_ball */
#include "../engine/engine.h"
#include "../spikes/common/stamp.h"
/* timer.h dropped: its CIA cycle counter was for the frame-budget spikes and
 * nothing here calls it. RESULTS stays -- peek.ps1 still reads game state. */
#define RESULTS ((unsigned char *)0x0350)
#define CIA1    ((unsigned char *)0xDC00)
static void silence_kernal_irq(void) { CIA1[0x0D] = 0x7F; }

/* -DTEST_TIMING=1 pulls the CIA cycle counter back in to measure a draw. */
#ifndef TEST_TIMING
#define TEST_TIMING 0
#endif
#if TEST_TIMING
#define CIA2 ((unsigned char *)0xDD00)
static void timer_start(void)
{
    CIA2[0x0E] = 0x00; CIA2[0x0F] = 0x00;
    CIA2[0x0D] = 0x7F;
    CIA2[0x04] = 0xFF; CIA2[0x05] = 0xFF;
    CIA2[0x06] = 0xFF; CIA2[0x07] = 0xFF;
    CIA2[0x0F] = 0x41;                            /* TB counts TA underflows */
    CIA2[0x0E] = 0x01;                            /* TA counts phi2          */
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
#endif
#include "../engine/hud.h"
#include "../engine/sound.h"

#define SPRITE_DATA ((unsigned char *)0xBF40)   /* above the bitmap, in bank 2 */
#define SPRITE_PTR  253                         /* ($BF40 - $8000) / 64 */

/* C64 keyboard matrix: write an inverted column mask to port A, read rows back
 * from port B, active low. Port A must be output and port B input -- the
 * opposite of what reading a joystick needs, which is why this build is
 * keyboard-only. */
#define CIA1_PRA    (*(unsigned char *)0xDC00)
#define CIA1_PRB    (*(unsigned char *)0xDC01)
#define CIA1_DDRA   (*(unsigned char *)0xDC02)
#define CIA1_DDRB   (*(unsigned char *)0xDC03)

/* (column, row) in the matrix. IJKL stands in for the PC arrow keys, which the
 * C64 keyboard does not have:
 *   J / L    move left / right
 *   I / K    rotate counter-clockwise / clockwise
 *   F        cycle the piece's colours
 *   X / Y    flip about the x / y axis
 *   SPACE    zip */
#define K_I      4, 1
#define K_J      4, 2
#define K_K      4, 5
#define K_L      5, 2
#define K_F      2, 5
#define K_X      2, 7
#define K_Y      3, 1
#define K_SPACE  7, 4
#define K_M      4, 4      /* toggle music */
#define K_1      7, 0
#define K_2      7, 3
#define K_P      5, 1      /* pause */
#define K_E      1, 6      /* end the game -- only offered on the pause screen */

/* Fall speed. The original states it in grid ROWS PER SECOND and ramps it with
 * the level (:433):
 *
 *   baseSpeed = min(2.6, 1.0 + (level - 1) * 0.08)
 *
 * Frames per row is then the machine's frame rate divided by that, which is why
 * this cannot be a constant: PAL runs 50.125 fields a second and NTSC 59.826, so
 * the same frame count falls 19% faster on an NTSC machine. Level 1 is 50 frames
 * a row on PAL and 60 on NTSC; both reach 1.0 rows a second.
 *
 * It used to be a flat 24 frames -- 2.08 rows a second on PAL -- so the first
 * level fell at roughly twice the speed it should have, and there was no ramp at
 * all. */
#define FRAMES_PER_ROW_MAX  60                  /* level 1 on NTSC, the slowest */

/* The KERNAL works out which machine this is at reset, long before we run, and
 * leaves it here: 1 for PAL, 0 for NTSC. Cheaper and more certain than counting
 * raster lines ourselves, and it survives silence_kernal_irq() because that only
 * stops the interrupt, it does not undo what the reset already did. */
#define PAL_FLAG (*(unsigned char *)0x02A6)
#define FRAMES_PER_SETTLE    3   /* cascade speed: frames per cell of fall */
#define MATCH_FLASH         22                  /* frames matched balls flash before clearing */
#define REPEAT_DELAY        10                  /* frames before a held key repeats */
#define REPEAT_RATE          3

#ifndef SKIP_TITLE
#define SKIP_TITLE 0
#endif
#ifndef TEST_ROW
#define TEST_ROW 0   /* pre-fill the bottom row so a clear fires immediately */
#endif
#ifndef SKIP_MODE
#define SKIP_MODE 1   /* headless run's mode: 1 = colour match, 0 = row build */
#endif

/* Empty board cells are drawn as dim sockets rather than left black, so the
 * playfield is visible. */
/* The tiled ODDBALLZ wallpaper is off by default. It looked close to the
 * original standing still, but cyan is the brightest blue the palette has, so
 * it ended up louder than the game itself -- and at ~7,200 pixel plots it cost
 * roughly two seconds of drawing at the start of every game. Build with
 * -DUSE_BACKDROP=1 to bring it back; the code in hud.h is unchanged. */
#ifndef USE_BACKDROP
#define USE_BACKDROP 0
#endif
#ifndef BACKDROP_COL
#define BACKDROP_COL 3                          /* cyan, as the original */
#endif
#ifndef EMPTY_COL
#define EMPTY_COL 11                            /* dark grey */
#endif

/* Engine colour 1..6 -> C64 colour, matched to the ball materials in
 * oddballz-app.js:1949. Index 0 is unused; the engine never emits it.
 *
 *   1  0x0099ff azure cyan   -> cyan       2  0xff2a5f ruby red -> red
 *   3  0x00f055 emerald      -> green      4  0xffc107 amber    -> yellow
 *   5  0xb030ff amethyst     -> purple     6  0xff00b7 magenta  -> light red
 *
 * Nothing here is forced by the hardware: the C64 has 16 colours and exact
 * red, green and cyan among them. Passing the engine index straight through as
 * a C64 colour number was what put white on the board -- C64 colour 1 is white.
 * Magenta is the only real compromise, since purple already takes the nearest
 * slot; light red reads pink and stays distinct from the dark red at 2. */
static const unsigned char BALL_C64[7] = { 0, 3, 2, 5, 7, 4, 10 };

static unsigned char landed = 0, gameovers = 0;
/* Of those game overs, how many were "no room for the next piece" rather than a
 * ball in the danger zone. Published at $035C: it is the only way to tell from
 * outside which of the two end conditions is doing the work. */
static unsigned char no_room = 0;
static unsigned int  resolves = 0, total_cleared = 0;
static unsigned char prev_keys = 0, repeat_ctr = 0, prev_music = 0;
static unsigned char prev_p = 0, pause_req = 0;
/* Published at $035D. A pause is invisible after the fact -- the board looks
 * exactly as it did -- so without a count there is no way to tell a working
 * resume from one that never ran. */
static unsigned char pauses = 0;
#ifndef TEST_PAUSE
#define TEST_PAUSE 0
#endif
/* Force the "no room" ending after a few landings. It is the harder of the two
 * to reach on purpose, and it is the one with the piece to show. */
#ifndef TEST_NOROOM
#define TEST_NOROOM 0
#endif
#if TEST_PAUSE
static unsigned char tested_pause = 0;
#endif
static unsigned char prev_level = 1;

/* Self-steering. Test scaffolding, not a feature: with it on, an untouched piece
 * walks itself toward a random column so the board fills without a player and a
 * headless run has something to screenshot. In the real game a piece you do not
 * touch must fall where it was spawned, so this is OFF unless a build asks for
 * it with -DDEMO_STEER=1. */
#ifndef DEMO_STEER
#define DEMO_STEER 0
#endif
#if DEMO_STEER
static signed char   target_x = 12;
/* Set once the player touches a key, cleared on spawn. Without it the idle
 * self-steering fights the player: let go of left/right and the piece slides
 * straight back to the demo's target column. */
static unsigned char manual = 0;
#endif

/* isZipping, :406/:763. Latched by SPACE and cleared only when the next piece
 * spawns, so the drop does not stop the moment the key comes up. */
static unsigned char zipping = 0;

/* Sub-row interpolation, tabulated so the per-frame path has no division.
 *
 * Both are indexed by the frame counter and yield PIXELS. Getting that wrong is
 * subtle: `off` counts frames, a row is PITCH_Y pixels apart, and the two are
 * only equal by accident. Adding the frame count straight to the sprite's Y
 * made the piece descend FRAMES_PER_ROW pixels per row instead of PITCH_Y, so
 * it slid past the next row and snapped back -- invisible at 12 frames per row,
 * glaring at 24. */
static unsigned char SHEAR[FRAMES_PER_ROW_MAX];  /* horizontal, half a ball per row */
static unsigned char STEP_Y[FRAMES_PER_ROW_MAX]; /* vertical, PITCH_Y per row */

/* Both tables and the zip step depend on the current speed, so they are rebuilt
 * whenever the level changes rather than being constants. */
static unsigned char frames_per_row = FRAMES_PER_ROW_MAX;
static unsigned char zip_step = FRAMES_PER_ROW_MAX / 2;

/* Hundredths of a field per second, set once from PAL_FLAG. */
static unsigned int fps100 = 5013;

static void set_fall_speed(void)
{
    /* Hundredths of a row per second, so the whole thing stays in integers:
     * 1.00 + (level-1) * 0.08, capped at 2.60. Frames per row is the frame rate
     * over that, rounded rather than truncated -- NTSC level 1 is 59.83 frames,
     * and truncating would make it 59 and quietly 2% fast. */
    unsigned int s = (unsigned int)(100 + ((unsigned int)level - 1) * 8);
    unsigned char o;
    if (s > 260) s = 260;
    frames_per_row = (unsigned char)((fps100 + s / 2) / s);

    /* Zip advances a row every other frame -- 25 rows a second. The original
     * uses 35, which would be 1.43 frames a row; the piece can only move a whole
     * row at a time here, so it is 25 or 50, and 25 is the one that reads as a
     * fast drop rather than a teleport. */
    zip_step = (unsigned char)(frames_per_row / 2);

    for (o = 0; o < frames_per_row; ++o) {
        SHEAR[o]  = (unsigned char)(((unsigned int)o * (PITCH_X / 2)) / frames_per_row);
        STEP_Y[o] = (unsigned char)(((unsigned int)o * PITCH_Y)       / frames_per_row);
    }
}

/* The sound player is ticked here rather than from a raster interrupt: every
 * wait in the game goes through this, so music and effects advance with the
 * frame without needing an assembly ISR. The one audible gap is the initial
 * board draw, which never waits on a frame. */
static void wait_frame(void)
{
    while (VIC[0x12] == 250) { }
    while (VIC[0x12] != 250) { }
    sound_tick();
}

static void wait_frames(unsigned char n) { while (n--) wait_frame(); }

static void draw_cell(signed char x, signed char y, unsigned char col)
{
    stamp_ball_fast(CELL_X(x, y), CELL_Y(y), col, (unsigned char)(y & 1));
}

static void clear_cell(signed char x, signed char y)
{
    erase_ball_clean(CELL_X(x, y), CELL_Y(y), (unsigned char)(y & 1));
    if (on_field(x, y)) draw_cell(x, y, EMPTY_COL);   /* leave the socket behind */
}

/* Ball shape into sprite memory, reusing ball_halfwidth so a falling ball is
 * pixel-identical to a landed one. Multicolour sprite: pattern 10 picks the
 * per-sprite colour, so all four balls can differ while sharing one shape. */
static void build_sprite(void)
{
    int dy, dx, ry = BALL_H / 2;
    unsigned char i;
    for (i = 0; i < 63; ++i) SPRITE_DATA[i] = 0;
    for (dy = -ry; dy <= ry; ++dy) {
        int w = ball_halfwidth(dy), row = 5 + dy;
        if (row < 0 || row > 20) continue;
        for (dx = -w; dx <= w; ++dx) {
            int px = 4 + dx;
            if (px < 0 || px > 11) continue;
            SPRITE_DATA[row * 3 + (px >> 2)] |= (unsigned char)(2 << ((3 - (px & 3)) << 1));
        }
    }
}

static void sprites_init(void)
{
    unsigned char i;
    for (i = 0; i < 4; ++i) SCREEN[0x3F8 + i] = SPRITE_PTR;
    VIC[0x1C] = 0x0F;       /* multicolour sprites 0-3 */
    VIC[0x25] = 0x0B;
    VIC[0x26] = 0x0C;
    VIC[0x1B] = 0x00;       /* in front of the bitmap */
    VIC[0x15] = 0x00;       /* start hidden */
}

/* Ball centre sits at sprite pixel 4 (hires offset 8) and sprite row 5. */
static void sprite_pos(unsigned char i, int lx, int ly)
{
    unsigned int sx = (unsigned int)(16 + 2 * lx);
    VIC[0x00 + i * 2] = (unsigned char)(sx & 0xFF);
    VIC[0x01 + i * 2] = (unsigned char)(45 + ly);
    if (sx > 255) VIC[0x10] = (unsigned char)(VIC[0x10] |  (1 << i));
    else          VIC[0x10] = (unsigned char)(VIC[0x10] & ~(1 << i));
}

/* Place the four sprites for the piece, `off` frames into its fall. */
static void piece_show(unsigned char off)
{
    unsigned char i, vis = 0;
    signed char sgn = (signed char)(direction == 5 ? 1 : -1);
    for (i = 0; i <= 3; ++i) {
        signed char gx = (signed char)(piece_x + rel_x[i]);
        signed char gy = (signed char)(piece_y + rel_y[i]);
        VIC[0x27 + i] = BALL_C64[img[i]];
        sprite_pos(i, CELL_X(gx, gy) + sgn * SHEAR[off], CELL_Y(gy) + STEP_Y[off]);
        /* Masked above the field, the way the Windows version does it: a ball
         * is not drawn until it reaches the fourth row. Four independent
         * sprites make this per BALL rather than per piece, so a shape enters
         * one ball at a time instead of appearing whole. */
        if (gy >= FIELD_TOP) vis |= (unsigned char)(1 << i);
    }
    VIC[0x15] = vis;
}

static void publish(void)
{
    RESULTS[0]  = (unsigned char)score;
    RESULTS[1]  = (unsigned char)(score >> 8);
    RESULTS[2]  = level;
    RESULTS[3]  = (unsigned char)ball_count;
    RESULTS[4]  = (unsigned char)(ball_count >> 8);
    RESULTS[5]  = landed;
    RESULTS[6]  = (unsigned char)piece_y;
    RESULTS[7]  = gameovers;
    RESULTS[8]  = (unsigned char)resolves;
    RESULTS[9]  = (unsigned char)(resolves >> 8);
    RESULTS[10] = (unsigned char)total_cleared;
    RESULTS[11] = (unsigned char)(total_cleared >> 8);
    RESULTS[12] = no_room;
    RESULTS[13] = pauses;
    RESULTS[30] = PAL_FLAG;         /* $036E: 1 PAL, 0 NTSC */
    RESULTS[31] = frames_per_row;   /* $036F: the calibrated fall speed */
}

/* checkMatches, :1001. matcher picks the mode: clear by colour, or clear full
 * rows. They are alternatives, not stages.
 *
 * plo/phi are the row range of the piece that just landed.
 *
 * Settling must run after EVERY landing, not only when something was cleared.
 * The JS loops `while (index > 0 || hasClearedRows || !noneDropped)` (:1013),
 * so checkGaps happens on every pass regardless. A landing piece can leave its
 * own balls unsupported -- an L-shape resting on one end -- and skipping the
 * settle left them floating indefinitely, until some unrelated match elsewhere
 * happened to open a scan window over their rows and they finally dropped. */
static void resolve(signed char plo, signed char phi)
{
    unsigned char first = 1;

    for (;;) {
        signed char x, y, ylo = MAX_Y, yhi = 0;
        unsigned char changed = 0, dropped = 0;

        ++resolves;

        if (matcher) {
            unsigned char cl = match_colors();
            total_cleared += cl;
            if (cl) {
                /* Flash the matched balls white before they go. This is the
                 * only way to see WHICH balls the engine grouped together --
                 * at speed a match is over in a frame or two, and separate
                 * runs on opposite sides of the board clear in the same pass,
                 * which reads as unrelated balls vanishing. */
                sfx_play(SFX_POP);
                for (y = 0; y <= MAX_Y; ++y)
                    for (x = MIN_X; x <= MAX_X; ++x)
                        if (MARK[AT(x, y)]) draw_cell(x, y, 1);   /* white */
                wait_frames(MATCH_FLASH);

                for (y = 0; y <= MAX_Y; ++y)
                    for (x = MIN_X; x <= MAX_X; ++x)
                        if (MARK[AT(x, y)]) {
                            clear_cell(x, y);
                            if (y < ylo) ylo = y;
                            if (y > yhi) yhi = y;
                            changed = 1;
                        }
            }
        } else {
            /* Row Build. deleteRow rewrites whole COLUMNS rather than
             * individual cells, so unlike a colour match there is no tidy list
             * of what it touched. Snapshot the board first, then redraw only
             * the cells that actually changed. MARK is free here -- only
             * match_colors uses it, and that is the other mode.
             *
             * The previous version called vic_bitmap_mode() and repainted just
             * the balls. That wiped the empty sockets AND the HUD, so after a
             * row cleared the board became balls floating in a black void --
             * which is exactly why Row Build looked as though its supports had
             * stopped working. Nothing was wrong with the settling; the
             * playfield had simply been erased out from under it. */
            memcpy(MARK, BMAP, sizeof BMAP);
            if (find_full_row()) {
                /* Flash the row white before it goes, exactly as a colour match
                 * does. find_full_row rather than check_rows because the latter
                 * deletes as it finds, leaving nothing to show. */
                signed char fx = frow_x, fy = frow_y;
                sfx_play(SFX_POP);
                while (BMAP[AT(fx, fy)] != OUT) {
                    draw_cell(fx, fy, 1);            /* white */
                    /* Force this cell into the redraw below: after the collapse
                     * it might coincidentally hold the same colour it started
                     * with, and would then stay white. */
                    MARK[AT(fx, fy)] = 0xFE;
                    fx = (signed char)(fx + DIR_DX[frow_rdir]);
                    fy = (signed char)(fy + DIR_DY[frow_rdir]);
                }
                wait_frames(MATCH_FLASH);
                delete_row(frow_x, frow_y, frow_rdir, frow_cdir);
            }
            for (y = 0; y <= MAX_Y; ++y)
                for (x = MIN_X; x <= MAX_X; ++x) {
                    unsigned int i = AT(x, y);
                    if (BMAP[i] == OUT || BMAP[i] == MARK[i]) continue;
                    /* A socket occupies the same pixels as a ball, so drawing
                     * one cleanly replaces the other -- except above the field,
                     * where there is no socket to fall back to and the cell has
                     * to be erased instead. */
                    if (BMAP[i])            draw_cell(x, y, BALL_C64[BMAP[i]]);
                    else if (on_field(x, y)) draw_cell(x, y, EMPTY_COL);
                    else                     clear_cell(x, y);
                    changed = 1;
                }
        }

        if (changed) {
            wait_frames(FRAMES_PER_SETTLE);
        } else if (!first) {
            return;
        }
        first = 0;

        /* Row Build does NOT settle. Balls stay exactly where they land, which
         * is the whole point of the mode: you are building complete rows, and
         * gravity pulling balls out from under a half-built row would make that
         * impossible. Only deleteRow moves anything, by collapsing the columns
         * of a row it has just cleared.
         *
         * This is also what the JS does, though by omission rather than intent:
         * the landing path guards checkMatches with `if (this.matcher)` (:495),
         * so in Row Build checkGaps never runs either. */
        if (!matcher) {
            if (!changed) return;
            continue;
        }

        /* Always settle from the WHOLE board.
         *
         * Starting the window at just the disturbed rows was faster but not
         * safe: the window narrows to wherever balls actually moved, so a ball
         * left unsupported in a row that dropped out of the window never got
         * rescanned and hung in mid-air until some later landing happened to
         * open a window over it. A landed piece can also strand its own balls
         * -- it stops when ANY of the four is blocked, so the others may have
         * empty space beneath them.
         *
         * The cost is one full-board scan per settle sequence, about 75,000
         * cycles or four frames; every step after the first narrows as before.
         * Cheap next to a ball hanging in the air. */
        settle_from(0, MAX_Y - 1);
        (void)plo; (void)phi; (void)ylo; (void)yhi;

        while (engine_settle_step()) {
            unsigned char i;
            dropped = 1;
            for (i = 0; i < n_moves; ++i) {
                clear_cell(move_fx[i], move_fy[i]);
                draw_cell(move_tx[i], move_ty[i], BALL_C64[move_col[i]]);
            }
            wait_frames(FRAMES_PER_SETTLE);
        }

        /* Settling can expose fresh matches, so only stop when a pass both
         * cleared nothing and dropped nothing. */
        if (!changed && !dropped) return;
    }
}

static unsigned char key_down(unsigned char col, unsigned char row)
{
    CIA1_PRA = (unsigned char)~(1 << col);
    return (unsigned char)((CIA1_PRB & (1 << row)) == 0);
}

#if DEMO_STEER
static unsigned char demo_ctr = 0;
#endif

/* Returns non-zero if a fast drop is being asked for.
 *
 * direction is NOT touched here. It is chosen once per piece at spawn
 * (:337) and fixed for the whole fall. Re-deciding it each frame made pieces
 * zig-zag across their target column and bounce off the board edges. */
static unsigned char read_input(void)
{
    unsigned char k = 0, pressed;

    if (key_down(K_J))     k |= 0x01;
    if (key_down(K_L))     k |= 0x02;
    if (key_down(K_SPACE)) k |= 0x04;
    if (key_down(K_K))     k |= 0x08;
    if (key_down(K_F))     k |= 0x10;
    if (key_down(K_X))     k |= 0x20;
    if (key_down(K_Y))     k |= 0x40;
    if (key_down(K_I))     k |= 0x80;

    pressed = (unsigned char)(k & ~prev_keys);
    prev_keys = k;
#if DEMO_STEER
    if (k) manual = 1;                       /* player has taken over, for good */
#endif

    /* Held left/right repeats, but only after a delay. Moving on every frame is
     * twelve cells per row, which reads as a twitch rather than a steer. */
    if (k & 0x03) {
        signed char dx = (signed char)((k & 0x01) ? -1 : 1);
        if (pressed & 0x03)      { piece_move(dx); repeat_ctr = REPEAT_DELAY; }
        else if (repeat_ctr && !--repeat_ctr) { piece_move(dx); repeat_ctr = REPEAT_RATE; }
    } else {
        repeat_ctr = 0;
    }

    /* Edge-triggered, or they fire every frame the key is down.
     *
     * Only the colour cycle makes a noise. Steering, rotating and flipping are
     * constant during a fall, and a click on each turned the game into a
     * typewriter. The sounds that remain all mark something happening to the
     * BOARD: cycle, zip, lock, match. */
    if (pressed & 0x10) sfx_play(SFX_CLICK);  /* F: colour cycle */
    if (pressed & 0x08) piece_rotate_dir(1);  /* K: clockwise */
    if (pressed & 0x80) piece_rotate_dir(0);  /* I: counter-clockwise */
    if (pressed & 0x10) piece_cycle_colors(); /* F */
    if (pressed & 0x20) piece_flip(1);        /* X: flip about x */
    if (pressed & 0x40) piece_flip(0);        /* Y: flip about y */

    /* zip(), :763 -- a latch, not a held key. Once committed the piece drops
     * all the way; isZipping is only cleared when the next one spawns (:406). */
    if (pressed & 0x04) { zipping = 1; sfx_play(SFX_ZIP); }

    if (key_down(K_M)) { if (!(prev_music)) music_toggle(); prev_music = 1; }
    else prev_music = 0;

    /* P only raises a flag. The pause blocks for as long as it is up and repaints
     * the screen twice, neither of which belongs in the routine that reads the
     * keyboard once a frame. */
    if (key_down(K_P)) { if (!prev_p) pause_req = 1; prev_p = 1; }
    else prev_p = 0;

#if DEMO_STEER
    if (!manual) {                           /* nobody playing: demo itself */
        if (demo_ctr) --demo_ctr;
        else {
            demo_ctr = REPEAT_RATE;
            if (piece_x < target_x)      piece_move(1);
            else if (piece_x > target_x) piece_move(-1);
        }
    }
#endif
    return 0;
}

/* Every in-board cell gets a dim socket, so the playfield is visible instead of
 * black on black. Landed balls draw over the socket; clearing one puts it back. */
/* The board's shape, worked out once. Every row of a hexagon is one contiguous
 * run, so a row is fully described by where it starts and how long it is --
 * and the shape never changes, so scanning all 441 grid positions with
 * in_board() on every draw was a quarter of what the draw cost. */
static unsigned char pf_bc[MAX_Y + 1], pf_n[MAX_Y + 1], pf_y0[MAX_Y + 1];
static unsigned char pf_ready = 0;

static void playfield_init(void)
{
    signed char x, y;
    for (y = 0; y <= MAX_Y; ++y) {
        signed char x0 = 0, x1 = 0;
        unsigned char any = 0;
        for (x = MIN_X; x <= MAX_X; ++x)
            if (on_field(x, y)) { if (!any) { x0 = x; any = 1; } x1 = x; }
        /* Same arithmetic stamp_ball_fast does, once for the row instead of
         * once per socket. */
        pf_bc[y] = any ? (unsigned char)((CELL_X(x0, y) - 4) >> 2) : 0;
        pf_n[y]  = any ? (unsigned char)(x1 - x0 + 1) : 0;
        pf_y0[y] = (unsigned char)(CELL_Y(y) - BALL_H / 2);
    }
    pf_ready = 1;
}

static void draw_playfield(void)
{
    unsigned char y;
    if (!pf_ready) playfield_init();
    for (y = 0; y <= MAX_Y; ++y) {
        if (!pf_n[y]) continue;
        sr_bc0  = pf_bc[y];
        sr_n    = pf_n[y];
        sr_y0   = pf_y0[y];
        sr_slot = (unsigned char)((y & 1) ? 2 : 1);
        sr_col  = EMPTY_COL;
        socket_row();
    }
}

/* Returns zero when there was no room for the new piece, which is game over.
 *
 * This used to call restart() instead -- silently wiping the board AND the score
 * mid-game, which is what "the playfield clears randomly while playing" was. It
 * was a guard I added, not something the original does: build() (:504) spawns
 * unconditionally and checkGameOver() (:1053) is the only end condition.
 *
 * It fires because the two tests disagree about where the danger is.
 * check_game_over() looks only at columns 4-12 of rows 0-3, but a spawned shape
 * can reach outside that -- one cell over in x, or a row further down -- so a
 * board that is not yet "over" can still have no room for the next piece. The
 * original never notices because it stamps the piece anyway; here that would
 * overwrite landed balls, so ending the game is both closer to the intent and
 * tidier than what it replaced. */
static unsigned char next_piece(void)
{
    piece_spawn();
    zipping = 0;
#if DEMO_STEER
    target_x = (signed char)(6 + rnd(12));
#endif
    direction = (unsigned char)(rnd(2) ? 5 : 2);   /* fixed for this piece, :337 */
    return piece_fits(piece_x, piece_y);
}

/* --- title and end screens --------------------------------------------- */

static const unsigned char T_ODDBALLZ[8] = { G_O, G_D, G_D, G_B, G_A, G_L, G_L, G_Z };
static const unsigned char T_C64[3]      = { G_C, 6, 4 };
/* Section headings. Light green: distinct from the cyan title, the yellow C64
 * and the white key list, so the two headings read as labels rather than as
 * another line of the thing they sit above. */
static const unsigned char T_CONTROLS[8] = { G_C, G_O, G_N, G_T, G_R, G_O, G_L, G_S };
static const unsigned char T_SELECT[11]  = { G_S, G_E, G_L, G_E, G_C, G_T, G_SP,
                                             G_G, G_A, G_M, G_E };
/* Key list for the title. Key first, then what it does -- that is the order you
 * read it in when you are looking for a key. */
static const unsigned char T_K1[8]  = { G_J, G_SP, G_L, G_SP, G_M, G_O, G_V, G_E };
static const unsigned char T_K2[10] = { G_I, G_SP, G_K, G_SP, G_R, G_O, G_T, G_A, G_T, G_E };
static const unsigned char T_K3[7]  = { G_F, G_SP, G_C, G_Y, G_C, G_L, G_E };
static const unsigned char T_K4[8]  = { G_X, G_SP, G_Y, G_SP, G_F, G_L, G_I, G_P };
static const unsigned char T_K5[10] = { G_S, G_P, G_A, G_C, G_E, G_SP, G_D, G_R, G_O, G_P };
static const unsigned char T_K6[7]  = { G_P, G_SP, G_P, G_A, G_U, G_S, G_E };

/* The pause screen. Both options name their key first, the same way the title's
 * control list does. */
static const unsigned char T_PAUSED[6]  = { G_P, G_A, G_U, G_S, G_E, G_D };
static const unsigned char T_RESUME[8]  = { G_P, G_SP, G_R, G_E, G_S, G_U, G_M, G_E };
static const unsigned char T_ENDGAME[10] = { G_E, G_SP, G_E, G_N, G_D, G_SP,
                                             G_G, G_A, G_M, G_E };

static const unsigned char T_MODE1[13]   = { 1, G_SP, G_C, G_O, G_L, G_O, G_R, G_SP,
                                             G_M, G_A, G_T, G_C, G_H };
static const unsigned char T_MODE2[11]   = { 2, G_SP, G_R, G_O, G_W, G_SP,
                                             G_B, G_U, G_I, G_L, G_D };
static const unsigned char T_PRESS[11]   = { G_P, G_R, G_E, G_S, G_S, G_SP,
                                             G_S, G_P, G_A, G_C, G_E };
static const unsigned char T_GAMEOVER[9] = { G_G, G_A, G_M, G_E, G_SP,
                                             G_O, G_V, G_E, G_R };
static const unsigned char T_SCORE[5]    = { G_S, G_C, G_O, G_R, G_E };

/* Blocks until SPACE is pressed AND released, so the press does not fall
 * through and immediately zip the first piece. */
static void wait_space(void)
{
#if SKIP_TITLE
    /* Headless verification has no key to press. Auto-continuing rather than
     * skipping the screens means both still DRAW, so what they leave behind on
     * the bitmap is exactly what a real run would leave. */
    wait_frames(120);
#else
    while (!key_down(K_SPACE)) wait_frame();
    while (key_down(K_SPACE))  wait_frame();
#endif
    prev_keys = 0;
}

/* Also picks the game mode. Row Build is the JS `matcher` flag inverted
 * (:1007): clear complete rows instead of colour matches. It has been ported
 * since the engine went in but was never reachable. */
static void title_screen(void)
{
    VIC[0x15] = 0x00;                        /* nothing from the last game */
    vic_bitmap_mode();
#if TEST_TIMING
    timer_start();
#endif
    /* Everything shifted up to make room for a sixth key line. A heading sits
     * ~16 apart from what it labels and the lines under it 12, so the gap itself
     * says which lines belong to which heading. */
    big_text(14,  T_ODDBALLZ,  8, 3, 3);     /* cyan, triple size */
#if TEST_TIMING
    /* ODDBALLZ alone: 77 set font pixels x 9 for the 3x scale = 693 plot calls,
     * so this divides out to a clean cycles-per-plot. */
    store_result(18, timer_read());          /* $0362-$0365 */
#endif
    big_text(40,  T_C64,       3, 7, 2);     /* yellow            */

    big_text(64,  T_CONTROLS,  8, 13, 1);    /* heading, light green */
    big_text(80,  T_K1,        8,  1, 1);    /* the keys, white      */
    big_text(92,  T_K2,       10,  1, 1);
    big_text(104, T_K3,        7,  1, 1);
    big_text(116, T_K4,        8,  1, 1);
    big_text(128, T_K5,       10,  1, 1);
    big_text(140, T_K6,        7,  1, 1);

    /* The 1 and the 2 in yellow: they are the thing you actually press, and in
     * one colour with the mode name they read as part of it. */
    big_text (160, T_SELECT,  11,    13, 1); /* heading, light green */
    big_text2(176, T_MODE1,   13, 1,  7, 3, 1);
    big_text2(188, T_MODE2,   11, 1,  7, 3, 1);
#if TEST_TIMING
    store_result(14, timer_read());          /* $035E-$0361, cycles */
#endif

#if SKIP_TITLE
    matcher = SKIP_MODE;
    wait_frames(120);
#else
    for (;;) {
        if (key_down(K_1)) { matcher = 1; break; }
        if (key_down(K_2)) { matcher = 0; break; }
        wait_frame();
    }
    while (key_down(K_1) || key_down(K_2)) wait_frame();
#endif
    prev_keys = 0;
}

/* Overlaid on the board rather than replacing it, so the final position stays
 * visible. Drawn through the colour-RAM path, which the board never uses, so
 * the balls underneath keep their own colours. */
static void gameover_screen(void)
{
    ov_text(58,  T_GAMEOVER, 9, 1, 2);       /* white, double size */
    ov_text(90,  T_SCORE,    5, 1, 1);
    ov_number(102, score,    5, 1, 2);
    ov_text(132, T_PRESS,   11, 1, 2);       /* double size too    */
    wait_space();
}

/* Repaint the playfield from the engine's own board, which is the authority --
 * the bitmap is only ever a picture of it. Used to come back from a pause, where
 * the screen has been deliberately wiped.
 *
 * A socket occupies the same pixels as a ball, so painting sockets everywhere
 * first and balls over the occupied cells needs no erase step. */
static void redraw_board(void)
{
    signed char x, y;

    vic_bitmap_mode();
#if USE_BACKDROP
    draw_backdrop(BACKDROP_COL);
#endif
    draw_playfield();
    for (y = 0; y <= MAX_Y; ++y)
        for (x = MIN_X; x <= MAX_X; ++x) {
            unsigned int i = AT(x, y);
            if (BMAP[i] && BMAP[i] != OUT) draw_cell(x, y, BALL_C64[BMAP[i]]);
        }
    hud_reset();                /* vic_bitmap_mode() wiped the panels */
    hud_init();
    hud_update(level, skill, ball_count, score);
}

/* Pause. Returns non-zero if the player chose to end the game.
 *
 * The screen is WIPED rather than overlaid, which is the point: a pause you can
 * read the board through is a free planning move, and the original did not offer
 * one. The sprites go too, or the falling piece would hang there over the
 * pause text.
 *
 * Ending is on this screen rather than on a key of its own during play. It gives
 * the "are you sure" for free -- you have to stop the game and pick E off a list
 * with the alternative sitting next to it -- without a second screen for it. */
static unsigned char pause_screen(void)
{
    unsigned char ended = 0;

    ++pauses;
    VIC[0x15] = 0x00;
    vic_bitmap_mode();                       /* bitmap, screen and colour RAM */
#if TEST_TIMING
    timer_start();
#endif
    big_text(56,  T_PAUSED,  6, 7, 3);       /* yellow, triple size */
    big_text(104, T_RESUME,  8, 1, 1);       /* white               */
    big_text(120, T_ENDGAME, 10, 1, 1);
#if TEST_TIMING
    store_result(26, timer_read());          /* $036A-$036D */
#endif

    for (;;) {
#if TEST_PAUSE == 2
        /* Hold it up. Resuming after 90 frames leaves a 1.8-second window in a
         * two-minute run, which sampling screenshots does not reliably hit. */
        wait_frame();
        continue;
#elif TEST_PAUSE == 3
        /* Take the quit branch, which is otherwise unreachable headlessly. */
        wait_frames(60);
        ended = 1;
        break;
#elif SKIP_TITLE
        /* Headless: no key can be pressed, and holding here would stall the run.
         * Resume, so what follows is still a real game. */
        wait_frames(90);
        break;
#else
        if (key_down(K_P)) break;
        if (key_down(K_E)) { ended = 1; break; }
        wait_frame();
#endif
    }
    while (key_down(K_P) || key_down(K_E)) wait_frame();

    /* Only on resume. Quitting goes straight to the title, so repainting a board
     * nobody will look at would just be a wait between the key and the title --
     * and the repaint is the slowest thing here. */
    if (!ended) redraw_board();
    prev_keys = 0;
    return ended;
}

/* The end sequence, shared by both ways of reaching it.
 *
 * show_piece is for the "no room" ending. That piece is never stamped -- it
 * cannot be, the cells it wants are occupied -- so leaving it on the sprites is
 * the only way to see it at all, and without it the game just stops with nothing
 * on screen that explains why. It sits over the board, overlapping whatever
 * blocked it, which is exactly the thing to look at.
 *
 * It was hidden here at first, on the grounds that a piece that never fitted was
 * clutter. It is not clutter, it is the answer to "why did that end?". */
static void end_of_game(unsigned char show_piece)
{
    if (show_piece) {
        /* All four, past the mask. The point of showing this piece is that it
         * had nowhere to go, and half of it hidden above the field would not
         * say that. */
        piece_show(0);
        VIC[0x15] = 0x0F;
    } else {
        VIC[0x15] = 0x00;
    }
    ++gameovers;
    sfx_play(SFX_OVER);
    VIC[0x20] = 2;
    wait_frames(40);
    VIC[0x20] = BORDER_COL;
    gameover_screen();          /* over the board, waits for SPACE */
    VIC[0x15] = 0x00;           /* or a shown piece follows us to the title */
}

int main(void)
{
    unsigned char off = 0;

    vic_bitmap_mode();
    stamp_init();
    font_init();                             /* the size-1 glyph mask table */
    silence_kernal_irq();
    sound_init();
    CIA1_DDRA = 0xFF;                        /* columns out, rows in: keyboard */
    CIA1_DDRB = 0x00;
    build_sprite();
    sprites_init();

    /* 50.125 fields a second on PAL, 59.826 on NTSC -- the real numbers, not 50
     * and 60, because a 0.3% error compounds over a long game. */
    fps100 = PAL_FLAG ? 5013u : 5983u;
    set_fall_speed();

    for (;;) {          /* title -> game -> game over -> title */
    title_screen();

    /* Wipe the title. draw_playfield() only paints sockets, so without this the
     * title text stays in the bitmap and shows through the gaps between balls
     * and out to the sides of the hexagon. */
    vic_bitmap_mode();

    engine_init_game();
    set_fall_speed();               /* level is back to 1, so the speed is too */
#if USE_BACKDROP
    draw_backdrop(BACKDROP_COL);   /* before the board: it clips against it */
#endif
#if TEST_TIMING
    timer_start();
    draw_playfield();
    store_result(22, timer_read());          /* $0366-$0369 */
#else
    draw_playfield();
#endif
    /* hud_reset() matters now that the game loops back to the title. hud_update
     * only repaints a value that CHANGED, and the cached values survive a new
     * game -- so on the second game level was still 1, the repaint was skipped,
     * and the digit stayed missing after vic_bitmap_mode() wiped the panel.
     * Only LEVEL showed the bug, because only LEVEL starts each game with the
     * same value it ended the last one with. */
    hud_reset();
    hud_init();
    /* Paint the starting values. hud_update only runs after a landing, so
     * without this the panels sit blank until the first piece comes to rest. */
    hud_update(level, skill, ball_count, score);

    /* A real game over is reachable headlessly but rare -- the demo tops out
     * roughly once every 170 million cycles and the screen shows for two
     * seconds, so catching it by chance is about a one in seventy shot.
     * -DTEST_GAMEOVER=1 shows it immediately instead. */
#if TEST_ROW
    { signed char tx;
      for (tx = 12; tx <= 20; ++tx) { BMAP[AT(tx, 19)] = 3; draw_cell(tx, 19, BALL_C64[3]); } }
#endif
#if TEST_GAMEOVER
    score = 12345;
    gameover_screen();
#endif
    next_piece();

    for (;;) {
        signed char nx, ny;

        wait_frame();
        read_input();

#if TEST_PAUSE
        /* Raise it once, a few landings in, so a screenshot catches the pause
         * over a board that has something on it -- which is the only way to see
         * that resuming puts the board back. No key can be pressed headlessly. */
        if (landed >= 3 && !tested_pause) { tested_pause = 1; pause_req = 1; }
#endif
        /* Quitting from the pause is not a game over: no GAME OVER, no score
         * screen, no counting it as one. You chose to stop, so the only thing
         * you are waiting for is the title. */
        if (pause_req) {
            pause_req = 0;
            if (pause_screen()) break;
        }

        /* Test the next row BEFORE sliding toward it. Interpolating first and
         * asking afterwards meant the sprite travelled nine of the ten pixels
         * into an occupied cell -- visibly overlapping the ball below -- and
         * then snapped back a whole row when it locked.
         *
         * The JS hit this too when its motion was made smooth, and fixed it the
         * same way: updateContinuous (:442) walks every integer row the piece
         * would cross this frame and lands it on "the row above the blocker"
         * (:459) rather than moving first and checking afterwards. This is the
         * discrete-row form of that rule. */
        nx = (signed char)(direction == 5 ? piece_x + 1 : piece_x);
        ny = (signed char)(piece_y + 1);

        if (piece_fits(nx, ny)) {
            piece_show(off);
            off = (unsigned char)(off + (zipping ? zip_step : 1));
            if (off >= frames_per_row) { off = 0; piece_x = nx; piece_y = ny; }
            continue;
        }

        /* Resting. Sit exactly on the grid, then hand over to the bitmap. */
        off = 0;
        piece_show(0);

        /* Landed: hand the piece over from sprites to the bitmap. */
        VIC[0x15] = 0x00;
        {
            unsigned char i;
            signed char plo = MAX_Y, phi = 0;
            for (i = 0; i <= 3; ++i) {
                signed char gy = (signed char)(piece_y + rel_y[i]);
                draw_cell((signed char)(piece_x + rel_x[i]), gy, BALL_C64[img[i]]);
                if (gy < plo) plo = gy;
                if (gy > phi) phi = gy;
            }
            piece_stamp();
            ++landed;
            sfx_play(SFX_DROP);
            /* Let the lock actually sound. resolve() can fire the match effect
             * within microseconds of this, and effects share voice 3 -- without
             * a beat here the lock is replaced before a single frame has
             * ticked, so it is never heard on a landing that matches. Four
             * frames is the length of the lock effect. */
            wait_frames(SFX_LEN[SFX_DROP]);
            resolve(plo, phi);
        }
        check_advance();
        if (level != prev_level) {
            prev_level = level;
            set_fall_speed();       /* the ramp, :433 */
            sfx_play(SFX_LEVEL);
        }
        publish();
        hud_update(level, skill, ball_count, score);

        /* Two ways to end: the danger zone is occupied, or the next piece has
         * nowhere to go.
         *
         * The first leaves the piece that did it already stamped into the
         * bitmap. The second has nothing on the board to point at, because the
         * piece never got placed -- hence the flag. The two disagree about where
         * the danger is, which is why the second happens at all: the spawn root
         * is row 3 (:170) but a shape reaches below and either side of it, into
         * cells check_game_over() never inspects. */
        if (check_game_over()) { end_of_game(0); break; }
        {
            unsigned char fits = next_piece();
#if TEST_NOROOM
            /* Force the failure AFTER the spawn, not instead of it -- the piece
             * being shown is the one next_piece() just made. */
            if (landed >= 3) fits = 0;
#endif
            if (!fits) { ++no_room; publish(); end_of_game(1); break; }
        }
    }
    }
}
