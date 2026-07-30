/* sound.h - SID effects and background music.
 *
 * Voices 1 and 2 carry the music, voice 3 is reserved for effects, so a sound
 * never steals a note from the tune. The SID's three voices are the whole
 * budget; the 1992 PC version had one square-wave channel, so this is already
 * the "gained" side of the port.
 *
 * sound_tick() must be called once per frame. It is driven from the game loop
 * rather than a raster interrupt, which is simpler and needs no assembly ISR,
 * but means the music pauses during anything that does not wait on a frame --
 * the initial board draw is the only one long enough to hear. Moving the tick
 * to a raster IRQ is the fix if that ever matters.
 *
 * Note frequencies: reg = Hz * 16777216 / 985248 = Hz * 17.0287 on PAL. The
 * table is octave 1; higher octaves are a shift, since an octave is a doubling.
 * Notes are packed (octave << 4) | semitone so the tick needs no division.
 */

#ifndef SOUND_H
#define SOUND_H

#define SID ((unsigned char *)0xD400)

#define V1 0
#define V2 7
#define V3 14

/* Waveform | gate. Bit 4 triangle, 5 saw, 6 pulse, 7 noise. */
#define W_TRI   0x11
#define W_SAW   0x21
#define W_PULSE 0x41
#define W_NOISE 0x81

#define N(o, s) (unsigned char)(((o) << 4) | (s))
#define REST 0xFF

static const unsigned int NOTE[12] = {
    557, 590, 625, 662, 702, 743, 788, 834, 884, 936, 992, 1051
};

#ifndef USE_MUSIC
#define USE_MUSIC 0
#endif
#if USE_MUSIC
static void sid_freq(unsigned char v, unsigned char n)
{
    unsigned int f = NOTE[n & 15];
    unsigned char o = (unsigned char)(n >> 4);
    if (o > 1) f <<= (o - 1);
    SID[v]     = (unsigned char)f;
    SID[v + 1] = (unsigned char)(f >> 8);
}
#endif /* USE_MUSIC -- only the music needs note frequencies */

/* --- effects ----------------------------------------------------------- */

#define SFX_NONE  0
#define SFX_CLICK 1     /* rotate, flip, colour cycle */
#define SFX_DROP  2     /* a piece comes to rest      */
#define SFX_POP   3     /* balls cleared by a match   */
#define SFX_ZIP   4     /* fast drop committed        */
#define SFX_LEVEL 5     /* level up                   */
#define SFX_OVER  6     /* game over                  */
#define SFX_COUNT 7

/* Each effect is a start frequency, a per-frame slide, a length and an
 * envelope. Sweeping the frequency is what makes eight bytes of data sound
 * like something rather than a beep. */
/* The lock (SFX_DROP) is deliberately the shortest and plainest of these: four
 * frames, barely any slide, a plain triangle that decays on its own. It fires
 * on every single piece, so anything with character in it becomes irritating
 * fast. It used to run nine frames with a big downward sweep, which is what
 * made it sound long and unlike itself from one landing to the next. */
static const unsigned int SFX_F0[SFX_COUNT] = { 0, 7000, 1700, 7000, 14000, 4000, 6000 };
static const int          SFX_DF[SFX_COUNT] = { 0,    0, -110,  1100, -1300,   700, -240 };
static const unsigned char SFX_LEN[SFX_COUNT] = { 0, 2, 4, 7, 10, 18, 28 };
static const unsigned char SFX_W[SFX_COUNT]   = { 0, W_PULSE, W_TRI, W_PULSE, W_SAW, W_TRI, W_SAW };
static const unsigned char SFX_AD[SFX_COUNT]  = { 0, 0x00, 0x04, 0x00, 0x00, 0x09, 0x0A };
static const unsigned char SFX_SR[SFX_COUNT]  = { 0, 0xF0, 0x00, 0xA0, 0x80, 0x70, 0x90 };

static unsigned char sfx_id = SFX_NONE, sfx_t;
static unsigned int  sfx_f;

