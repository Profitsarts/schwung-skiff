/* SPDX-License-Identifier: GPL-3.0-only */
/* (Skiff Q2) THEME QUARK's widgets (design/mock_quark.py, A COLLIDER, the user's pick): a detector's event display. The
 * cards the periodic table's cells (bracket corners, an energy-level gauge), dotted rules, nuclei and their orbitals,
 * particle tracks curling in the field, collisions, the radiation sign on what can't be undone; the tracks the colour
 * charges. Our own drawings, on the canvas (cv_*) when ux.quark; Skiff's Barlow faces. Included by ui_graph.c. */

#define QK_COS(ph) sine_i((uint32_t)(ph) + 0x40000000u)   /* (dsp.c sine_i: a turn is 2^32, +-32767) */
#define QK_SIN(ph) sine_i((uint32_t)(ph))

/* a lab instrument's bracket corners round a box, L px each way */
static void qk_corners(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, int32_t l)
{
    cv_rect(x, y, l, 1, c);
    cv_rect(x, y, 1, l, c);
    cv_rect(x + w - l, y, l, 1, c);
    cv_rect(x + w - 1, y, 1, l, c);
    cv_rect(x, y + h - 1, l, 1, c);
    cv_rect(x, y + h - l, 1, l, c);
    cv_rect(x + w - l, y + h - 1, l, 1, c);
    cv_rect(x + w - 1, y + h - l, 1, l, c);
}

/* a dotted rule across the screen (the header's foot, the footer's top): a dot every 3 px */
static void qk_rule(int32_t y)
{
    int32_t x;
    for (x = 1; x < 240; x += 3)
        cv_rect(x, y, 1, 1, T_DIM);
}

/* a card's gauge: energy levels, a 2 x 2 dot every 4 px, lit up to the value (the hot card: INK) */
static void qk_dots(int32_t x, int32_t y, int32_t w, int32_t ratio, int hot)
{
    int32_t n = (w - 1) / 4, lit = (n * (ratio > 1000 ? 1000 : ratio) + 500) / 1000, k;
    for (k = 0; k < n; k++)
        cv_rect(x + k * 4, y, 2, 2, k < lit ? (hot ? T_INK : T_THEME) : hot ? ux_mix(T_INK, T_THEME, 60) :
                ux_mix(T_SURF, T_TEXT, 12));
}

/* an ellipse rx x ry turned by rot (a turn 2^32) round (cx, cy), n segments */
static void qk_ellipse(int32_t cx, int32_t cy, int32_t rx, int32_t ry, uint32_t rot, uint16_t c, uint32_t n)
{
    int32_t cr = QK_COS(rot), sr = QK_SIN(rot), px = 0, py = 0;
    uint32_t k;
    for (k = 0; k <= n; k++) {
        uint32_t ph = k * (0xFFFFFFFFu / n);   /* (32-bit: no 64-bit division in the firmware) */
        int32_t ex = rx * QK_COS(ph) / 32767, ey = ry * QK_SIN(ph) / 32767;
        int32_t x = cx + (ex * cr - ey * sr) / 32767, y = cy + (ex * sr + ey * cr) / 32767;
        if (k)
            cv_line(px, py, x, y, c);
        px = x;
        py = y;
    }
}

/* a disc, radius r */
static void qk_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    int32_t j, w;
    for (j = -r; j <= r; j++) {
        for (w = r; w * w + j * j > r * r + r / 2; w--)
            ;
        cv_rect(cx - w, cy + j, 2 * w + 1, 1, c);
    }
}

/* a nucleus: its quarks (red, green, blue), two orbitals round it, an electron on one (gold) */
static void qk_nucleus(int32_t x, int32_t y, uint16_t c, int32_t r)
{
    qk_ellipse(x, y, r, r * 38 / 100, 0x15555555u, ux_mix(c, T_SURF, 30), 24);
    qk_ellipse(x, y, r, r * 38 / 100, 0xEAAAAAABu, ux_mix(c, T_SURF, 55), 24);
    cv_rect(x - 2, y - 2, 2, 2, T_TRK(0));
    cv_rect(x + 1, y - 2, 2, 2, T_TRK(1));
    cv_rect(x - 1, y + 1, 2, 2, T_TRK(2));
    cv_rect(x + r * 7 / 10, y - r * 3 / 10, 2, 2, T_ACCENT);
}

/* a dotted ring round (cx, cy), radius r, n dots */
static void qk_dotring(int32_t cx, int32_t cy, int32_t r, uint32_t n, uint16_t c)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        uint32_t ph = k * (0xFFFFFFFFu / n);
        cv_rect(cx + r * QK_COS(ph) / 32767, cy + r * QK_SIN(ph) / 32767, 1, 1, c);
    }
}

/* a charged particle's track from (x, y), heading `dir` (a turn 2^32), curling one way (curl +-1) ever tighter as it
 * slows, `len` px, its first n drawn ((Skiff Q6) n < len: still flying out); its colour fading into `under`;
 * -> where it has got to, x << 16 | y */
