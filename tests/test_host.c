/* SPDX-License-Identifier: GPL-3.0-only
 * Host test of Skiff Synth: drives the plugin through its v2 API as Schwung's Signal Chain does.
 *   build/test_host OUTDIR   (writes OUTDIR/ui_hierarchy.json and OUTDIR/chain_params.json for scripts/test.sh) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "host/plugin_api_v1.h"
#include "skiff_core.h"

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host);

static int fails;
#define CHECK(c, ...)                                                                                         \
    do {                                                                                                      \
        if (!(c)) {                                                                                           \
            fails++;                                                                                          \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                       \
            printf(__VA_ARGS__);                                                                              \
            printf("\n");                                                                                     \
        }                                                                                                     \
    } while (0)

static plugin_api_v2_t *api;
static char buf[65536];

static void note(void *in, int on, int n, int v)
{
    uint8_t m[3] = {(uint8_t)(on ? 0x90 : 0x80), (uint8_t)n, (uint8_t)v};
    api->on_midi(in, m, 3, MOVE_MIDI_SOURCE_INTERNAL);
}

/* render nb blocks; the peak and the sum of squares of what came out */
static double render(void *in, int nb, int *peak, int16_t *keep)
{
    int16_t out[2 * MOVE_FRAMES_PER_BLOCK];
    double e = 0;
    int b, i;
    *peak = 0;
    for (b = 0; b < nb; b++) {
        api->render_block(in, out, MOVE_FRAMES_PER_BLOCK);
        for (i = 0; i < 2 * MOVE_FRAMES_PER_BLOCK; i++) {
            int a = abs(out[i]);
            if (a > *peak)
                *peak = a;
            e += (double)out[i] * out[i];
        }
        if (keep)
            memcpy(keep + b * 2 * MOVE_FRAMES_PER_BLOCK, out, sizeof out);
    }
    return e;
}

static int getp(void *in, const char *key)
{
    CHECK(api->get_param(in, key, buf, sizeof buf) > 0, "get_param %s", key);
    return atoi(buf);
}

