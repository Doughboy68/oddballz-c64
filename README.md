# Oddballz C64

A Commodore 64 port of Oddballz, the 1992 Borland Pascal hex puzzle game.

This is a **new project with its own history**. The WebGL/Three.js remake at
`oddballz-hd` is *not* forked or copied into it — it serves as a
**read-only reference spec and behavioural oracle**. Its engine (section 2 of
`oddballz-app.js`) is integer-only and has been validated over hundreds of
simulated drops, so it can be used to cross-check C64 output on identical board
states.

## Status

**Playable and feature-complete for what this port set out to be.** The engine is
ported and self-tested (34 cases, 34 passing). The board renders in multicolour
bitmap over a dim socket playfield, the falling piece rides hardware sprites,
matches and cascades animate, rows clear with a flash, the HUD sits in the four
corner wedges, and SID effects fire on the events that matter.

- **Both game modes**, chosen from the title: colour match and Row Build.
- **Title, pause and game-over screens.** Pause on **P** blanks the playfield so
  it cannot be studied, and offers resume or quit.
- **Fall speed calibrated against the original** and against the machine —
  1.0 grid rows a second at level 1, ramping to 2.6, on both PAL and NTSC.
- **The drawn board is a regular hexagon**, nine cells a side, with the top three
  rows masked so pieces enter out of black.
- **Drawing is fast.** Title 4.6s → 0.31s, pause 0.10s, board 1.11s → 0.20s. A
  game now starts in about half a second of drawing against roughly five and a
  half.

Missing on purpose: background music (built, then dropped — see **Sound**), high
scores, attract mode, and a graphic background (see **Next steps**).

Never run on real hardware. Everything here is VICE.

### How much of it is assembly

Three inner loops are hand-written 6502; everything else is C. Measured from the
linker map and the sources:

| | compiled bytes | share | source lines |
|---|---|---|---|
| C (`game.c` and its headers) | 19,403 | 87% | 1,781 |
| assembly (`stamp.s`, `blit.s`, `sockets.s`) | **1,164** | **5%** | 621 |
| cc65 runtime library | 1,622 | 7% | — |

The assembly is a quarter of the source lines but a twentieth of the binary,
which is the whole story about cc65: **1,781 lines of C compile to about 11 bytes
a line, 621 lines of assembly to about 1.9.** That ~6× density difference is the
same factor that made drawing slow in the first place.

The split follows the work, not the language: those 1,164 bytes run thousands of
times per screen, while the 19 KB of C — the ported engine, the game loop,
keyboard, sound, screens — runs a handful of times per landing, where cc65 being
six times fatter costs nothing anyone can perceive.

## Building and running

```powershell
.\tools\build-game.ps1          # builds AND checks the memory map
.\tools\make-disk.ps1           # builds, then writes dist\oddballz.prg and .d64
.\tools\run-vice.ps1 -Prg .\build\game.prg -Cycles 250000000   # headless, screenshots
```

**Always build through `tools/build-game.ps1`.** It reads the linker map and
fails the build if BSS reaches the VIC's memory — see **The memory trap** below
for why that check exists.

### On a real C64

`tools/make-disk.ps1` writes both distributables to `dist/`:

- **`dist/oddballz.prg`** — a genuine C64 program. cc65's c64 target emits the
  standard `$0801` load address and a BASIC stub, so anything that can send a
  PRG (Kung Fu Flash, Ultimate-II, a PC-to-C64 link) runs it directly.
- **`dist/oddballz.d64`** — the more universal container.

An SD2IEC or Ultimate cartridge mounts the `.d64` as drive 8 and the machine
loads from it exactly as it would from a 1541:

```
LOAD"ODDBALLZ",8,1
RUN
```

**The `,1` matters.** Without it the KERNAL relocates the file to the BASIC
start instead of honouring the load address in the file, and the program lands
in the wrong place and crashes.

Requirements on the real machine: a stock C64 or C128 in 64 mode, **PAL or
NTSC** — the fall speed calibrates itself to whichever it finds, see **Fall
speed, and PAL vs NTSC**. Nothing here depends on a REU, a particular SID
revision, or a fast loader. It has been verified under VICE only; a real machine
is untested.

**Loading takes about 50 seconds and that is correct.** It is 22 KB over the
serial bus at 1541 speed, which is what a real drive costs. Under VICE the
emulator hides it by warping through the load — if it feels slow there, either
warp-on-autostart is off (Settings → Speed) or true drive emulation is on. Turning
TDE off drops the emulated load from ~55 seconds to ~10:

```bash
x64sc +drive8truedrive -autostart oddballz.d64
```

Safe here, since this is a plain PRG on a plain image with no fastloader for TDE
to matter to. A fastloader would fix it on real hardware too, but that is its own
piece of work.

### Controls

| | |
|---|---|
| **1** / **2** | at the title: colour match, or Row Build |
| **J** / **L** | move left / right |
| **I** / **K** | rotate counter-clockwise / clockwise |
| **F** | cycle the piece's colours (no-op in Row Build) |
| **X** / **Y** | flip about the x / y axis |
| **SPACE** | zip — a latch, one press commits the drop |
| **P** | pause — wipes the screen, then **P** resumes or **E** ends the game |

The title screen lists all of these, so nothing here is knowledge you have to
bring to the machine.

The pause wipes the playfield rather than drawing over it. A pause you can read
the board through is a free planning move, which is not what a pause is for.
Ending the game lives on that screen rather than on a key of its own, which gives
the "are you sure" for free: stopping the game is a deliberate act, and the
alternative is sitting right next to it.

Quitting from the pause goes straight back to the title — no GAME OVER, no score
screen, and it does not count as a game over. You chose to stop, so the only
thing you are waiting for is the title, and skipping the board repaint on that
path is what makes it immediate.

## Text drawing, and what speeding it up actually bought

Text used to be a loop over `plot()`, one pixel at a time. Measured with
`TEST_TIMING=1`, the title screen took **4,561,385 cycles — 4.6 seconds**.
ODDBALLZ alone was 1,848,973 cycles for 693 plotted pixels: about **2,670 cycles
a pixel**, against roughly 60 for `stamp.s`, the assembly ball blit in this same
repo.

`plot()` is a fully general per-pixel routine and pays for it every time: two
16-bit multiplies for the addresses, arbitration for one of the cell's three
colour slots, then a read-modify-write of one bitmap byte — all reached through
cc65's stack calling convention. At triple size every font pixel became **nine**
of those calls.

