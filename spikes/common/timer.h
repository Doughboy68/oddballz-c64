/* timer.h - cycle-accurate measurement readable from outside the emulator.
 *
 * CIA2 timer A counts system cycles and timer B counts A's underflows, giving a
 * 32-bit count. Timer A alone wraps at 65,536 cycles -- under four frames --
 * which is not enough for anything interesting.
 *
 * Results are parked in the tape buffer at $0350, where nothing else lives, so
 * tools/peek.ps1 can read them straight out of a running emulator. This is the
 * C64 analogue of inspecting window.oddApp in the JS version.
 *
 * A PAL frame is 19,656 cycles; the visible portion is about 15,700.
 */

#ifndef TIMER_H
#define TIMER_H

#define RESULTS ((unsigned char *)0x0350)
#define CIA1    ((unsigned char *)0xDC00)
#define CIA2    ((unsigned char *)0xDD00)

static void timer_start(void)
{
    CIA2[0x0E] = 0x00; CIA2[0x0F] = 0x00;         /* stop both */
    CIA2[0x0D] = 0x7F;                            /* no CIA2 NMIs on underflow */
    CIA2[0x04] = 0xFF; CIA2[0x05] = 0xFF;         /* TA latch $FFFF */
    CIA2[0x06] = 0xFF; CIA2[0x07] = 0xFF;         /* TB latch $FFFF */
    CIA2[0x0F] = 0x41;                            /* TB: count TA underflows */
    CIA2[0x0E] = 0x01;                            /* TA: count phi2 */
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

/* The KERNAL's 60 Hz IRQ would otherwise land inside a measured section. */
static void silence_kernal_irq(void) { CIA1[0x0D] = 0x7F; }

#endif /* TIMER_H */
