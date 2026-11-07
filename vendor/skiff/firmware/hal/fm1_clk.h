/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff) The core clock: what runs now, and MENU > SYSTEM > CPU CLOCK 320 MHZ's switch at boot (main.c).
 * The boot stage M-VAVE's V15 update installed runs SYS at 360 MHz (PLL2's VCO 540 MHz x 2/3; HSB 180, LSB 60); JieLi
 * rates the chip for 320. fm1_clk320.h and fm1_clk320_probe.h are curist's (github.com/curist/fm1-clk320-probe, MIT; the
 * sequence after the JieLi AC79 SDK's sys_clock_update, Apache-2.0), unchanged: the check of the clock registers, the
 * switch to the SDK's 320 MHz row (HSB 160, SFC 80, LSB 53: RAM code, the cache and the flash controller stopped around
 * the PLL change and retimed after it; no voltage touched), its reasons, a loop timed against the crystal. A soft reset
 * keeps the PLL where it is; a power-on brings back the boot stage's 360. USB and the TRS UART run on PLL48M and the
 * timers on the crystal (unchanged); the LCD's SPI is LSB / 5: 10.7 MHz at 320 (12 at 360).
 *   fm1_clk_mhz()      the core clock now, from the PLL registers (0: SYS is not on PLL2's VCO x 2/3)
 *   fm1_sfc_baud()     the flash controller's divider (XIP runs at HSB / (n + 1))
 *   fm1_clk320_go(t)   360 -> 320 now: checked, switched with IRQs off, verified; t[0], t[1] the loop's crystal ticks
 *                      before and after (360 x t[0] / t[1]: the clock it runs at). 0 = done; else FM1_CLK320_E*, not
 *                      switched (EALREADY: a soft reset kept 320; EVERIFY: switched, and PLL2 is not at 240). Call it on
 *                      one core, before the audio, the timers and USB, with nothing reading the flash meanwhile (the
 *                      LCD's DMA idle) */
#pragma once
#include <stdint.h>
#include "fm1_clk320_probe.h"

static uint32_t fm1_clk_mhz(void)
{
    uint32_t ref = ((FCLK_PLL_CON & 0x0C000000u) == 0x08000000u ? 40000u : 24000u) /
                   (((FCLK_PLL2_CON0 >> 2) & 31u) + 2u);           /* kHz */
    if ((FCLK_CLK_CON2 & 15u) != 6u || !(FCLK_CLK_CON0 & 0x100u))
        return 0;
    return (ref * fm1_clk320_mult() * 2u / 3u + 500u) / 1000u;
}

/* the flash controller's clock divider: SFC = HSB / (n + 1) (the switch: 1, 80 MHz at 320; at 360 the boot stage's) */
static uint32_t fm1_sfc_baud(void) { return FCLK_SFC_BAUD; }

static int fm1_clk320_go(uint32_t *t)
{
    fm1_clk320_t p;
    uint32_t f = irq_save();
    int rc = fm1_clk320_check(&p);
    t[0] = t[1] = 0;
    if (rc == FM1_CLK320_OK) {
        t[0] = fm1_clk320_loop_ticks();
        FL_FAR(fm1_clk320_switch)(&p);
        t[1] = fm1_clk320_loop_ticks();
        if (fm1_clk320_mult() != FM1_CLK320_MULT_320)
            rc = FM1_CLK320_EVERIFY;
    }
    irq_restore(f);
    return rc;
}