The rewrite in `hud.h` fixes the two structural problems. A character cell holds
one colour per slot, so the slot is resolved **once per cell** rather than once
per pixel; and four logical pixels share a bitmap byte, so whole bytes are OR'd
in rather than each pixel read-modify-writing the same byte four times. `EXPAND`
and the `ROW_ADDR`/`ROW_CELL` tables in `stamp.h` already existed for this — the
ball blit hit the same wall first.

Three things mattered, in increasing order of payoff:

- **Table out the variable shifts.** `1 << (3 - (px & 3))` and
  `(bits >> (2 - c)) & 1` shift by a *variable* amount, which the 6502 cannot do
  and cc65 turns into a loop or a helper call. `BIT3`/`BIT4` make them indexed
  loads.
- **Index the row tables with an `unsigned char`.** All 200 scanlines fit in a
  byte, so the lookups become `lda tbl,y` instead of 16-bit indexed addresses.
- **Precompute the size-1 glyph masks.** 93 of the title's 104 glyphs are
  unscaled, and at scale 1 the mask pass cost more than the drawing. There are
  only 36 glyphs and 4 alignments, so `font_init()` works all of it out at
  startup into 1,440 bytes.

That got 3.7x, and made it obvious the rest was cc65 rather than algorithm: about
a thousand byte writes still costing 700-1,200 cycles each. So the inner loop
went to assembly in `blit.s` — and then the C *around* it became the cost, five
arguments pushed on cc65's software stack twice per glyph. So the glyph loop went
down too, and C now hands over a whole string at once.

| | pixel-at-a-time C | byte-at-a-time C | assembly | |
|---|---|---|---|---|
| title screen | 4,561,385 | 1,238,832 | **300,983** | **15.2x** — 4.6s to 0.31s |
| ODDBALLZ alone | 1,848,973 | 465,166 | 105,865 | 17.5x |
| pause screen | ~1,900,000 | 483,612 | **99,565** | **19x** — 0.10s, five frames |
| playfield | 1,090,001 | 1,090,001 | 1,090,001 | untouched, see below |

Four things carried it, in increasing order of payoff:

- **Table out the variable shifts.** `1 << (3 - (px & 3))` shifts by a *variable*
  amount, which the 6502 cannot do and cc65 turns into a loop or a helper call.
- **Index the row tables with an `unsigned char`.** All 200 scanlines fit in a
  byte, so the lookups become `lda tbl,y`.
- **Precompute the glyph masks.** At size 1 the mask pass cost more than the
  drawing; `font_init()` works all of it out at startup. Note the asymmetry in
  how the two tables are keyed: `SM0`/`SM1` are per *glyph* and cost 1,440 bytes,
  while `S3` is per *font row pattern* — a row is only ever one of eight — and
  covers every glyph in 128.
- **Hand over whole strings.** The advance is exactly one byte column, so the
  alignment within a byte is the same for every glyph in a string and the column
  simply increments. `SMANY0`/`SMANY1` then skip a column with nothing in it,
  which is the right-hand column of most glyphs and both columns of a space.

**15x, not the 20-40x first estimated here.** That estimate counted operations
and assumed the cost per operation would hold; it does not. What is left is
`docol`'s inner loop, which recomputes the bitmap pointer from the row tables on
every scanline — within a character row the next scanline is simply the next
byte, so walking a pointer instead is worth perhaps another 25%. It has not been
done because a tenth of a second is already below the point where you see it
happen.

## The playfield: an algorithm change, not a language one

The board draw was never part of the above — `draw_cell()` already goes through
`stamp_ball_fast`, so it was assembly all along. 1,090,001 cycles for 271 sockets
is about 4,000 each, which is simply what the ball blit costs, 271 times.

What `sockets.s` changes is what gets re-derived. Everything `stamp.s` works out
per ball is constant for a row of sockets: the colour is the same board-wide, the
colour slot is fixed by the grid row's parity, the eleven pairs of mask bytes are
the same because every socket is the same shape, and the scanlines are the same
because the row is level. So a scanline's two packed bytes are computed **once**
and then stored straight across the row. Sockets sit 8 logical pixels apart and
are 7 wide on two byte columns, so they never touch — the pointer just steps 16
bytes per socket.

Plain stores, not read-modify-write: the board is drawn onto a bitmap
`vic_bitmap_mode()` has just cleared, so there is nothing underneath to preserve.
And the hexagon's shape is worked out once into `pf_bc`/`pf_n`/`pf_y0` rather
than by scanning all 441 grid positions with `in_board()` on every draw, which
was a quarter of what was left.

| | before | after | |
|---|---|---|---|
| playfield | 1,090,001 | **198,289** | **5.5x** — 1.11s to 0.20s |

So a game now starts in about half a second of drawing all told, against roughly
five and a half before any of this.

### The top three rows are masked

The board the engine plays on is 20 rows, and the top three hold 6, 7 and 8 cells
— which made the drawn hexagon irregular, a six-cell top edge against nine
everywhere else. The original masks those rows from view, and a piece is not
drawn until it reaches the fourth row.

Nothing is lost by hiding them, because nothing can ever come to rest there.
Every cell in rows 0–2 lies inside `check_game_over()`'s danger zone — rows 0–3
of columns 4–12, and those rows reach no further right than column 11 — so a ball
arriving in one ends the game that instant. They exist only for a piece to enter
through.

So `in_board()` is unchanged and `FIELD_TOP`/`on_field()` sit alongside it,
consulted only by the drawing. The board the engine plays on, the danger zone and
the spawn point are all exactly as they were; the drawn hexagon is now regular,
nine cells a side, with the piece coming in out of black instead of sliding down
unusable playfield.

The mask applies to the piece too. Four independent sprites make that per **ball**
rather than per piece, so a shape enters one ball at a time. The one deliberate
exception is the "no room" game over, which shows all four past the mask — the
point of showing that piece is that it had nowhere to go, and half of it hidden
would not say so.

`ORIGIN_Y` centres the **drawn** rows rather than all twenty, so the hexagon sits
in the middle of the frame with the same black margin above as below — and the
margin above is where a piece comes in from. That makes `CELL_Y` negative for the
masked rows, which is harmless: nothing draws them, sprite positions are computed
in ints before the VIC's offset is added, and the blit clamps them away anyway.
The lift is safe against the HUD because the board's topmost drawn row starts at
character column 11 and its widest reaches column 30, while the wedges are
columns 0–5 and 31–37.

