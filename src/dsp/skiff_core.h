/* SPDX-License-Identifier: GPL-3.0-only
 * Skiff Synth: what one copy of Skiff's sound core (skiff_core.c) offers the plugin (skiff_plugin.c). */
#ifndef SKIFF_CORE_H
#define SKIFF_CORE_H
#include <stdint.h>

#ifndef SKC_COPIES
#define SKC_COPIES 8             /* independent Skiff cores in one dsp.so: at most this many instances at once */
#endif

typedef struct {
    const char *label;           /* Skiff's label ("CUTOFF") */
    const char *const *names;    /* an enum's value names (index value - min), 0 = a number */
    int16_t min, max, def;
    uint8_t is_enum;
} skc_desc_t;

typedef struct {
    const char *key;             /* Move's key ("atk") */
    uint8_t global;              /* 0 = a track parameter (P_*), 1 = a global of the copy (G_*) */
    uint16_t id;
} skc_key_t;

typedef struct {                 /* one copy's entry points */
    void (*init)(void);
    void (*midi)(const uint8_t *m, int len);
    void (*render)(int16_t *out, int frames);
    void (*load)(uint32_t e, uint32_t pi);
    uint32_t (*engine)(void);
    uint32_t (*preset)(void);
    int32_t (*get)(uint32_t id);
    void (*set)(uint32_t id, int32_t v);
    int32_t (*gget)(uint32_t id);
    void (*gset)(uint32_t id, int32_t v);
    void (*desc)(uint32_t id, skc_desc_t *o);
    void (*panic)(void);
} skc_core_t;

/* the shared tables (built into copy 0) */
uint32_t skiff_engine_count(void);
uint32_t skiff_engine_at(uint32_t n);
const char *skiff_engine_name(uint32_t e);
uint32_t skiff_preset_count(uint32_t e);
const char *skiff_preset_name(uint32_t e, uint32_t pi);
uint32_t skiff_param_count(void);
uint32_t skiff_gparam_count(void);
void skiff_gdesc(uint32_t id, skc_desc_t *o);
extern const skc_key_t skiff_keys[];
extern const uint32_t skiff_nkeys;

/* the copies (skiff_cores.c) */
extern const skc_core_t skiff_cores[SKC_COPIES];
#endif
