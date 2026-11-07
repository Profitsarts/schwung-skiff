/* SPDX-License-Identifier: GPL-3.0-only
 *
 * Skiff Synth: Schwung sound generator (plugin API v2) around Skiff's sound core.
 *
 * Each instance takes one of the SKC_COPIES independent copies of the core (skiff_core.c, built once per copy),
 * because Skiff keeps all of its state in statics. Everything here runs on Schwung's audio callback (or on its
 * loader thread for create / destroy / the restoring set_param): no allocation, no file I/O, no locks, no logging.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host/plugin_api_v1.h"
#include "skiff_core.h"

typedef struct {
    const skc_core_t *c;
    int slot;
    char err[64];
} inst_t;

static inst_t g_inst[SKC_COPIES];
static int g_used[SKC_COPIES];          /* taken with an atomic compare-and-swap: create runs on the loader thread */

/* ------------------------------------------------------------- presets --- */
/* Move's preset list: every engine's factory presets, in Skiff's engine order (PRESETS page), as one list */
static uint32_t preset_total(void)
{
    uint32_t n = 0, k;
    for (k = 0; k < skiff_engine_count(); k++)
        n += skiff_preset_count(skiff_engine_at(k));
    return n;
}

static int preset_find(uint32_t idx, uint32_t *e, uint32_t *pi)
{
    uint32_t k;
    for (k = 0; k < skiff_engine_count(); k++) {
        uint32_t en = skiff_engine_at(k), n = skiff_preset_count(en);
        if (idx < n) {
            *e = en;
            *pi = idx;
            return 1;
        }
        idx -= n;
    }
    return 0;
}

static uint32_t preset_index(uint32_t e, uint32_t pi)
{
    uint32_t n = 0, k;
    for (k = 0; k < skiff_engine_count(); k++) {
        uint32_t en = skiff_engine_at(k);
        if (en == e)
            return n + pi;
        n += skiff_preset_count(en);
    }
    return 0;
}

static uint32_t engine_pos(uint32_t e)     /* ENGINES index -> its place in Skiff's engine order */
{
    uint32_t k;
    for (k = 0; k < skiff_engine_count(); k++)
        if (skiff_engine_at(k) == e)
            return k;
    return 0;
}

/* ------------------------------------------------------------ helpers --- */
static const skc_key_t *key_find(const char *key)
{
    uint32_t i;
    for (i = 0; i < skiff_nkeys; i++)
        if (!strcmp(skiff_keys[i].key, key))
            return &skiff_keys[i];
    return 0;
}

static void key_desc(const inst_t *in, const skc_key_t *k, skc_desc_t *d)
{
    if (k->global)
        skiff_gdesc(k->id, d);
    else
        in->c->desc(k->id, d);
}

static int32_t key_get(const inst_t *in, const skc_key_t *k)
{
    return k->global ? in->c->gget(k->id) : in->c->get(k->id);
}

static void key_set(const inst_t *in, const skc_key_t *k, int32_t v)
{
    if (k->global)
        in->c->gset(k->id, v);
    else
        in->c->set(k->id, v);
}

/* a value from Move: a number (an enum's index), or one of an enum's names */
static int parse_value(const skc_desc_t *d, const char *val, int32_t *out)
{
    char *end;
    long v = strtol(val, &end, 10);
    if (end != val && (*end == 0 || *end == '.')) {
        *out = d->is_enum ? d->min + (int32_t)v : (int32_t)v;
        return 1;
    }
    if (d->is_enum && d->names) {
        int32_t i;
        for (i = 0; i <= d->max - d->min; i++)
            if (d->names[i] && !strcmp(d->names[i], val)) {
                *out = d->min + i;
                return 1;
            }
    }
    return 0;
}

typedef struct {
    char *b;
    int n, cap;
} sb_t;