static void sfx_play(unsigned char id)
{
    /* A new effect always replaces the old one, so a given event always sounds
     * the same. The previous rule let a LONGER effect block a shorter one,
     * which meant a lock landing inside the tail of a zip played the zip's
     * descending sweep instead -- the same event sounding different depending
     * on what came before it. Only game over is protected. */
    if (sfx_id == SFX_OVER && sfx_t < SFX_LEN[SFX_OVER]) return;
    sfx_id = id;
    sfx_t  = 0;
    sfx_f  = SFX_F0[id];

    /* Gate LOW before gating high. The SID's envelope generator restarts only
     * on a gate low->high TRANSITION -- writing the control register with the
     * gate bit already set does nothing at all. Effects here fire back to back
     * (a lock is immediately followed by a match), so the previous effect was
     * usually still gated on and the new one simply never attacked: it either
     * stayed silent or rode out the old envelope. That is the "sound does not
     * play all the time" bug. */
    SID[V3 + 4] = 0x00;

    SID[V3 + 5] = SFX_AD[id];
    SID[V3 + 6] = SFX_SR[id];
    SID[V3 + 2] = 0x00;                 /* pulse width, for the pulse waves */
    SID[V3 + 3] = 0x08;
    SID[V3]     = (unsigned char)sfx_f;
    SID[V3 + 1] = (unsigned char)(sfx_f >> 8);
    SID[V3 + 4] = SFX_W[id];            /* gate on: now a real transition */
}

static void sfx_tick(void)
{
    if (!sfx_id) return;
    if (++sfx_t >= SFX_LEN[sfx_id]) {
        SID[V3 + 4] = (unsigned char)(SFX_W[sfx_id] & 0xFE);   /* gate off */
        sfx_id = SFX_NONE;
        return;
    }
    /* Floor the slide. A downward sweep long enough to pass zero wrapped the
     * unsigned frequency to something near 65535 and shrieked -- game over ran
     * 6000 down by 240 for 28 frames, which goes negative before it ends. */
    {
        int f = (int)sfx_f + SFX_DF[sfx_id];
        if (f < 120) f = 120;
        sfx_f = (unsigned int)f;
    }
    SID[V3]     = (unsigned char)sfx_f;
    SID[V3 + 1] = (unsigned char)(sfx_f >> 8);
}

/* --- music ------------------------------------------------------------- *
 *
 * OFF. The three-layer chiptune below works, but it was not varied enough to
 * live with, and the player runs from the main loop rather than a raster
 * interrupt -- so anything that does not wait on a frame (a match scan, a
 * cascade, the board draw) stalls it audibly. Doing it properly means an
 * interrupt-driven player with its own pattern format, which is a project in
 * itself and not what this port is for.
 *
 * Dropping it also gives voice 3 to the effects alone, so nothing competes for
 * the channel, and saves the code and tables in a build with under 1 KB of
 * headroom below the screen.
 *
 * Build with -DUSE_MUSIC=1 to bring it back.
 */
#ifndef USE_MUSIC
#define USE_MUSIC 0
#endif

#if USE_MUSIC

#define MUSIC_STEPS 32
#define MUSIC_SPEED 6           /* frames per step, ~8 steps a second at 50 Hz */

/* Three layers, which is what actually makes a chiptune rather than a tune
 * played on a chip:
 *
 *   voice 1  ARPEGGIO -- the chord's three notes cycled one per FRAME, 50 times
 *            a second. One oscillator then sounds like a whole chord. This is
 *            the defining C64 trick and the thing my first attempt lacked
 *            entirely; a single sustained note per step is what made it sound
 *            like a toy.
 *   voice 2  bass, jumping to the octave on the off-beats rather than hammering
 *            the root, plus pulse-width movement.
 *   voice 3  drums, borrowed from the effects channel whenever no effect is
 *            playing. Kick on the beat, hat off it.
 *
 * Am - F - C - G, eight steps a chord. */
static const unsigned char CHORD[4][3] = {
    { N(4, 9), N(5, 0), N(5, 4) },      /* Am : A  C  E */
    { N(4, 5), N(4, 9), N(5, 0) },      /* F  : F  A  C */
    { N(4, 0), N(4, 4), N(4, 7) },      /* C  : C  E  G */
    { N(4, 7), N(4,11), N(5, 2) }       /* G  : G  B  D */
};
static const unsigned char BASSN[4] = { N(2, 9), N(2, 5), N(2, 0), N(2, 7) };