static uint32_t qk_track(int32_t x, int32_t y, uint32_t dir, int32_t r, int32_t len, int32_t curl, uint16_t c,
                         uint16_t under, int32_t n)
{
    int32_t dx = QK_COS(dir) / 8, dy = QK_SIN(dir) / 8, fx = x << 12, fy = y << 12, k;   /* (Q12) */
    for (k = 0; k < n; k++) {
        int32_t rr = r - r * 6 * k / (10 * len), t = 4096 / (rr > 4 ? rr : 4) * curl, ndx = dx - dy * t / 4096;
        dy += dx * t / 4096;
        dx = ndx;
        fx += dx;
        fy += dy;
        cv_rect(fx >> 12, fy >> 12, 1, 1, ux_mix(c, under, 65 * k / len));
    }
    return (uint32_t)(fx >> 12) << 16 | ((uint32_t)(fy >> 12) & 0xFFFFu);
}

/* a collision at (x, y): n tracks out of it, each its colour charge, curling its own way (seed: which way) */
static void qk_burst(int32_t x, int32_t y, uint32_t n, uint32_t seed, int32_t len, uint16_t under)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        int32_t l;
        seed = seed * 1103515245u + 12345u;
        l = len / 2 + (int32_t)(seed >> 20) % (len / 2 + 1);
        qk_track(x, y, k * (0xFFFFFFFFu / n) + (seed >> 6 & 0x0FFFFFFFu), 10 + (int32_t)(seed >> 26) % 30, l,
                 (seed >> 16) & 1u ? 1 : -1, T_TRK(k), under, l);
    }
    qk_disc(x, y, 1, T_TEXT);
}

/* the radiation sign (what can't be undone), radius r: three blades round a hub */
static void qk_hazard(int32_t cx, int32_t cy, int32_t r, uint16_t c)
{
    static const int16_t U[3][2] = {{0, -1024}, {887, 512}, {-887, 512}};   /* the blades' middles: up, lower right, left */
    int32_t x, y, k;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++) {
            int32_t d2 = x * x + y * y, on = d2 * 100 <= r * r * 7;          /* the hub */
            if (d2 <= r * r && d2 * 100 >= r * r * 18)
                for (k = 0; k < 3; k++) {
                    int32_t dot = x * U[k][0] + y * U[k][1];
                    if (dot > 0 && dot * dot / 256 * 4 >= d2 * 4096 * 3)   /* (within 30 deg of it; r <= 12: 32-bit) */
                        on = 1;
                }
            if (on)
                cv_rect(cx + x, cy + y, 1, 1, c);
        }
}

/* a dotted outline (dark matter: there, but nothing shows), a dot every 3 px */
static void qk_dotframe(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t i;
    for (i = 0; i < w; i += 3) {
        cv_rect(x + i, y, 1, 1, c);
        cv_rect(x + i, y + h - 1, 1, 1, c);
    }
    for (i = 0; i < h; i += 3) {
        cv_rect(x, y + i, 1, 1, c);
        cv_rect(x + w - 1, y + i, 1, 1, c);
    }
}

/* REC: a red nucleus with its orbital (the selected track armed: lit; another: its orbital only) */
static void qk_rec(int32_t cx, int32_t cy, int full)
{
    qk_ellipse(cx, cy, 7, 3, 0x15555555u, ux_mix(T_REC, T_BG, 40), 20);
    if (full)
        qk_disc(cx, cy, 3, T_REC);
}

/* a Monte Carlo fill: the column x from below y to the foot, a sample kept where the hash of (x, y) falls under it, each
 * a shade of the theme into the panel (the same samples every time) */
static void qk_mc(int32_t x, int32_t y, int32_t foot)
{
    for (y += 2; y < foot; y++) {
        uint32_t h = ((uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u) * 2654435761u;
        if ((h >> 24) < 46u)
            cv_rect(x, y, 1, 1, ux_mix(T_THEME, T_SURF, 20 + (int32_t)(h >> 8 & 63u) * 45 / 63));
    }
}

/* a calorimeter tower w wide, h tall up to its foot y1: 4 px cells lit to fh px (the energy deposited), lighter up it,
 * the rest dark; cap: a line over the lit ones (0: none) */
static void qk_tower(int32_t x, int32_t y1, int32_t w, int32_t h, int32_t fh, uint16_t c, uint16_t cap)
{
    int32_t n = h / 4, lit = (fh * n + h / 2) / h, k;
    for (k = 0; k < n; k++)
        cv_rect(x, y1 - (k + 1) * 4, w, 3, k < lit ? ux_mix(c, T_TEXT, k * 50 / (n > 1 ? n : 1)) : ux_mix(T_SURF, T_TEXT, 10));
    if (cap)
        cv_rect(x - 2, y1 - lit * 4 - 2, w + 4, 1, cap);
}

/* a time's half-life (an envelope's DECAY, RELEASE: 99 % of the way in TIME_MS_X10, e^-4.6; half of it in 0.15 of
 * that): "T1/2 12 MS" */
static void qk_halflife(char *b, int16_t p)
{
    uint32_t ms10 = TIME_MS_X10[p & 127] * 15u / 100u;
    str_cpy(b, "T1/2 ", 8);
    if (ms10 < 100u)
        fmt_fix(b + 5, (int32_t)ms10, 1);
    else
        fmt_int(b + 5, (int32_t)((ms10 + 5u) / 10u));
    str_cpy(b + str_len(b), " MS", 4);
}