static void sb_put(sb_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void sb_put(sb_t *s, const char *fmt, ...)
{
    va_list ap;
    int w;
    if (s->n >= s->cap)
        return;
    va_start(ap, fmt);
    w = vsnprintf(s->b + s->n, (size_t)(s->cap - s->n), fmt, ap);
    va_end(ap);
    s->n = w < 0 ? s->cap : s->n + w > s->cap ? s->cap : s->n + w;
}

static void sb_str(sb_t *s, const char *str)   /* a JSON string */
{
    sb_put(s, "\"");
    for (; str && *str; str++) {
        if (*str == '"' || *str == '\\')
            sb_put(s, "\\%c", *str);
        else if ((unsigned char)*str >= 0x20)
            sb_put(s, "%c", *str);
    }
    sb_put(s, "\"");
}

static int sb_done(sb_t *s)
{
    if (s->n >= s->cap)
        return -1;                          /* did not fit: say so rather than hand back cut JSON */
    s->b[s->n] = 0;
    return s->n;
}

static void param_json(sb_t *s, const char *key, const skc_desc_t *d)
{
    sb_put(s, "{\"key\":");
    sb_str(s, key);
    sb_put(s, ",\"name\":");
    sb_str(s, d->label);
    if (d->is_enum && d->names) {
        int32_t i;
        sb_put(s, ",\"type\":\"enum\",\"options\":[");
        for (i = 0; i <= d->max - d->min; i++) {
            if (i)
                sb_put(s, ",");
            sb_str(s, d->names[i] ? d->names[i] : "");
        }
        sb_put(s, "],\"default\":%d}", d->def - d->min);
    } else {
        sb_put(s, ",\"type\":\"int\",\"min\":%d,\"max\":%d,\"default\":%d,\"step\":1}", d->min, d->max, d->def);
    }
}

static int chain_params_json(const inst_t *in, char *buf, int len)
{
    sb_t s = {buf, 0, len - 1};
    uint32_t i, k;
    sb_put(&s, "[{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":%u,\"refreshes_labels\":true},",
           preset_total() - 1u);
    sb_put(&s, "{\"key\":\"engine\",\"name\":\"Engine\",\"type\":\"enum\",\"refreshes_labels\":true,\"options\":[");
    for (k = 0; k < skiff_engine_count(); k++) {
        if (k)
            sb_put(&s, ",");
        sb_str(&s, skiff_engine_name(skiff_engine_at(k)));
    }
    sb_put(&s, "]}");
    for (i = 0; i < skiff_nkeys; i++) {
        skc_desc_t d;
        key_desc(in, &skiff_keys[i], &d);
        sb_put(&s, ",");
        param_json(&s, skiff_keys[i].key, &d);
    }
    sb_put(&s, "]");
    return sb_done(&s);
}

/* the pages: Skiff's EDIT 1 / 2 on the root (the engine's 8 values on the 8 knobs), then ENV, LFO, FX, VOICE */
static const struct {
    const char *level, *label;
    const char *keys[8];
} PAGES[] = {
    {"env", "Env", {"atk", "dec", "sus", "rel", "env_cut", "env_pit", "env_shp", "level"}},
    {"lfo", "LFO", {"lfo_rate", "lfo_wave", "lfo_pit", "lfo_cut", "lfo_shp", "lfo_amp", "lfo_fade", "lfo_phase"}},
    {"fx", "FX", {"dist", "chorus", "delay", "reverb", "dly_time", "dly_fdbk", "rev_size", "rev_type"}},
    {"voice", "Voice", {"voice", "glide", "detune", "pan", "transp", "gl_mode", "prio", "spread"}},
};

static void label_of(const inst_t *in, const char *key, sb_t *s)
{
    const skc_key_t *k = key_find(key);
    skc_desc_t d = {0};
    if (k)
        key_desc(in, k, &d);
    sb_put(s, "{\"key\":");
    sb_str(s, key);
    sb_put(s, ",\"label\":");
    sb_str(s, d.label ? d.label : key);
    sb_put(s, "}");
}

static int hierarchy_json(const inst_t *in, char *buf, int len)
{
    static const char *const ROOT[8] = {"e0", "e1", "e2", "e3", "e4", "e5", "e6", "e7"};
    sb_t s = {buf, 0, len - 1};
    uint32_t p, k;
    sb_put(&s, "{\"levels\":{\"root\":{\"name\":\"Skiff\",\"list_param\":\"preset\",\"count_param\":\"preset_count\","
               "\"name_param\":\"preset_name\",\"knobs\":[");
    for (k = 0; k < 8u; k++)
        sb_put(&s, "%s\"%s\"", k ? "," : "", ROOT[k]);
    sb_put(&s, "],\"params\":[{\"key\":\"engine\",\"label\":\"Engine\"}");
    for (k = 0; k < 8u; k++) {
        sb_put(&s, ",");
        label_of(in, ROOT[k], &s);
    }
    for (p = 0; p < sizeof PAGES / sizeof PAGES[0]; p++)
        sb_put(&s, ",{\"level\":\"%s\",\"label\":\"%s\"}", PAGES[p].level, PAGES[p].label);
    sb_put(&s, "]}");
    for (p = 0; p < sizeof PAGES / sizeof PAGES[0]; p++) {
        sb_put(&s, ",\"%s\":{\"name\":\"%s\",\"knobs\":[", PAGES[p].level, PAGES[p].label);
        for (k = 0; k < 8u; k++)
            sb_put(&s, "%s\"%s\"", k ? "," : "", PAGES[p].keys[k]);
        sb_put(&s, "],\"params\":[");
        for (k = 0; k < 8u; k++) {
            if (k)
                sb_put(&s, ",");
            label_of(in, PAGES[p].keys[k], &s);
        }
        sb_put(&s, "]}");
    }
    sb_put(&s, "}}");
    return sb_done(&s);
}

/* state: "skiff1|engine|preset|p0,p1,..|g0,g1,.." (every track and global value: the whole sound) */
static int state_get(const inst_t *in, char *buf, int len)
{
    sb_t s = {buf, 0, len - 1};
    uint32_t i;
    sb_put(&s, "skiff1|%u|%u|", in->c->engine(), in->c->preset());
    for (i = 0; i < skiff_param_count(); i++)
        sb_put(&s, "%s%d", i ? "," : "", (int)in->c->get(i));
    sb_put(&s, "|");
    for (i = 0; i < skiff_gparam_count(); i++)
        sb_put(&s, "%s%d", i ? "," : "", (int)in->c->gget(i));
    return sb_done(&s);
}

static void state_set(inst_t *in, const char *val)
{
    unsigned e, pi;
    const char *p;
    uint32_t i;
    if (strncmp(val, "skiff1|", 7) || sscanf(val + 7, "%u|%u|", &e, &pi) != 2)
        return;
    in->c->load(e, pi);
    p = strchr(val + 7, '|');
    p = p ? strchr(p + 1, '|') : 0;
    for (i = 0; p && *++p && *p != '|' && i < skiff_param_count(); i++) {
        char *end;
        long v = strtol(p, &end, 10);
        in->c->set(i, (int32_t)v);            /* every value as stored, bounded by its descriptor */
        p = end;
        if (*p != ',')
            break;
    }
    p = p ? strchr(p, '|') : 0;
    for (i = 0; p && *++p && i < skiff_gparam_count(); i++) {
        char *end;
        long v = strtol(p, &end, 10);
        in->c->gset(i, (int32_t)v);
        p = end;
        if (*p != ',')
            break;
    }
}

/* ------------------------------------------------------------- the API --- */
static void *v2_create(const char *module_dir, const char *json_defaults)
{
    int k;
    (void)module_dir;
    (void)json_defaults;
    for (k = 0; k < SKC_COPIES; k++) {
        int free_ = 0;
        if (__atomic_compare_exchange_n(&g_used[k], &free_, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            inst_t *in = &g_inst[k];
            in->c = &skiff_cores[k];
            in->slot = k;
            in->err[0] = 0;
            in->c->init();
            return in;
        }
    }
    return NULL;                              /* every copy is in use */
}

static void v2_destroy(void *p)
{
    inst_t *in = p;
    if (!in)
        return;
    in->c->panic();
    __atomic_store_n(&g_used[in->slot], 0, __ATOMIC_RELEASE);
}

static void v2_midi(void *p, const uint8_t *msg, int len, int source)
{
    inst_t *in = p;
    if (!in || source == MOVE_MIDI_SOURCE_TOUCH || source == MOVE_MIDI_SOURCE_FX_BROADCAST)
        return;
    in->c->midi(msg, len);
}

static void v2_set(void *p, const char *key, const char *val)
{
    inst_t *in = p;
    const skc_key_t *k;
    if (!in || !key || !val)
        return;
    if (!strcmp(key, "state")) {
        state_set(in, val);
    } else if (!strcmp(key, "preset")) {
        uint32_t e, pi;
        if (preset_find((uint32_t)atoi(val), &e, &pi))
            in->c->load(e, pi);
    } else if (!strcmp(key, "engine")) {
        uint32_t n = skiff_engine_count(), k2;
        char *end;
        long v = strtol(val, &end, 10);
        if (end == val) {
            for (k2 = 0, v = -1; k2 < n; k2++)
                if (!strcmp(skiff_engine_name(skiff_engine_at(k2)), val))
                    v = (long)k2;
        }
        if (v >= 0 && (uint32_t)v < n && skiff_engine_at((uint32_t)v) != in->c->engine())
            in->c->load(skiff_engine_at((uint32_t)v), 0);
    } else if (!strcmp(key, "all_notes_off") || !strcmp(key, "panic")) {
        in->c->panic();
    } else if ((k = key_find(key))) {
        skc_desc_t d;
        int32_t v;
        key_desc(in, k, &d);
        if (parse_value(&d, val, &v))
            key_set(in, k, v);
    }
}

static int v2_get(void *p, const char *key, char *buf, int len)
{
    inst_t *in = p;
    const skc_key_t *k;
    if (!in || !key || !buf || len <= 0)
        return -1;
    if (!strcmp(key, "ui_hierarchy"))
        return hierarchy_json(in, buf, len);
    if (!strcmp(key, "chain_params"))
        return chain_params_json(in, buf, len);
    if (!strcmp(key, "state"))
        return state_get(in, buf, len);
    if (!strcmp(key, "preset"))
        return snprintf(buf, (size_t)len, "%u", preset_index(in->c->engine(), in->c->preset()));
    if (!strcmp(key, "preset_count"))
        return snprintf(buf, (size_t)len, "%u", preset_total());
    if (!strcmp(key, "preset_name") || !strcmp(key, "name")) {
        uint32_t e = in->c->engine();
        return snprintf(buf, (size_t)len, "%s %s", skiff_engine_name(e), skiff_preset_name(e, in->c->preset()));
    }
    if (!strcmp(key, "engine"))
        return snprintf(buf, (size_t)len, "%u", engine_pos(in->c->engine()));
    if ((k = key_find(key))) {
        skc_desc_t d;
        int32_t v = key_get(in, k);
        key_desc(in, k, &d);
        return snprintf(buf, (size_t)len, "%d", d.is_enum ? v - d.min : v);
    }
    return -1;
}

static int v2_error(void *p, char *buf, int len)
{
    inst_t *in = p;
    if (!in || !in->err[0] || len <= 0)
        return 0;
    return snprintf(buf, (size_t)len, "%s", in->err);
}

static void v2_render(void *p, int16_t *out, int frames)
{
    inst_t *in = p;
    if (!in) {
        memset(out, 0, sizeof(int16_t) * 2u * (size_t)frames);
        return;
    }
    in->c->render(out, frames);
}

static plugin_api_v2_t g_api = {
    .api_version = MOVE_PLUGIN_API_VERSION_2,
    .create_instance = v2_create,
    .destroy_instance = v2_destroy,
    .on_midi = v2_midi,
    .set_param = v2_set,
    .get_param = v2_get,
    .get_error = v2_error,
    .render_block = v2_render,
};

__attribute__((visibility("default"))) plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    (void)host;
    return &g_api;
}
