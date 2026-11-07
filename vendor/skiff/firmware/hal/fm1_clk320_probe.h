/* SPDX-License-Identifier: MIT */
/* PROBE BUILD (fm1-clk320-probe): what the probe adds around fm1_clk320.h, which is the study's published listing
 * (MIT) byte for byte, so the soak tests exactly the code others copy. fm1_clk320.h needs RAMFN, REG32,
 * SFC_CON and CACHE_CON (fm1_flash.h) and uint32_t before it.
 *
 *   FM1_CLK320_E*            fm1_clk320_check's return values, named for the report
 *   fm1_clk320_loop_ticks()  the core clock without the cycle counter: TIMER4 ticks (the crystal) for
 *                            4000 passes of 64 dependent adds, the best of three. Timed before and after
 *                            the switch in one boot, the ratio is the clock ratio. IRQs off. */
#pragma once
#include <stdint.h>
#include "fm1_flash.h"                          /* RAMFN, RAMINL, FL_FAR, REG32, SFC_CON, CACHE_CON */
#include "fm1_time.h"                           /* fm1_ticks: TIMER4, 24 MHz from the crystal */
#include "fm1_clk320.h"

#define FM1_CLK320_MULT_360 270u
#define FM1_CLK320_MULT_320 240u

enum {
    FM1_CLK320_OK = 0,
    FM1_CLK320_EALREADY,                        /* PLL2 already at 240 (a soft reset kept a switch) */
    FM1_CLK320_EMODE,                           /* SYS not on PLL2 VCO * 2/3 */
    FM1_CLK320_EMULT,                           /* PLL2 not at 270 */
    FM1_CLK320_EREF,                            /* PLL2 reference not 2 MHz */
    FM1_CLK320_EDIV,                            /* HSB / LSB dividers not /2 /3 */
    FM1_CLK320_ESRC,                            /* SYS not on the PLL (CLK_CON0 b8) */
    FM1_CLK320_EUSB,                            /* USB clocked from PLL2 */
    FM1_CLK320_ESDRAM,                          /* SDRAM in use */
    FM1_CLK320_ECACHE,                          /* (unused since the listing keeps cache b9) */
    FM1_CLK320_EVERIFY,                         /* (plat_fm1.c) PLL2 not at 240 after the switch */
    FM1_CLK320_ECONTROL,                        /* (plat_fm1.c) the control build: checked, not switched */
};

static __attribute__((noinline)) uint32_t fclk_adds(uint32_t passes)
{
    uint32_t x = 0;
    while (passes--)
        __asm__ volatile(".rept 64\n\t%0 = %0 + 1\n\t.endr" : "+r"(x));
    return x;
}

static uint32_t fm1_clk320_loop_ticks(void)
{
    uint32_t best = ~0u, t, k;
    fclk_adds(4);                               /* the loop into the cache */
    for (k = 0; k < 3u; k++) {
        t = fm1_ticks();
        fclk_adds(4000);
        t = fm1_ticks() - t;
        if (t < best)
            best = t;
    }
    return best;
}
