/* SPDX-License-Identifier: MIT
 * SYS 360 -> 320 MHz at boot, after the JieLi AC79 SDK's sys_clock_update (Apache-2.0),
 * PLL2-VCO path with the SDK's 320 MHz row (HSB 160, SFC 80, LSB 53). */
#define FCLK_SYS_DIV    REG32(0x10008u)   /* [17:16] HSB = SYS/(n+1); [10:8] LSB = HSB/(n+1) */
#define FCLK_CLK_CON0   REG32(0x1000Cu)   /* b8 SYS from the PLL (0: RC); b1 RC16M on */
#define FCLK_CLK_CON2   REG32(0x10014u)   /* [3:0] SYS source: 6 = PLL2 VCO * 2/3 */
#define FCLK_PLL_CON    REG32(0x119A0u)   /* [27:26] = 2: 40 MHz reference, else 24 MHz */
#define FCLK_PLL2_CON0  REG32(0x119A8u)   /* [6:2] reference divider - 2 */
#define FCLK_PLL2_CON1  REG32(0x119ACu)   /* [11:0] VCO multiplier - 2 */
#define FCLK_USB_CON0   REG32(0x16A00u)
#define FCLK_USB_CLK    REG32(0x16A08u)   /* [4:3] = 3: USB clocked from PLL2 */
#define FCLK_SDR_CON0   REG32(0x40400u)   /* b11: SDRAM in use */
#define FCLK_SDR_CON1   REG32(0x40404u)
#define FCLK_PSR_CON    REG32(0x40500u)   /* b0: the SDK sets cache b9 back if set */
#define FCLK_SFC_BAUD   REG32(0x40204u)   /* SFC clock = HSB / (n + 1) */

typedef struct { uint32_t pll2_con1, sys_div, sfc_baud, sfc_con, cache_b9; } fm1_clk320_t;

static inline uint32_t fm1_clk320_mult(void) { return (FCLK_PLL2_CON1 & 0xFFFu) + 2u; }

/* 0 = the expected state; fills p. Any other value: do not switch. */
static int fm1_clk320_check(fm1_clk320_t *p)
{
    uint32_t osc = (FCLK_PLL_CON & 0x0C000000u) == 0x08000000u ? 40000000u : 24000000u;
    uint32_t ref = osc / (((FCLK_PLL2_CON0 >> 2) & 31u) + 2u);
    uint32_t div = FCLK_SYS_DIV, cache = CACHE_CON, n;
    if (fm1_clk320_mult() == 240u) return 1;              /* already 320 (a soft reset keeps the PLL) */
    if ((FCLK_CLK_CON2 & 15u) != 6u) return 2;            /* SYS not on PLL2 VCO * 2/3 */
    if (fm1_clk320_mult() != 270u) return 3;              /* not the boot loader's 360 */
    if (ref != 2000000u) return 4;
    if (((div >> 16) & 3u) != 1u || ((div >> 8) & 7u) != 2u) return 5;   /* HSB /2, LSB /3 */
    if (!(FCLK_CLK_CON0 & 0x100u)) return 6;              /* SYS not on the PLL */
    if ((FCLK_USB_CLK & 0x18u) == 0x18u) return 7;        /* USB on PLL2 */
    if (FCLK_SDR_CON0 & 0x800u) return 8;                 /* SDRAM */
    p->cache_b9 = ((FCLK_PSR_CON & 1u) ? 0x200u : 0u) | (cache & 0x200u);   /* keep the boot loader's b9 */
    p->sys_div = (div & ~0x30000u & ~0x700u) | 0x10000u | 0x200u;          /* HSB 320/2, LSB 160/3 */
    p->sfc_baud = 1u;                                                      /* SFC 160/2 = 80 */
    p->pll2_con1 = (FCLK_PLL2_CON1 & ~0xFFFu) | ((480000000u / ref + 4094u) & 0xFFFu);   /* VCO 480 */
    n = 160u * 45u;                                       /* CS high 45 ns in 160 MHz cycles, rounded up */
    n = n / 1000u + (n % 1000u != 0u);
    p->sfc_con = (SFC_CON & ~0xF00000u) | (((n + 1u) << 20) & 0xF00000u);
    return 0;
}

#define FCLK_SYNC() __asm__ volatile("csync" ::: "memory")
#define FCLK_WAIT_CACHE() do {} while (!(CACHE_CON & (1u << 14)))
#define FCLK_WAIT_SFC() do {} while (SFC_CON & 0x80000000u)
static inline __attribute__((always_inline)) void fclk_spin(uint32_t n)
{
    volatile uint32_t k = n + 1u;
    while (--k)
        __asm__ volatile("nop");
}

/* RAM only, IRQs off, second core not running: no flash fetch from here to the end */
static RAMFN void fm1_clk320_switch(const fm1_clk320_t *p)
{
    uint32_t pll2 = p->pll2_con1, sdiv = p->sys_div, baud = p->sfc_baud, scon = p->sfc_con, b9 = p->cache_b9;
    uint32_t usb = FCLK_USB_CON0, sdr = FCLK_SDR_CON1, con2 = FCLK_CLK_CON2, r = FCLK_CLK_CON0;
    if (r & 0x100u) {                                     /* SYS onto RC16M */
        r = (r | 2u) & ~0x100u;
        FCLK_SYNC(); FCLK_WAIT_CACHE(); FCLK_SYNC(); FCLK_WAIT_SFC();
        FCLK_CLK_CON0 = r;
        FCLK_SYNC(); fclk_spin(300);
    }
    FCLK_SYNC(); FCLK_WAIT_CACHE(); CACHE_CON &= ~0x300u;     /* cache off */
    FCLK_SYNC(); FCLK_WAIT_SFC(); SFC_CON &= ~1u;            /* flash controller off */
    FCLK_SYNC(); fclk_spin(60u * 4u);                         /* 60 us at RC16M, x2 margin */
    FCLK_USB_CON0 = usb;                                      /* the SDK's write order */
    FCLK_SDR_CON1 = sdr;
    FCLK_PLL2_CON1 = pll2;
    FCLK_CLK_CON2 = con2;
    FCLK_SYS_DIV = sdiv;
    FCLK_SYNC(); fclk_spin(1000u * 4u);                       /* 1 ms: the PLL settles */
    FCLK_SYNC(); FCLK_WAIT_CACHE(); FCLK_SYNC(); FCLK_WAIT_SFC();
    FCLK_SFC_BAUD = baud;                                     /* flash controller back, retimed */
    SFC_CON = scon;
    FCLK_SYNC(); fclk_spin(60u * 4u);
    CACHE_CON |= 0x100u | b9;                                 /* cache back */
    FCLK_SYNC();
    r = FCLK_CLK_CON0;
    if (!(r & 0x100u)) {                                  /* SYS back onto the PLL */
        r |= 0x100u;
        FCLK_SYNC(); FCLK_WAIT_CACHE(); FCLK_SYNC(); FCLK_WAIT_SFC();
        FCLK_CLK_CON0 = r;
        FCLK_SYNC(); fclk_spin(300);
    }
}

/* at boot, before the second core and any peripheral start:
 *   fm1_clk320_t p;
 *   if (fm1_clk320_check(&p) == 0) { irqs off; FL_FAR(fm1_clk320_switch)(&p); irqs back; }
 *   then fm1_clk320_mult() == 240 means 320 MHz */
