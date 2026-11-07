/* SPDX-License-Identifier: GPL-3.0-only
 *
 * Skiff Synth for Ableton Move (Schwung): one copy of Skiff's sound core.
 *
 * Skiff (github.com/illuminus5/skiff, a fork of Felucca by Hügelton Instruments) keeps its whole state in
 * static variables: six tracks, their voices, the engines' memory, the FX buses. That is one synth per process.
 * So this file is compiled several times (scripts/build.sh, -DSKC_ID=0..SKC_COPIES-1): every copy is its own
 * translation unit with its own statics, and each Move instance takes one copy (skiff_plugin.c). Skiff's sources
 * are not changed; they are included as Skiff's own host build (vendor/skiff/tests/hostsim.c) and its browser
 * emulator (web/emu/felucca_web.c) include them.
 *
 * A copy plays Skiff's track 1 (trk[0]) only: Move's sequencer and Signal Chain replace Skiff's own sequencer,
 * song and the other five tracks. MIDI goes through Skiff's own MIDI IN path (midi_control.c midi_event), so
 * notes, pitch bend, CC1 / CC11 / aftertouch to the modulation matrix and sustain behave as on the FM-1.
 */
#ifndef SKC_ID
#define SKC_ID 0
#endif

#define FELUCCA_FLASH 0
#define FELUCCA_OTA 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 0
#define FELUCCA_CDC 0
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "SKIFF-MOVE"
#endif

/* Skiff's host build (tests/hostsim.c): libc shim, engines, voices, modulation, FX, the sequencer's MIDI IN.
 * Its test helpers come along unused; its main() is renamed out of the way, as web/emu/felucca_web.c does. */
#define main skc_hostsim_main_unused
#include "../../vendor/skiff/tests/hostsim.c"
#undef main

#include "skiff_core.h"

#define SKC_CAT2(a, b) a##b
#define SKC_CAT(a, b) SKC_CAT2(a, b)
#define SKC(name) SKC_CAT(SKC_CAT(skc, SKC_ID), SKC_CAT(_, name))

static track_t *const T = &trk[0];
static int32_t skc_out[2u * CTL];

/* -------------------------------------------------------------- per copy --- */
void SKC(init)(void)
{
    uint32_t k;
    host_tracks_init();                       /* Skiff's defaults (felucca_init's), empty patterns */
    for (k = 1; k < NTRK; k++)
        trk[k].p[P_MUTE] = 1;                 /* only track 1 plays here */
    host_preset(T, ENGINE_ORDER[0], 0);       /* ANALOG, its first preset, as Skiff starts */
}

void SKC(midi)(const uint8_t *m, int len)
{
    uint32_t st;
    if (len < 1)
        return;
    st = m[0] & 0xF0u;
    if (st < 0x80u || st == 0xF0u)
        return;                               /* realtime / sysex: not ours */
    /* every channel to track 1 (MIDI channel 1): the slot's own routing already picked what reaches us */
    midi_event(st, 0, len > 1 ? m[1] & 0x7Fu : 0, len > 2 ? m[2] & 0x7Fu : 0);
}

