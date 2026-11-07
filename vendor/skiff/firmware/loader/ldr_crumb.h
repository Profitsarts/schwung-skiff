/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff, SMK-37 Pro) The update loader's start record: how this start came about and how far the run before it got.
 * It lives in RAM that a reset keeps (hal/fm1_sys.h FM1_LDR_CRUMB_ADDR, the top of the app's NOINIT, which app.ld
 * keeps free), so a loader that resets itself mid-write leaves its stage and sector for the next start, which hosts
 * read with the info query (ota.c, cmd 0x7B). A record that did not survive (a power-on, or a reset that lost the
 * RAM) starts again at 1. No registers here: loader.c reads them, tests/ldr_test.c runs this as it is.
 *
 * The reply to cmd 0x7B is the record's first 32 bytes as they are (little-endian):
 *   +0  u8  p3_rst: P3_RST_SRC at this start (bit0 power-on, 1 VDDIO low, 2 watchdog, 3 VCM, 4 long press, 5 1.2 V,
 *           6 soft)              +1  u8  wdt_con: P3_WDT_CON as this start found it
 *   +2  u16 starts since the record began
 *   +4  u32 rst_src: RST_SRC at this start (bit5 soft, PWR_CON)
 *   +8  this run, 12 bytes:      +0 u8 stage  +1 u8 warm (1: started by the app, its RAM record; 0: by the boot stage
 *           from the flash record)  +2 u16 the slowest 4 KiB erase, ms  +4 u32 sector (stage 3: the one being written)
 *                                +8 u32 ms since the start, at the last stage change
 *   +20 the run before this start, the same 12 bytes (stage 0: none recorded)
 * stages: 1 idle (handshake), 2 checking the package (2b), 3 writing (sector), 4 finishing (step 4), 5 done */
#pragma once
#include <stdint.h>

#define LDR_CRUMB_MAGIC 0x3152444Cu              /* "LDR1" */

typedef struct {
    uint8_t stage, warm;
    uint16_t erase_max_ms;
    uint32_t sector, ms;
} ldr_run_t;

typedef struct {
    uint8_t p3_rst, wdt_con;
    uint16_t starts;
    uint32_t rst_src;
    ldr_run_t now, prev;                         /* (the reply ends here: 32 bytes) */
    uint32_t magic, pad;
} ldr_crumb_t;                                   /* 40 B of the 48 kept */
_Static_assert(sizeof(ldr_crumb_t) == 40 && __builtin_offsetof(ldr_crumb_t, magic) == 32, "ldr_crumb_t: the wire layout");

static void ldr_crumb_start(ldr_crumb_t *c, uint32_t warm, uint32_t p3_rst, uint32_t rst_src, uint32_t wdt_con)
{
    if (c->magic != LDR_CRUMB_MAGIC) {
        uint32_t *w = (uint32_t *)c, i;
        for (i = 0; i < sizeof *c / 4u; i++)
            w[i] = 0;
        c->magic = LDR_CRUMB_MAGIC;
    }
    c->prev = c->now;
    c->starts++;
    c->p3_rst = (uint8_t)p3_rst;
    c->wdt_con = (uint8_t)wdt_con;
    c->rst_src = rst_src;
    c->now.stage = 1;
    c->now.warm = (uint8_t)warm;
    c->now.erase_max_ms = 0;
    c->now.sector = c->now.ms = 0;
}

static void ldr_crumb_stage(ldr_crumb_t *c, uint32_t stage, uint32_t sector, uint32_t ms)
{
    c->now.stage = (uint8_t)stage;
    c->now.sector = sector;
    c->now.ms = ms;
}

static void ldr_crumb_erase(ldr_crumb_t *c, uint32_t took_us)
{
    uint32_t ms = took_us / 1000u;
    if (ms > c->now.erase_max_ms)
        c->now.erase_max_ms = (uint16_t)(ms > 0xFFFFu ? 0xFFFFu : ms);
}