### Fall speed, and PAL vs NTSC

The original states fall speed in grid **rows per second** and ramps it with the
level (`oddballz-app.js:433`):

```
baseSpeed = min(2.6, 1.0 + (level - 1) * 0.08)
```

Frames per row is the machine's field rate divided by that, which is why it
cannot be a constant. PAL runs 50.125 fields a second and NTSC 59.826, so the
same frame count falls **19% faster** on an NTSC machine. `set_fall_speed()`
reads the KERNAL's own PAL/NTSC flag at `$02A6` — set at reset, long before we
run — and calibrates from it:

| | PAL | NTSC | rows/sec |
|---|---|---|---|
| level 1 | 50 frames/row | 60 | 1.0 |
| level 11 | 28 | 33 | 1.8 |
| level 21+ (capped) | 19 | 23 | 2.6 |

This was a flat 24 frames a row with no ramp at all, which on PAL is 2.08 rows a
second — the first level fell at roughly twice the speed it should have.

The tables that interpolate the sprite between rows are rebuilt whenever the
level changes. That is safe to do mid-game because the level can only change on a
landing, when the interpolation offset is already back at zero.

Everything else timed in frames — the lock and match effects, the cascade
settle — still runs 19% quicker on NTSC. That is cosmetic and has been left
alone; the fall speed is the one that decides how hard the game is.

### Build-time switches

All default off/sane; nothing below is needed for a normal build.

| | |
|---|---|
| `USE_MUSIC=1` | the chiptune, dropped from the default build |
| `USE_BACKDROP=1` | the tiled ODDBALLZ wallpaper |
| `BG_COL`, `BORDER_COL`, `EMPTY_COL`, `BACKDROP_COL`, `HUD_COL`, `HUD_LABEL_COL` | palette |
| `SKIP_TITLE=1` | auto-continue the title and end screens — headless runs cannot press a key |
| `SKIP_MODE=0` | which mode a headless run plays |
| `TEST_GAMEOVER=1` | show the end screen immediately |
| `TEST_ROW=1` | pre-fill the bottom row so a clear fires on the first landing |
| `DEMO_STEER=1` | an untouched piece walks itself toward a random column |
| `TEST_PAUSE=1` | pause three landings in; `=2` holds it up, `=3` takes the quit branch |
| `TEST_TIMING=1` | time the title draw into `$035E`, and ODDBALLZ alone into `$0362` |
| `TEST_NOROOM=1` | force the "no room for the next piece" ending, which is the hard one to reach on purpose |

The last four exist because a screenshot cannot press a key, and several things
— the title clear, the end screen, a row clearing — are otherwise unreachable
without waiting on luck.

## Toolchain

| Tool | State |
|---|---|
| VICE 3.10 (`x64sc`) | installed via `winget install VICE-Team.VICE.GTK3` |
| Java 8 | pre-existing, `1.8.0_481` |
| KickAssembler 5.25 | `vendor/kickassembler/KickAss.jar`, runs on the Java 8 above |
| cc65 (snapshot) | `vendor/cc65/bin/` — `cc65`, `ca65`, `ld65`, `cl65` |

`vendor/` is gitignored — those are fetched, not authored. To recreate it:

```powershell
# KickAssembler (1,284,374 bytes)
Invoke-WebRequest https://theweb.dk/KickAssembler/KickAssembler.zip -OutFile ka.zip

# cc65 (~24.5 MB). NOTE: the URL matters. The documented SourceForge link
# (sourceforge.net/projects/cc65/files/...) and the cytranet mirror both serve
# a ~139 KB HTML interstitial instead of the zip. Only the master mirror with
# ?viasf=1 returns real bytes. Check for the "PK" magic before trusting it.
Invoke-WebRequest 'https://master.dl.sourceforge.net/project/cc65/cc65-snapshot-win32.zip?viasf=1' -OutFile cc65.zip
```

VICE lives at:

```
C:\Users\Brian\AppData\Local\Microsoft\WinGet\Packages\VICE-Team.VICE.GTK3_Microsoft.Winget.Source_8wekyb3d8bbwe\GTK3VICE-3.10-win64\bin\
```

winget added it to PATH, but **the harness shell does not pick that up** — the
scripts in `tools/` hardcode the full path deliberately.

## The verification loop

Claude cannot see native Windows GUI windows, so VICE's own display is invisible
to it. Two channels work around that, and both are verified:

### 1. Still frames — `tools/run-vice.ps1`

Runs a `.prg` for a bounded number of cycles, exits, and dumps a PNG that Claude
can read visually. Good for layout, colour, board geometry, sprite placement.

```powershell
.\tools\run-vice.ps1 -Prg .\build\loop-test.prg
```

Flags confirmed present in VICE 3.10: `-autostart`, `-limitcycles`,
`-exitscreenshot`, `-warp`, `-autostart-warp`, `-binarymonitor`.
About **985,248 cycles = 1 second** of emulated PAL time. VICE exits with code 1
when `-limitcycles` trips; that is normal, check for the PNG rather than the
exit code.

### 2. Memory assertions — `tools/peek.ps1`

Reads C64 memory out of a *running* emulator over the binary monitor (TCP
127.0.0.1:6502). This is the C64 analogue of inspecting `window.oddApp` in the
JS version: tests can assert on real engine state instead of on pixels.

```powershell
.\tools\peek.ps1 -Prg .\build\loop-test.prg -Start 0x0400 -End 0x040F
```

### What neither channel can do

**Smoothness, feel and timing cannot be verified this way.** Those need Brian's
eyes on a real screen. He is the primary tester for anything motion-related, and
iteration will be slower than it was on the web version.

## The loop test

`tools/make-loop-test.ps1` hand-assembles a 60-byte `.prg` (there is no
assembler yet) that fills the screen with PETSCII `$51` — a filled circle — in
yellow on blue. A screen of balls is instantly recognisable in a screenshot, so
if the PNG looks right the entire pipeline is working.

Both channels agreed: the PNG showed a full 40×25 grid of yellow balls, and
`$0400-$040F` read back as `51` sixteen times.