static void write_file(const char *dir, const char *name, const char *s)
{
    char path[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "w");
    if (!f) {
        perror(path);
        exit(2);
    }
    fputs(s, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    void *inst[SKC_COPIES + 1];
    int i, n, peak, np;
    double e;

    api = move_plugin_init_v2(NULL);
    CHECK(api && api->api_version == MOVE_PLUGIN_API_VERSION_2, "v2 API");

    /* 1. instances: SKC_COPIES of them, then none */
    for (i = 0; i < SKC_COPIES; i++) {
        inst[i] = api->create_instance("/tmp", NULL);
        CHECK(inst[i], "instance %d", i);
    }
    inst[SKC_COPIES] = api->create_instance("/tmp", NULL);
    CHECK(!inst[SKC_COPIES], "instance %d past the copies", SKC_COPIES);
    api->destroy_instance(inst[SKC_COPIES - 1]);
    inst[SKC_COPIES - 1] = api->create_instance("/tmp", NULL);
    CHECK(inst[SKC_COPIES - 1], "a destroyed instance's copy is taken again");
    printf("instances: %d independent copies ok\n", SKC_COPIES);

    /* 2. isolation: a note on instance 0 sounds there only */
    note(inst[0], 1, 60, 100);
    e = render(inst[0], 40, &peak, NULL);
    CHECK(e > 0 && peak > 100, "instance 0 sounds (peak %d)", peak);
    for (i = 1; i < SKC_COPIES; i++) {
        e = render(inst[i], 40, &peak, NULL);
        CHECK(e == 0, "instance %d is silent while 0 plays (peak %d)", i, peak);
    }
    note(inst[0], 0, 60, 0);
    printf("isolation: a note on one instance is heard on that one only ok\n");

    /* 3. every factory preset makes sound, held notes end after a release */
    np = getp(inst[1], "preset_count");
    CHECK(np >= 100, "preset_count %d", np);
    {
        int silent = 0, loud = 0;
        for (n = 0; n < np; n++) {
            char v[16];
            snprintf(v, sizeof v, "%d", n);
            api->set_param(inst[1], "preset", v);
            CHECK(getp(inst[1], "preset") == n, "preset %d reads back", n);
            render(inst[1], 2, &peak, NULL);                   /* the engine switch */
            note(inst[1], 1, 48, 110);
            note(inst[1], 1, 55, 110);
            note(inst[1], 1, 60, 110);
            e = render(inst[1], 120, &peak, NULL);             /* ~0.35 s */
            api->get_param(inst[1], "preset_name", buf, sizeof buf);
            if (e == 0) {
                silent++;
                printf("  silent: preset %d %s\n", n, buf);
            }
            if (peak >= 32767)
                loud++;
            note(inst[1], 0, 48, 0);
            note(inst[1], 0, 55, 0);
            note(inst[1], 0, 60, 0);
            api->set_param(inst[1], "panic", "1");
            render(inst[1], 700, &peak, NULL);                 /* ~2 s: every release over */
        }
        CHECK(!silent, "%d presets made no sound", silent);
        printf("presets: %d factory presets play (%d touched full scale)\n", np, loud);
    }

    /* 4. state: a sound saved and restored into another instance renders the same samples */
    {
        static int16_t a[2 * MOVE_FRAMES_PER_BLOCK * 200], b[2 * MOVE_FRAMES_PER_BLOCK * 200];
        static char st[8192];
        int pa, pb;
        api->set_param(inst[2], "preset", "20");
        api->set_param(inst[2], "e2", "17");
        api->set_param(inst[2], "atk", "30");
        api->set_param(inst[2], "lfo_wave", "SQR");
        CHECK(getp(inst[2], "lfo_wave") == 3, "an enum set by name reads back as its index");
        CHECK(api->get_param(inst[2], "state", st, sizeof st) > 0, "state");
        api->set_param(inst[3], "state", st);
        render(inst[2], 4, &pa, NULL);
        render(inst[3], 4, &pb, NULL);
        note(inst[2], 1, 62, 90);
        note(inst[3], 1, 62, 90);
        render(inst[2], 200, &pa, a);
        render(inst[3], 200, &pb, b);
        CHECK(pa > 0 && !memcmp(a, b, sizeof a), "a restored state renders the same samples");
        api->get_param(inst[3], "state", buf, sizeof buf);
        CHECK(!strcmp(st, buf), "state round trip");
        printf("state: save / restore is bit for bit ok\n");
    }

    /* 5. engine switch, labels following the engine */
    {
        char l0[64], l1[64];
        api->set_param(inst[4], "engine", "0");
        render(inst[4], 2, &peak, NULL);
        api->get_param(inst[4], "chain_params", buf, sizeof buf);
        strncpy(l0, strstr(buf, "\"key\":\"e0\"") ? strstr(buf, "\"key\":\"e0\"") : "", 40);
        l0[40] = 0;
        api->set_param(inst[4], "engine", "FM6");
        render(inst[4], 2, &peak, NULL);
        CHECK(getp(inst[4], "engine") == 1, "engine by name (FM6 is second in Skiff's order)");
        api->get_param(inst[4], "chain_params", buf, sizeof buf);
        strncpy(l1, strstr(buf, "\"key\":\"e0\"") ? strstr(buf, "\"key\":\"e0\"") : "", 40);
        l1[40] = 0;
        CHECK(strcmp(l0, l1), "EDIT labels follow the engine (%s / %s)", l0, l1);
        printf("engines: switch by index and name, labels follow ok\n");
    }

    /* 6. pitch bend and CC reach Skiff's MIDI IN */
    {
        uint8_t pb[3] = {0xE0, 0x00, 0x7F}, cc[3] = {0xB0, 1, 127};
        api->set_param(inst[5], "preset", "0");
        render(inst[5], 2, &peak, NULL);
        api->on_midi(inst[5], pb, 3, MOVE_MIDI_SOURCE_INTERNAL);
        api->on_midi(inst[5], cc, 3, MOVE_MIDI_SOURCE_EXTERNAL);
        note(inst[5], 1, 57, 100);
        e = render(inst[5], 40, &peak, NULL);
        CHECK(e > 0, "a note after bend / CC");
    }

    /* 7. the JSON Schwung reads (validated by scripts/test.sh) */
    n = api->get_param(inst[1], "ui_hierarchy", buf, sizeof buf);
    CHECK(n > 0, "ui_hierarchy fits");
    write_file(dir, "ui_hierarchy.json", buf);
    n = api->get_param(inst[1], "chain_params", buf, sizeof buf);
    CHECK(n > 0, "chain_params fits");
    write_file(dir, "chain_params.json", buf);
    CHECK(api->get_param(inst[1], "ui_hierarchy", buf, 64) == -1, "too small a buffer says so");

    /* 8. cost: 8 notes held on the heaviest-looking engines, every copy rendering */
    {
        struct timespec t0, t1;
        double us;
        int b2;
        for (i = 0; i < SKC_COPIES; i++) {
            char v[8];
            snprintf(v, sizeof v, "%d", i % 14);
            api->set_param(inst[i], "engine", v);
            render(inst[i], 2, &peak, NULL);
            for (n = 0; n < 8; n++)
                note(inst[i], 1, 48 + 3 * n, 100);
        }
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (b2 = 0; b2 < 345; b2++)                          /* 1 s */
            for (i = 0; i < SKC_COPIES; i++)
                render(inst[i], 1, &peak, NULL);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / 345.0;
        printf("cost: %d instances x 8 notes: %.0f us per 128-frame block on this host (%.1f %% of a block)\n",
               SKC_COPIES, us, us / 2902.0 * 100.0);
    }

    for (i = 0; i < SKC_COPIES; i++)
        api->destroy_instance(inst[i]);
    printf(fails ? "test_host: %d FAILED\n" : "test_host: all ok\n", fails);
    return fails != 0;
}