/* Which steps of the eight take the octave above. */
static const unsigned char BASS_OCT[8] = { 0, 0, 1, 0, 0, 1, 0, 1 };

static unsigned char mus_step = 0, mus_t = 0, mus_on = 1, mus_arp = 0;
static unsigned int  mus_pw = 0x0600;
static signed char   mus_pw_dir = 1;

static void drum_hit(unsigned char kick)
{
    if (sfx_id) return;                 /* an effect owns voice 3 */
    SID[V3 + 4] = 0x00;                 /* gate low first, or no new attack */
    if (kick) {
        SID[V3 + 5] = 0x0A;  SID[V3 + 6] = 0x00;
        SID[V3]     = 0x30;  SID[V3 + 1] = 0x03;
        SID[V3 + 4] = W_TRI;
    } else {
        SID[V3 + 5] = 0x00;  SID[V3 + 6] = 0x40;
        SID[V3]     = 0x00;  SID[V3 + 1] = 0x40;
        SID[V3 + 4] = W_NOISE;
    }
}

static void music_tick(void)
{
    unsigned char c;
    if (!mus_on) return;

    c = (unsigned char)(mus_step >> 3);

    /* Arpeggio: a new chord tone every single frame. */
    sid_freq(V1, CHORD[c][mus_arp]);
    if (++mus_arp > 2) mus_arp = 0;

    /* Pulse-width sweep, also per frame -- stops the lead being a flat square. */
    mus_pw = (unsigned int)((int)mus_pw + mus_pw_dir * 24);
    if (mus_pw > 0x0C00) mus_pw_dir = -1;
    if (mus_pw < 0x0200) mus_pw_dir =  1;
    SID[V1 + 2] = (unsigned char)mus_pw;
    SID[V1 + 3] = (unsigned char)((mus_pw >> 8) & 0x0F);

    if (++mus_t < MUSIC_SPEED) return;
    mus_t = 0;

    /* Bass: root, or the octave above on the off-beats. */
    {
        unsigned char n = (unsigned char)(BASSN[c] + (BASS_OCT[mus_step & 7] << 4));
        sid_freq(V2, n);
        SID[V2 + 4] = W_SAW & 0xFE;
        SID[V2 + 4] = W_SAW;
    }

    if (!(mus_step & 3))      drum_hit(1);      /* kick on the beat  */
    else if ((mus_step & 3) == 2) drum_hit(0);  /* hat off the beat  */

    if (++mus_step >= MUSIC_STEPS) mus_step = 0;
}

static void music_toggle(void)
{
    mus_on = (unsigned char)!mus_on;
    if (!mus_on) { SID[V1 + 4] = 0; SID[V2 + 4] = 0; }
}

#else   /* music off: voices 1 and 2 stay silent, voice 3 is all effects */

static void music_tick(void) { }
static void music_toggle(void) { }

#endif /* USE_MUSIC */

static void sound_tick(void) { music_tick(); sfx_tick(); }

static void sound_init(void)
{
    unsigned char i;
    for (i = 0; i < 25; ++i) SID[i] = 0;
    SID[0x18] = 0x0F;                   /* volume, filters off */
    sfx_id = SFX_NONE;

#if USE_MUSIC
    /* The arpeggio voice is gated ON once and left ringing -- only its
     * frequency changes, every frame. Retriggering it per note would machine-gun
     * the attack and destroy the shimmer. */
    SID[V1 + 2] = 0x00; SID[V1 + 3] = 0x06;
    SID[V1 + 5] = 0x00; SID[V1 + 6] = 0xF0;   /* instant attack, full sustain */
    SID[V1 + 4] = W_PULSE;
    SID[V2 + 5] = 0x08; SID[V2 + 6] = 0x69;   /* bass: quick attack, some ring */
    mus_step = 0; mus_t = 0; mus_arp = 0;
#endif
}

#endif /* SOUND_H */