void SKC(render)(int16_t *out, int frames)
{
    int f = 0;
    while (f < frames) {
        uint32_t n = (uint32_t)(frames - f) < CTL ? (uint32_t)(frames - f) : CTL, i;
        mix_block(skc_out, n);                /* Skiff's mix: events, the track, its buses, the master */
        for (i = 0; i < 2u * n; i++) {
            int32_t s = skc_out[i];
            out[2 * f + (int)i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
        }
        f += (int)n;
    }
}

/* the engine e (an ENGINES index) and its factory preset pi, as Skiff's PRESETS page loads a sound: the engine
 * switches at the next block (eng_req), its values at once */
void SKC(load)(uint32_t e, uint32_t pi)
{
    if (e >= NENGINES || !eng_ok(e))
        return;
    host_preset_req(T, e, pi);
}

uint32_t SKC(engine)(void) { return T->eng_req; }
uint32_t SKC(preset)(void) { return T->preset; }

int32_t SKC(get)(uint32_t id) { return id < P_COUNT ? T->p[id] : 0; }

void SKC(set)(uint32_t id, int32_t v)
{
    const param_desc_t *d;
    if (id >= P_COUNT)
        return;
    d = track_desc(T, id);
    T->p[id] = (int16_t)(v < d->min ? d->min : v > d->max ? d->max : v);
    if (id >= P_E0 && T->eng_req == ENGI_FM6)
        fm6_track_loaded(T);                  /* FM6: its patch follows the PATCH value (ui.c, editor) */
}

int32_t SKC(gget)(uint32_t id) { return id < G_COUNT ? song.g[id] : 0; }

void SKC(gset)(uint32_t id, int32_t v)
{
    if (id >= G_COUNT)
        return;
    v = v < GP[id].min ? GP[id].min : v > GP[id].max ? GP[id].max : v;
    song.g[id] = (int16_t)v;
}

/* the descriptor Skiff shows for parameter id on this track (mode-dependent labels: track_desc) */
void SKC(desc)(uint32_t id, skc_desc_t *o)
{
    const param_desc_t *d = id < P_COUNT ? track_desc(T, id) : &TP[P_LEVEL];
    o->label = d->label;
    o->names = d->names;
    o->min = d->min;
    o->max = d->max;
    o->def = d->def;
    o->is_enum = d->names != 0;
}

void SKC(panic)(void)
{
    uint32_t v;
    for (v = 0; v < NVOICE; v++)
        T->v[v].gate = 0;
    midi_silence_track(0);
}

/* ------------------------------------------- shared tables (copy 0 only) --- */
#if SKC_ID == 0
uint32_t skiff_engine_count(void) { return NENG_SHOWN; }
uint32_t skiff_engine_at(uint32_t n) { return eng_vis(n); }          /* display order -> ENGINES index */
const char *skiff_engine_name(uint32_t e) { return e < NENGINES ? ENGINES[e]->name : ""; }
uint32_t skiff_preset_count(uint32_t e) { return e < NENGINES ? ENGINES[e]->npresets : 0; }
const char *skiff_preset_name(uint32_t e, uint32_t pi)
{
    return e < NENGINES && pi < ENGINES[e]->npresets ? ENGINES[e]->presets[pi].name : "";
}

/* the parameters Move gets, by page: Skiff's own pages (EDIT 1 / 2, ENV, LFO, FX, VOICE) as 8-knob pages.
 * Skiff's names and ranges; G = a global of the copy's song (its delay and reverb are this instance's own) */
#define TK(k, id) {k, 0, id}
#define GK(k, id) {k, 1, id}
const skc_key_t skiff_keys[] = {
    TK("e0", P_E0), TK("e1", P_E1), TK("e2", P_E2), TK("e3", P_E3),
    TK("e4", P_E4), TK("e5", P_E5), TK("e6", P_E6), TK("e7", P_E7),
    TK("atk", P_ATK), TK("dec", P_DEC), TK("sus", P_SUS), TK("rel", P_REL),
    TK("env_cut", P_ED_FLT), TK("env_pit", P_ED_PIT), TK("env_shp", P_ED_SHP), TK("level", P_LEVEL),
    TK("lfo_rate", P_LRATE), TK("lfo_wave", P_LWAVE), TK("lfo_pit", P_LD_PIT), TK("lfo_cut", P_LD_FLT),
    TK("lfo_shp", P_LD_SHP), TK("lfo_amp", P_LD_AMP), TK("lfo_fade", P_LFADE), TK("lfo_phase", P_LPHASE),
    TK("dist", P_DIST), TK("chorus", P_CHOR), TK("delay", P_DLY), TK("reverb", P_REV),
    GK("dly_time", G_DTIME), GK("dly_fdbk", G_DFDBK), GK("rev_size", G_RSIZE), GK("rev_type", G_RTYPE),
    TK("voice", P_VOICE), TK("glide", P_GLIDE), TK("detune", P_DETUNE), TK("pan", P_PAN),
    TK("transp", P_TRANS), TK("gl_mode", P_GLMODE), TK("prio", P_PRIO), TK("spread", P_SPRD),
};
const uint32_t skiff_nkeys = sizeof skiff_keys / sizeof skiff_keys[0];
#undef TK
#undef GK
uint32_t skiff_param_count(void) { return P_COUNT; }
uint32_t skiff_gparam_count(void) { return G_COUNT; }
void skiff_gdesc(uint32_t id, skc_desc_t *o)
{
    const param_desc_t *d = &GP[id < G_COUNT ? id : 0];
    o->label = d->label;
    o->names = d->names;
    o->min = d->min;
    o->max = d->max;
    o->def = d->def;
    o->is_enum = d->names != 0;
}
#endif