`spikes/toolchain/` then repeats the same fill through each real toolchain, in
different colours each time so a screenshot proves *that* build ran rather than
a stale one:

```powershell
& "C:\Program Files (x86)\Common Files\Oracle\Java\java8path\java.exe" `
    -jar .\vendor\kickassembler\KickAss.jar .\spikes\toolchain\fill.asm -o .\build\fill-kick.prg
.\vendor\cc65\bin\cl65.exe -t c64 -O .\spikes\toolchain\fill.c -o .\build\fill-cc65.prg
```

Both produced a correct full-screen fill.

### Gotcha: cc65 changes the character set behind your back

The cc65 build rendered `$51` as a white **`q`**, not a filled circle, while the
KickAssembler build rendered the circle correctly — from the identical screen
byte. Reading `$d018` out of both confirms why:

| Build | `$d018` | Char base | Charset |
|---|---|---|---|
| KickAssembler | `$15` | `$1000` | uppercase / graphics |
| cc65 | `$17` | `$1800` | lowercase |

**cc65's C64 runtime switches to the lowercase charset at startup.** The PETSCII
graphics glyphs — including every ball-ish character — only exist in the
uppercase/graphics set. Anything drawn with the cc65 runtime must set `$d018`
back to `$15` first, or use a custom charset (which the real port will do
anyway).

This is a good example of why the memory-read channel matters: the screenshot
showed *that* something was wrong, but `$d018` showed *what*.

## Design decisions carried over from the feasibility assessment

- **Landed board** → character mode or multicolour bitmap; redraw only changed
  cells.
- **Falling piece** → hardware sprites (4 balls = 4 sprites). Position is a
  register write, so pixel-smooth motion is nearly free; this should end up
  *smoother than the 1992 original*. On landing, stamp into screen/colour RAM
  and hide the sprites.
- **Cascades are the awkward part.** Freed balls are board cells, not sprites.
  Either animate in whole-cell steps (reads as falling, not pixel-smooth) or
  promote up to 8 to sprites and queue the rest. *Settled: whole-cell steps, and
  they read fine.*
- **Screen budget.** Multicolour bitmap (160×200 logical, 2:1 pixels ≈ square
  cells) with balls ~8×7 logical → board ~136×140, leaving ~60 rows for a HUD.
  20 rows of 16×16 balls does **not** fit; the board or the balls must shrink.
- **Dropped:** 3D spheres, PBR shading, particle explosions, starfield. Bursts
  become a flash/shrink animation.
- **Gained:** SID audio beats the 1992 PC version, and per-cell colour is nearly
  free on the VIC-II.

## Match rules — preserve exactly

- Parallel axes, dirs `[4, 0, 3]` → need **5** in a row.
- Perpendicular axes, dirs `[11, 9, 10]` → need **3** in a row.
- Dir 9 = `(x-1, y-2)` is **vertical** on screen.
- A connected 4-ball piece can never lie along a perpendicular axis.

## Next steps

Everything the feasibility assessment listed as unknown is settled, and the game
plays both modes at the original's speed. What is left is optional:

- **Verify on real hardware.** Only VICE has run this. Timing was the likeliest
  difference and is now the one thing deliberately calibrated for both machines —
  the game paces on raster line 250 rather than an interrupt, and reads the
  KERNAL's PAL/NTSC flag to work out what a frame is worth.
- **A graphic background** for the border and the four corner wedges, hexagon
  knocked out. Those cells hold no balls, so all three per-cell colour slots are
  free and nothing ever erases over them — a genuinely three-colour-per-cell
  image with no masking problem. Convert to the C64's scheme first (Multipaint or
  similar) and `memcpy` pre-packed bytes rather than plotting per pixel. The
  clipping and colour-RAM groundwork is already in `hud.h` behind `USE_BACKDROP`.
  Inside the hexagon is not worth it: every cell there is already an opaque ball
  or socket, so an image would only show through the gaps, and erasing a ball
  would punch a hole in it that something would have to restore.
- **Interrupt-driven music.** The chiptune exists but was dropped: run from the
  main loop it stalls whenever the game does not wait on a frame. Doing it
  properly needs a raster ISR and a pattern format — a project in itself.
- **Hand-drawn ball art.** `ball_halfwidth` at rx 4 / ry 5 gives only widths
  7, 5, 3, 1, which puts a one-row bulge at the ball's equator. A hand-drawn
  mask costs nothing at runtime — it is a table either way.
- **High scores, attract mode.**
- **Scale the cosmetic frame counts for NTSC.** The fall speed is calibrated, but
  the lock and match effects and the cascade settle are still raw frame counts,
  so they run 19% quicker on an NTSC machine.

Render optimisations still on the table, none of them currently needed: `docol`
in `blit.s` recomputes the bitmap pointer from the row tables every scanline when
within a character row the next scanline is simply the next byte (worth perhaps
25%); unroll the ball blit's scanline loop; inline `setslot` rather than `jsr`.

## Board geometry — settled

`tools/board-geometry.ps1` mirrors `isInBoard()` and the half-cell shear from
the JS engine and prints an ASCII map. For the classic 9-wide preset:

- **238 live cells**, matching the assessment.
- **17 balls** at the widest (row 11), **20 rows** tall.
- Those 20 rows are the board the *engine* plays on. The board that is *drawn* is
  the bottom 17 of them — 9,10,…,17,16,…,9 — which is a regular hexagon, nine a
  side. The top three are the entry area and are masked. See **The top three rows
  are masked**.
- The shear alternates strictly by **row parity** — even rows aligned, odd rows
  half a ball to the left. This is what forces multicolour bitmap over character
  mode: in char mode an odd row puts two differently-coloured half-balls in one
  8px character cell, and hires char mode has only one colour per cell.
- Max **6 ball colours** (`levAttr`, reached at level 12).

### Correction to the feasibility assessment

The assessment assumed multicolour's 160×200 gives roughly square cells
("balls ~8×7 logical → board ~136×140"). It does not. On a 4:3 screen a 320×200
physical pixel is already 0.833 wide per unit tall, and an MC logical pixel is
two of those — about **1.667 wide per unit tall**. So a ball `PITCH_X` logical
px across is `1.667 × PITCH_X` units wide visually, and true hex row spacing is
0.866 of that:

```
row pitch ≈ 1.443 × PITCH_X
20 rows ≤ 200 px  →  PITCH_X ≤ 6.9
```

**All 20 rows at correct hex proportions forces balls down to 6 logical px
across**, not 8.

### What the variants showed

`tools/build-board-variants.ps1` renders the real board at different sizes.

| | pitch | rows | result |
|---|---|---|---|
| A | 6×9 | 0–19 | correct proportions, full board, small balls, **no colour clash** |
| B | 8×9 | 0–19 | balls taller than the row pitch — heavy overlap, unusable |
| C | 8×11 | 4–19 | bigger, rounder balls, but **visible colour clash** |
| D | 8×11, ball 10 tall | 4–19 | as C with the overlap removed — nearly clean |
| **E** | **8×10, ball 10 tall** | **0–19** | **chosen.** D's ball size, full board, fits 200 px exactly |

### Rows 0–3 must be drawn — variant D was not safe

Checked before building on it, and the answer was no. Rows 0–3 are not dead
space, they are the **danger zone**:

```js
checkGameOver() {                                  // oddballz-app.js:1053
  for (let x = 4; x <= 12; x++)
    for (let y = 0; y <= 3; y++)
      if (... this.ballMap[x][y].bzMap !== 0) { this.endGame = true; ... }
}
```

It runs after every landing (`:501`), and pieces spawn at `y = 3`
(`startPos`, `:262`). So hiding rows 0–3 would end the game with no visible
cause and make every new piece materialise mid-board. `buildRowTables()`
excluding them from *drop targets* is a different thing from them being unused.

> **Later, and not a contradiction.** Rows 0–2 are now masked from view — see
> **The top three rows are masked**. That is a different thing from what variant D
> proposed. D would have removed the rows from the *board*, so they could not hold
> a ball and the danger zone would have had nothing to detect. The mask leaves
> `in_board()`, the danger zone and the spawn point untouched and skips only the
> socket painting, and row **3** — the spawn row — is still drawn. A ball that
> does reach a masked row is still drawn where it lands, because reaching one is
> game over and that is precisely the moment to show it. The "end the game with no
> visible cause" worry was well founded, though — it turned up later as its own
> bug, on the other game-over path, where the piece that had nowhere to go was
> never drawn at all.

Variant **E** keeps D's ball size and restores the full board by compressing the
row pitch from 11 to 10: 20 rows × 10 px = exactly 200. Costs a ~13 % vertical
squash against true hex spacing (11.5), which is far less visible than B's
overlap, and it means **the HUD cannot sit below the board**. The board is 136
of 160 logical px wide, so the HUD goes in the 24-logical-px (48 physical)
strip down the right-hand side.

**Colour clash is geometry-driven, not fundamental.** Multicolour bitmap gives
three ball colours per 8×8 attribute cell over a shared background, which is
enough *provided a cell is never touched by four balls*. That happens when
`BALL_H > PITCH_Y`, because the cell then meets two ball rows as well as two
ball columns. Keeping `BALL_H <= PITCH_Y` removes almost all of it.

The renderer allocates colour slots per cell and deliberately leaves any
genuine clash visible rather than hiding it, so the screenshots are honest.

### cc65 speed — a first data point

The static render needed **well over 60 million cycles** (60+ seconds of
emulated time) to draw 238 balls. That is not a verdict on cc65 — this code uses
`long` multiplication per scanline, which is close to worst case on a 6502, and
a real renderer would use a precomputed ball bitmap and a stamp routine. But it
does confirm the animation spike has to measure rather than assume.

Because of this, `render-board.c` turns the border **white** only after the last
ball is drawn. A screenshot fires at a fixed cycle count, so a half-drawn board
and a wrongly-drawn board otherwise look identical — the flag tells them apart.
That mistake cost a full debugging detour here.

## Frame budget — measured

`spikes/piece/falling-piece.c` puts the piece on four hardware sprites over the
empty board and times the two costs that matter. A **PAL frame is 19,656
cycles**.

| Work | Naive C | Optimised C | Assembly | Frame |
|---|---|---|---|---|
| Reposition 4 sprites (per frame) | 10,671 | **2,534** | — | 12.9 % |
| Stamp 4 balls on landing | 1,994,071 | 50,300 | **14,628** | **74.4 %** |

**A four-ball landing now fits inside a single PAL frame**, 136× faster than
where it started. Per ball it is 3,657 cycles, so a twenty-ball cascade costs
~73,140 — 3.7 frames, about 74 ms, against the quarter-second stall the C
version implied. That is comfortably spreadable across frames.

### Getting the stamp down 39.6×

| Change | Cycles |
|---|---|
| `draw_ball` — per-pixel ellipse, 32-bit multiply per scanline | 1,994,071 |
| Precomputed mask + expand tables (`stamp.h`) | 113,090 |
| Flatten 2-D arrays, `-Oirs` | 78,656 |
| Moving pointer per scanline; claim colour slots per char row | **50,300** |

Each step attacked a different cost, and the last two were **not** algorithmic —
they were about staying off cc65's slow paths:

- `a[i][j]` compiles to a real multiply. Flattening `EXPAND` to `(slot << 4) |
  mask` keeps every subscript under 64, so it becomes an 8-bit indexed load.
- Consecutive scanlines inside one character row are **consecutive bytes** in
  the bitmap. Walking a pointer replaces a 16-bit array subscript, which was
  where most of the remaining time went.
- A ball spans 11 scanlines but only 2–3 character rows, so colour slots are
  claimed ~6 times per ball instead of 22, lazily so a ball never burns a slot
  in a cell it does not touch.

Verified pixel-exact against `draw_ball` by stamping over the board and
comparing shapes — see `shots/zoom-blit.png`.

### The assembly blit — `spikes/common/stamp.s`

**ca65, not KickAssembler.** ca65 is cc65's own assembler, so it links natively
against the C object and shares its symbols. KickAssembler builds standalone
`.prg` files and cannot do that; it stays relevant only if the project ever goes
fully assembly.

Parameters go through globals (`stamp_bc`, `stamp_y0`, `stamp_col`) rather than
arguments — pushing three values onto cc65's C stack costs more than the routine
saves. Build with:

```powershell
.\vendor\cc65\bin\cl65.exe -t c64 -Oirs .\spikes\piece\falling-piece.c .\spikes\common\stamp.s -o .\build\falling-piece.prg
```

Two things bite when writing 6502 under cc65, both worth knowing in advance:

- **Branch range.** The draw body is well over 127 bytes, so the loop-back and
  the bounds-check skip both had to become `jmp` behind an inverted branch.
- **The zero page is full.** cc65's c64 runtime uses all of it — adding a single
  byte to the `ZEROPAGE` segment overflows the area. The hot variables are
  therefore aliased onto cc65's own scratch symbols (`tmp2-4`, `ptr4`, `sreg`),
  which are safe to clobber in a leaf routine that never calls back into C, and
  the cold ones live in `.bss`.

The assembly also fixes a latent correctness bug carried over from the C: colour
RAM is only four bits wide and its top nibble reads back as whatever was last on
the bus. The C version compared `COLRAM[cell] == col` whole, which happens to
work under VICE but would misbehave on real hardware. The assembly masks with
`#$0F`.

