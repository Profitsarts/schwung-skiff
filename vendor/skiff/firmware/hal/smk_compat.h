/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * SMK-37 Pro port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* SMK-37 Pro: the parts of the FM-1 LED API (fm1_input.h, fm1_led_anim.h) the SMK has no hardware for: the glow
 * level and the power-on sweep (the SMK's button LEDs are on / off; ui_input.c smk_leds_out reads the picture back).
 * No-ops, so the UI code stays as it is. Included after smk_input.h. */
#pragma once
#include <stdint.h>

static void fm1_led_dim_level(uint32_t lo) { (void)lo; }
static void fm1_led_anim_start(uint32_t glow) { (void)glow; }
static int fm1_led_anim_on(void) { return 0; }
