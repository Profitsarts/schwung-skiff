/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FM1_INLINE: register helpers that must inline into their caller, so the
 * SFR access sequence (and its timing, in ISRs and IRQ-off sections) is the
 * one written at the call site; -Os would otherwise out-line a helper with
 * several callers. RAM-resident code (.ram_text) may only use these, never
 * a called helper (build.py checks .ram_text for calls). */
#pragma once

#define FM1_INLINE static inline __attribute__((always_inline))

/* (Skiff) the target board: 0 = the FM-1 (default), 1 = the M-VAVE SMK-37 Pro: the same AC791N, SPL, audio path and
 * screen, another panel wiring and flash map (smk_input.h, fm1_flash_map.h; based on genkigenki's SMK-37 layer for
 * Felucca). build.py sets it from BOARD=smk37; the FM-1 build gets no new flag. */
#ifndef BOARD_SMK37
#define BOARD_SMK37 0
#endif
/* SMK_MODEL: the model name an SMK build reports (its identity and its update loader's). The SMK-37 Pro, SMK-37 Elite
 * and MKE-P37 are one keyboard under three names, and the stock updater takes only a unit's own factory package.
 * build.py sets it from MODEL=elite / mke. */
#if BOARD_SMK37 && !defined(SMK_MODEL)
#define SMK_MODEL "SMK-37 Pro"
#endif