Verified pixel-identical to the C version by screenshot — `shots/zoom-asm.png`.

### Verdict: assembly is required for the render inner loop

Confirmed by measurement, not prediction. The C version bottomed out at 50,300
cycles — 2.56 frames — with the algorithm already reduced to ~22 byte writes and
~6 slot claims per ball. The same algorithm in assembly runs in 14,628. The gap
was cc65 code generation, exactly as expected, though the prediction of 600–800
cycles per ball was optimistic: the real figure is 3,657, and the `jsr claim`
per cell plus the un-unrolled scanline loop are where it goes.

- **C is fine for game logic** — the engine port, match scanning, level state.
- **The bitmap render path needs assembly**, and now has it.

That verdict held twice more, and both times the same way round: an algorithm
change in C first, then assembly for what remained. Text went 4,561,385 → 300,983
cycles and the empty board 1,090,001 → 198,289. Each time the C rewrite bought
about 4×, and each time what was left was cc65 rather than algorithm — a byte
write costing 700–1,200 cycles compiled against 30–60 hand-written. The three
routines total 1,164 bytes, about 5% of the binary. See **Text drawing** and
**The playfield**.

### Ball art should be hand-drawn, not computed

`ball_halfwidth` at rx 4 / ry 5 yields only widths 7, 5, 3, 1, which puts a
one-row bulge at the ball's equator. It is consistent between drawn and stamped
balls, so it is not a rendering fault — but a hand-drawn 8×11 mask would look
considerably better and costs nothing at runtime, since the mask is a table
either way.

