/* fill.c - cc65 toolchain check.
 *
 * Same screen fill again, in a third colour scheme (white balls on red) so the
 * screenshot unambiguously identifies which toolchain produced it.
 *
 * Written as plain pointer/memset work rather than conio, because that is how
 * the real engine port would touch screen and colour RAM.
 */

#include <string.h>

#define BORDER  (*(unsigned char *)0xD020)
#define BGCOLOR (*(unsigned char *)0xD021)
#define SCREEN  ((unsigned char *)0x0400)
#define COLRAM  ((unsigned char *)0xD800)

#define BALL    0x51    /* PETSCII filled circle */
#define CELLS   1000    /* 40 x 25 */

int main(void)
{
    BORDER  = 0x00;     /* black */
    BGCOLOR = 0x02;     /* red   */

    memset(SCREEN, BALL, CELLS);
    memset(COLRAM, 0x01, CELLS);   /* white */

    for (;;) {
        /* spin so the screenshot catches a stable frame */
    }

    return 0;
}
