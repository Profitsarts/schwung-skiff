/* SPDX-License-Identifier: GPL-3.0-only
 * Skiff Synth: the table of the SKC_COPIES copies of skiff_core.c built into dsp.so (scripts/build.sh). */
#include "skiff_core.h"

#define SKC_DECL(n)                                                                                           \
    void skc##n##_init(void);                                                                                 \
    void skc##n##_midi(const uint8_t *m, int len);                                                            \
    void skc##n##_render(int16_t *out, int frames);                                                           \
    void skc##n##_load(uint32_t e, uint32_t pi);                                                              \
    uint32_t skc##n##_engine(void);                                                                           \
    uint32_t skc##n##_preset(void);                                                                           \
    int32_t skc##n##_get(uint32_t id);                                                                        \
    void skc##n##_set(uint32_t id, int32_t v);                                                                \
    int32_t skc##n##_gget(uint32_t id);                                                                       \
    void skc##n##_gset(uint32_t id, int32_t v);                                                               \
    void skc##n##_desc(uint32_t id, skc_desc_t *o);                                                           \
    void skc##n##_panic(void);
#define SKC_ROW(n)                                                                                            \
    {skc##n##_init, skc##n##_midi, skc##n##_render, skc##n##_load, skc##n##_engine, skc##n##_preset,          \
     skc##n##_get, skc##n##_set, skc##n##_gget, skc##n##_gset, skc##n##_desc, skc##n##_panic}

#if SKC_COPIES != 8
#error "skiff_cores.c lists 8 copies: change both together"
#endif
SKC_DECL(0) SKC_DECL(1) SKC_DECL(2) SKC_DECL(3) SKC_DECL(4) SKC_DECL(5) SKC_DECL(6) SKC_DECL(7)
const skc_core_t skiff_cores[SKC_COPIES] = {
    SKC_ROW(0), SKC_ROW(1), SKC_ROW(2), SKC_ROW(3), SKC_ROW(4), SKC_ROW(5), SKC_ROW(6), SKC_ROW(7),
};