### How this is measured

Two channels, because neither alone is enough.

**Border bands.** The border changes colour around each measured section, so
the band's height *is* the cycle count. Raster timing becomes visible in a
screenshot — the one profiling technique that survives not being able to see
the emulator window. White = per-frame work, red = landing stamp.

**CIA2 cycle counter.** Timer A counts system cycles, timer B counts A's
underflows, giving a 32-bit count that `peek.ps1` reads out of the running
emulator at `$0350`. Timer A alone wraps at 65,536 cycles — under four frames —
and the stamp overruns that by a factor of thirty. The KERNAL's 60 Hz IRQ is
silenced first so it cannot inflate the readings.

### What the numbers say

**Sprite motion works and is affordable.** Position is a register write, so the
hardware cost is nil; everything measured is cc65's arithmetic around it.
Replacing `CELL_X`/`CELL_Y` — a 16-bit multiply and divide each — with per-row
tables cut it 3.1×. At 17.6 % of a frame it is usable, though 3,468 cycles is
still steep for what is ultimately about twenty register writes.

**The landing stamp is the real problem, and it is algorithmic rather than a
language limit.** `draw_ball` walks the ellipse pixel by pixel, recomputing its
half-width per scanline with 32-bit multiplication — close to worst case on a
6502. But the geometry is far kinder than that: ball centres land on multiples
of 4 logical px, so **a ball occupies exactly two byte-aligned columns**, and a
stamp is 10 scanlines × 2 bytes = 20 byte writes plus a handful of colour cells.
That is ~80 writes for a four-ball piece. The strong expectation is that a
precomputed blit brings this inside one frame **in C**, without dropping to
assembly — but that is a prediction, and the next spike should measure it rather
than assume it.

## Cascades — `spikes/cascade/cascade.c`

Fall rules ported exactly from the JS engine:

```js
supported(x,y) = NOT (down-left empty AND down-right empty)   // :778
down-left  = dir 2 = (x,   y+1)
down-right = dir 5 = (x+1, y+1)
```

A ball **rests on two** and only falls when both diagonals are in-board and
empty, so it never slides into a single-width gap — which is why the board keeps
its characteristic voids. When it does fall both sides are free by definition,
so the choice is purely `flipGate`, alternating globally, and that is what
produces the zig-zag settle.

`checkGaps()` settles each ball all the way down in one pass; here each pass
advances every unsupported ball by **one cell**, which is what makes it an
animation. Scanning bottom-to-top while moving downward guarantees a ball cannot
move twice in a pass.

### The bottleneck was not what I expected

| Change | Worst step | Idle scan |
|---|---|---|
| `in_board()` per cell | 241,751 | 195,973 |
| Out-of-board sentinel in the map | 157,564 | 113,222 |
| Pointer-walk the rows | 120,223 | 75,229 |
| Scan only the disturbed rows | 40,257 | 12,814 |

Blitting was never the problem — **the scan was**. A settled board cost 195,973
cycles, ten frames, purely to confirm nothing needed doing. Three fixes:

- Out-of-board cells hold `0xFF` instead of `0`, so "in board and empty"
  collapses to one array read and `in_board()` leaves the inner loop entirely.
- Rows are walked with a moving pointer; `p[BMW]` is a constant offset compiling
  to `ldy #32 / lda (ptr),y`, where a 16-bit subscript cost 333 cycles a cell.
- Only the rows a cascade actually disturbs are scanned. A ball moves one row
  per step, so the window starts just above the hole and walks upward on its
  own. This alone was 3×.

### Worst case, and the cap

With a deliberately oversized 27-ball clear — far bigger than the 3 or 5 a real
match clears — 14 balls wanted to move in one step, at 128,697 cycles. Moves per
step are therefore capped (`MAX_MOVES`, 6); leftovers are picked up next step,
and a cascade that staggers slightly reads as natural. A real single-match clear
settles in **one step**.

### Colour clash is now structurally impossible

Slots are assigned **statically by the ball's grid-row parity**, not allocated
first-come-first-served:

`
even ball rows -> slot 1 -> screen hi nibble -> bit pattern 01
odd  ball rows -> slot 2 -> screen lo nibble -> bit pattern 10
`

The geometry makes that sound. Balls are byte-aligned and 8 logical px apart, so
a cell holds exactly one ball per ball-row; and a cell spans 8 scanlines while
balls sit on a 10 px pitch, so it can only ever meet **two consecutive** ball
rows -- which are always opposite parity. One even ball and one odd ball per
cell, each with its own nibble, permanently. Colour RAM (pattern 11) is left
free for effects.

The earlier dynamic allocator released a slot only when a cell became completely
empty. That was survivable until every empty cell was drawn as a dim socket, at
which point no cell was ever empty again, slots leaked permanently, and the
fourth colour to touch a cell was drawn in the wrong one -- visibly, a ball
showing less than half its colour.

Dropping the allocator also removes `release_cells()`, which cost ~6,780
cycles per ball, roughly twice the blit it followed.

## The memory trap

**This bug landed three times and is invisible every time.** cc65's BSS grows
into the VIC's memory, the linker reports nothing wrong, the program links, and
the two structures quietly share RAM. The symptoms are a black screen, corrupted
ball colours, or an outright crash — none of which point at the cause.

| BSS ran | through | symptom |
|---|---|---|
| `$1F03-$289F` | bitmap at `$2000` | black screen |
| `$3F37-$4C74` | screen at `$4400` | caught before it shipped |
| `$4F2D-$5C94` | screen at `$5C00` | graphical glitches and crashes |

Two things fixed it for good.

**The VIC now lives in bank 2** (`$8000-$BFFF`), screen `$8400`, bitmap `$A000`.
cc65 banks BASIC out — `__HIMEM__` is `$D000` — so `$A000-$BFFF` is ordinary
RAM, and the whole of `$0801-$83FF` is free for code and data. Headroom went
from 93 bytes to over 10,000. The VIC sees the character ROM at `$9000-$9FFF` in
this bank, which both structures avoid.

**`tools/build-game.ps1` checks the map** and fails the build if BSS reaches the
screen, warning below 256 bytes of headroom. Build through it, not `cl65`
directly — the check is the whole point.

### Another PowerShell trap

`Set-Content -Encoding utf8` writes a **BOM**, which cc65 rejects with
`Invalid input character with code EF`. Use `[System.IO.File]::WriteAllText`
with `UTF8Encoding($false)`. Likewise, `2>&1` on a native command in Windows
PowerShell wraps stderr in ErrorRecords and trips `$ErrorActionPreference` on a
mere compiler warning.

## The engine — `engine/`

Ported from section 2 of `oddballz-app.js`, which stays the reference spec. The
JS engine is integer-only by design — byte grid, additive neighbour offsets,
run-length match counting, no multiply, divide or float on any hot path — and
that is what made the port straightforward. Rotation is literally
`(x,y) -> (x-y, x)`.

`engine/engine.h` holds the rules; `engine/game.c` drives the renderer.

### Deliberate deviations

- **Board storage.** The JS keeps `{inMap, bzMap}` per cell and calls
  `checkInMap()`. Here out-of-board cells hold `OUT` (`0xFF`), so "in board and
  empty" is one byte compare and `checkInMap` vanishes from every inner loop.
- **Row padding.** Rows are padded two above and below, because
  `moveInDirection` can step two rows off the board (dirs 8 and 9) before the
  sentinel stops the walk.
- **Matched cells are marked, then cleared.** Deleting in place would break
  detection of runs crossing one already found.
- **`colorCount` is kept modulo `colors^(i+1)`.** The JS lets it grow without
  bound because it holds doubles; the modulus is the period of
  `(count / (inc-1)) % colors`, so everything stays in 16 bits.
- **`Math.random` becomes an xorshift byte PRNG.** No rule depends on the
  source of randomness.
- **`lDelay` is not ported** — it is dead data in the JS, assigned to
  `pauseTime` and never read.
- **Fall speed is frames per row, not rows per second.** The JS works in real
  time; here the loop counts fields, so `set_fall_speed()` converts the same
  formula into a frame count and rebuilds the interpolation tables when the level
  changes. That conversion is what makes PAL and NTSC agree — see **Fall speed,
  and PAL vs NTSC**.
- **Two game-over paths, not one.** The JS only has `checkGameOver()`. Here a
  spawned piece that does not fit also ends the game, because it has nowhere to
  go and the JS's answer — stamp it anyway — would overwrite landed balls. The
  two disagree about where the danger is: the spawn root is row 3 but a shape
  reaches below and either side of it, into cells `check_game_over()` never
  inspects. Measured on a self-playing build, that second path was about one game
  in ten. `$035C` counts them.

### Verified, not assumed — `engine/engine-test.c`

The match rules are what must not drift, and no screenshot can confirm them. 34
hand-built cases, **34 passing**, covering: five-in-a-row matches on a parallel
axis and four does not; three on a perpendicular axis and two does not; dir 9
as the on-screen vertical; `sameBonus` of `len-3` and `len-2`; a run counted
once rather than from both ends; adjacent different colours not matching; the
rests-on-two support rule including the board edge; a settle step moving a ball
exactly one row; game over firing only in rows 0–3, columns 4–12; and `2^count`
scoring; flip mirroring a bar and re-normalising rel[0]; rotation as (x,y) ->
(x-y, x); and row clearing firing on a complete row but not an incomplete one.
Border goes green on a clean run, red otherwise.

```powershell
.\vendor\cc65\bin\cl65.exe -t c64 -Oirs .\engine\engine-test.c -o .\build\engine-test.prg
.\tools\peek.ps1 -Prg .\build\engine-test.prg -Start 0x0350 -End 0x0365
```

### A missing feature the demo exposed

The self-playing demo kept topping out after about eight pieces — game over
firing correctly, over and over. The cause was a genuine gap in the port: the JS
lets the player move a piece **laterally** (`targetFloatX`, :414), independent of
which diagonal it falls along. Only the two fall directions had been ported, so
pieces could only reach the columns their spawn point led to and piled into the
narrow top of the hexagon. Adding `piece_move()` took game-overs from 56 to 3
over the same run, and landings from row 4–7 down to row 18.

Worth recording because the *engine* was behaving correctly the whole time —
the danger-zone rule was doing exactly what it should. Only a running game
surfaced the omission.

Alongside it, a rendering bug in the same loop: `erase_piece()` ran *after* the
lateral move, wiping the new position and leaving the old one on screen.

### Flip, rotation and the kick system

`piece_flip()` ports `transform(flipX|flipY)` (:551) in full, including the part
that is easy to miss: after reflecting through the 5×5 table it tries **five root
shifts** — none, left, right, up, down — and keeps the one overlapping the
piece's own cells most, ties broken by least movement. That is a kick system, so
a flip against a wall shuffles clear instead of failing. Then rel[0] is
re-normalised back to (0,0) (:609).

Rotation is the simpler half: `(x,y) -> (x-y, x)`, reverted wholesale if the
result does not fit.

### Row Build is a *mode*, not a stage — and the JS is not its oracle

`checkMatches` (:1007) branches on `matcher`: **either** clear by colour match
**or** clear complete rows, never both.

**The HD version's Row Build is unfinished, so it cannot be copied.** The mode
branch lives inside `checkMatches`, which only makes sense if that is called in
both modes — but the landing path guards the call with `if (this.matcher)`
(:495). So in Row Build the JS never clears a row *or* settles. The rules below
come from the 1992 original via Brian, not from the reference.

Row Build differs in three ways, all of which had to be fixed after playtesting:

- **Pieces are one solid colour**, keyed to the shape (`config + 1`, random past
  shape six). `newBall` branches on `matcher` for this too (:352) and only the
  matching half had been ported, so pieces came out four-coloured. Colour
  carries no meaning when rows clear by being *full*.
- **Nothing settles.** Balls stay exactly where they land. Gravity pulling balls
  out from under a half-built row would make building one impossible. Only
  `deleteRow` moves anything, by collapsing the columns of a row it has cleared.
- **Only two colours appear at level 1**, because only two *shapes* are in play
  (`lShapes: 2`). That is the shape ramp, not a fault.

`deleteRow` rewrites whole columns rather than individual cells, so unlike a
colour match there is no list of touched cells. The board is snapshotted into
`MARK` — free in this mode — and only changed cells are redrawn. An earlier
version called `vic_bitmap_mode()` and repainted just the balls, which wiped the
sockets *and* the HUD: the board became balls floating in a black void, and read
as though supports had stopped working. Nothing was wrong with the settling; the
playfield had been erased out from under it.

`find_full_row()` reports a full row without deleting it, so the renderer can
flash it white first — `checkRows` finds and deletes in one step, leaving
nothing to show.

### Smooth motion and input

The falling piece rides **four hardware sprites**, interpolated within the row,
so it moves at pixel resolution instead of snapping a cell at a time. On landing
it is stamped into the bitmap and the sprites are hidden. This is the mechanism
the sprite spike measured at 2,534 cycles a frame.

Four separate sprites rather than one wide one also pays off at the top of the
board: masking the entry rows is per **ball**, so a shape enters one ball at a
time instead of appearing whole. See **The top three rows are masked**.

Keyboard, read straight off the C64 matrix. The full list is under **Controls**;
this is the in-game set:

| | |
|---|---|
| **J** / **L** | move left / right |
| **I** / **K** | rotate counter-clockwise / clockwise |
| **F** | cycle the piece's colours |
| **X** / **Y** | flip about the x / y axis |
| **SPACE** | zip |
| **P** | pause, and from there resume or end the game |

IJKL stands in for the PC arrow keys, which a C64 keyboard does not have.

SPACE is a **latch**, not a held key: zip() (:763) commits the piece to the
floor and isZipping is only cleared when the next one spawns (:406), so the
drop does not stop the moment the key comes up.

Everything but movement is edge-triggered, or it fires every frame the key is
down. Movement itself steps once on press then repeats after a delay -- applied
per frame it is twelve cells per row, which reads as a twitch.

Keyboard rather than joystick because reading the matrix needs port A as output
and port B as input, the exact opposite of reading a joystick; the two cannot
both be live.

A piece nobody touches falls where it spawned. It used to walk itself toward a
random column when the keyboard was idle, which let a headless run fill the board
with nobody playing — useful for testing, but it is not a feature, and it also
meant that letting go of left or right handed the piece back to the machine
mid-fall. That code now lives behind `-DDEMO_STEER=1` and is off in every
shipped build.

## Sound

Effects only, on voice 3. They fire on things that happen to the **board** —
colour cycle, zip, lock, match, level up, game over. Steering, rotating and
flipping happen constantly during a fall, and a click on each turned the game
into a typewriter.

Two bugs here were worth recording, because both made an event sound *different
from itself*:

- `sfx_play` let a **longer** effect block a shorter one, so a lock landing
  inside the tail of a zip played the zip's descending sweep instead. A new
  effect now always replaces, except game over.
- The frequency slide had no floor, so a sweep long enough to pass zero wrapped
  the unsigned value up near 65535 and shrieked. Game over did that every time:
  6000 descending by 240 over 28 frames goes negative before it ends.

The background music is built and works — three layers, arpeggio on voice 1,
octave-jumping bass on voice 2, drums on voice 3 — but is **off by default**. It
runs from the main loop rather than a raster interrupt, so anything that does not
wait on a frame stalls it audibly, and it was not varied enough to live with.
`-DUSE_MUSIC=1` brings it back.

## Open questions for Brian

Answered along the way, kept because the reasoning still applies:

- ~~Whether cascades should animate in whole-cell steps or promote to sprites.~~
  Whole-cell steps, and they read fine.
- ~~Whether colour clash is worth chasing.~~ It stopped being a question once
  slots were assigned by row parity — clash is now structurally impossible on the
  board rather than merely rare. See **Colour clash is now structurally
  impossible**.
- ~~Whether the fall speed felt right.~~ It did not, and the number was exact: a
  flat 24 frames a row is 2.08 rows a second against the original's 1.0.

Still open:

- Whether the ~13 % vertical squash in variant E is noticeable on a real
  display. It is the price of D's ball size with the full board.
- Whether it behaves on real hardware. Nothing here has run outside VICE. Both
  frame rates are handled, so the interesting failure would be something else —
  the raster wait, the CIA keyboard read, or SID timing.
- Whether the board wants a graphic background in the corners, and if so what
  the single global background colour should be, since it is one register for
  the whole screen and shows in every gap between sockets.
