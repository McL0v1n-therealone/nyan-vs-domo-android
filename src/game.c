/* Nyan Cat vs Domo-kun -- game core.
 * Ported from the PSP homebrew version: same software renderer, same rules,
 * same synthesized sound. PSP buttons are replaced by touch controls. */
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include "game.h"
#include "font8x8.h"

#define W GAME_W
#define H GAME_H
#define WH 544                    /* world height: two screens tall, camera follows */
#define STRIDE GAME_STRIDE
#define S 3                       /* cat art-pixel size in screen pixels */
#ifndef START_SCORE
#define START_SCORE 0             /* debug: build with -DSTART_SCORE=N to test later difficulty */
#endif
#define RGB(r, g, b) (0xFF000000u | ((b) << 16) | ((g) << 8) | (r)) /* ABGR = RGBA bytes */

unsigned int game_fb[STRIDE * H];
static unsigned int *cur = game_fb;

static unsigned int rng = 0x1234567;
static unsigned int rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
static float frnd(void) { return (rnd() % 10000) / 10000.f; }

/* =====================================================================
 * Audio: meow (pre-synthesized), laser buzz and explosions (real-time)
 * ===================================================================== */
#define RATE AUDIO_RATE
#define MEOW_LEN (RATE * 7 / 10)      /* 0.7 s */
static short meow[MEOW_LEN];
static volatile int meow_pos = -1;    /* -1 = silent; owned by audio thread */
static volatile int meow_req = 0;     /* requests from main thread */
static volatile int boom_req = 0;
static volatile int laser_on = 0;
static volatile int spit_req = 0;
static volatile int elaser_on = 0;   /* a Domo is firing its eye lasers */

/* sine lookup for the audio thread: phase in [0,1) */
#define SINE_N 1024
static float sine_lut[SINE_N];
static float boom_decay;              /* per-sample envelope multiplier */
static void audio_tables_init(void) {
    for (int i = 0; i < SINE_N; i++) sine_lut[i] = sinf(2.f * (float)M_PI * i / SINE_N);
    boom_decay = expf(-9.f / RATE);
}
static inline float fsin01(float ph) { return sine_lut[(int)(ph * SINE_N) & (SINE_N - 1)]; }

static void synth_meow(void) {
    float phase = 0.f;
    for (int n = 0; n < MEOW_LEN; n++) {
        float t = (float)n / MEOW_LEN;
        float f0 = t < 0.3f ? 550.f + 350.f * (t / 0.3f)          /* "mi-" up */
                            : 900.f - 450.f * ((t - 0.3f) / 0.7f); /* "-aow" down */
        float formant = t < 0.35f ? 2200.f - 1000.f * (t / 0.35f)
                                  : 1200.f - 550.f * ((t - 0.35f) / 0.65f);
        float env = t < 0.05f ? t / 0.05f : (t > 0.55f ? (1.f - t) / 0.45f : 1.f);
        phase += 2.f * (float)M_PI * f0 / RATE;
        if (phase > 2.f * (float)M_PI * 64) phase -= 2.f * (float)M_PI * 64;
        float s = 0.f;
        for (int h = 1; h <= 10; h++) {
            float d = (h * f0 - formant) / 500.f;
            s += expf(-d * d) * sinf(phase * h) / (0.5f + 0.1f * h);
        }
        s *= env * (1.f + 0.15f * sinf(2.f * (float)M_PI * 6.f * n / RATE));
        int v = (int)(s * 7000.f);
        meow[n] = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
    }
}

/* called from the audio callback thread for every buffer */
void game_audio(short *out, int CHUNK) {
    static float lphase = 0.f, lfo = 0.f, lvol = 0.f, bphase = 0.f;
    static float ephase = 0.f, elfo = 0.f, evol = 0.f, benv = 0.f;
    static int boom_t = -1, spit_t = -1;
    static unsigned int nseed = 1;
    {
        if (meow_req) { meow_req = 0; meow_pos = 0; }
        if (boom_req) { boom_req = 0; boom_t = 0; benv = 1.f; }
        if (spit_req) { spit_req = 0; spit_t = 0; }
        int pos = meow_pos;
        for (int i = 0; i < CHUNK; i++) {
            int s = 0;
            if (pos >= 0 && pos < MEOW_LEN) s += meow[pos++];
            /* laser: wobbling saw-wave buzz with click-free fade */
            lvol += ((laser_on ? 1.f : 0.f) - lvol) * 0.002f;
            if (lvol > 0.001f) {
                lfo += 30.f / RATE;
                if (lfo >= 1.f) lfo -= 1.f;
                lphase += (300.f + 120.f * fsin01(lfo)) / RATE;
                if (lphase >= 1.f) lphase -= 1.f;
                s += (int)((lphase * 2.f - 1.f) * 3000.f * lvol);
            }
            /* Domo eye lasers: low, harsh, fast-wobbling hum */
            evol += ((elaser_on ? 1.f : 0.f) - evol) * 0.003f;
            if (evol > 0.001f) {
                elfo += 55.f / RATE;
                if (elfo >= 1.f) elfo -= 1.f;
                ephase += (130.f + 40.f * fsin01(elfo)) / RATE;
                if (ephase >= 1.f) ephase -= 1.f;
                s += (int)((ephase < 0.5f ? 1.f : -1.f) * 2200.f * evol);
            }
            /* explosion: noise burst + falling thump */
            if (boom_t >= 0) {
                float t = (float)boom_t / RATE;
                if (t > 0.45f) boom_t = -1;
                else {
                    nseed = nseed * 1103515245u + 12345u;
                    float noise = (float)((nseed >> 16) & 0xFFFF) / 32768.f - 1.f;
                    bphase += (150.f - 220.f * t) / RATE;
                    if (bphase >= 1.f) bphase -= 1.f;
                    benv *= boom_decay;
                    s += (int)((noise * 0.6f + fsin01(bphase) * 0.8f) * benv * 9000.f);
                    boom_t++;
                }
            }
            /* Domo spit: short descending "bloop" (square wave) */
            if (spit_t >= 0) {
                float t = (float)spit_t / RATE;
                if (t > 0.15f) spit_t = -1;
                else {
                    float fq = 500.f - 2000.f * t;
                    s += (((int)(spit_t * fq / RATE * 2.f)) & 1 ? 2200 : -2200) * (1.f - t / 0.15f);
                    spit_t++;
                }
            }
            s = s > 32767 ? 32767 : (s < -32768 ? -32768 : s);
            out[2 * i] = out[2 * i + 1] = (short)s;
        }
        if (pos >= 0) meow_pos = pos >= MEOW_LEN ? -1 : pos;
    }
}

/* =====================================================================
 * Drawing primitives
 * ===================================================================== */
static void rect(int x, int y, int w, int h, unsigned int c) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    for (int j = y0; j < y1; j++) {
        unsigned int *p = cur + j * STRIDE;
        for (int i = x0; i < x1; i++) p[i] = c;
    }
}
#define PX(ox, oy, ux, uy, c) rect((ox) + (ux) * S, (oy) + (uy) * S, S, S, (c))

static const unsigned int BLACK = RGB(0, 0, 0), WHITE = RGB(0xFF, 0xFF, 0xFF);

/* one 8x8 glyph of the PSP debug font (MSB = leftmost pixel) */
static void put_char(int x, int y, unsigned int c, unsigned char ch) {
    const unsigned char *g = font8x8 + ch * 8;
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++)
            if (g[j] & (0x80 >> i)) cur[(y + j) * STRIDE + x + i] = c;
}

/* small 8x8 text with a drop shadow */
static void text(int x, int y, const char *s, unsigned int c) {
    for (; *s; s++, x += 8) {
        if (x < 0 || x > W - 9 || y < 0 || y > H - 9) continue;
        put_char(x + 1, y + 1, BLACK, *s);
        put_char(x, y, c, *s);
    }
}

/* big text: the 8x8 font scaled up, shadow pass first */
static void big_text_at(int x, int y, int sc, const char *s, unsigned int c) {
    int n = strlen(s);
    if (n > 60) n = 60;
    for (int pass = 0; pass < 2; pass++)
        for (int k = 0; k < n; k++) {
            const unsigned char *g = font8x8 + (unsigned char)s[k] * 8;
            for (int j = 0; j < 8; j++)
                for (int i = 0; i < 8; i++)
                    if (g[j] & (0x80 >> i)) {
                        int o = pass ? 0 : sc / 2 + 1;   /* pass 0 = shadow */
                        rect(x + (k * 8 + i) * sc + o, y + j * sc + o, sc, sc, pass ? c : BLACK);
                    }
        }
}
static void big_text(int y, int sc, const char *s, unsigned int c) {
    int n = strlen(s);
    if (n > 60) n = 60;
    big_text_at((W - n * 8 * sc) / 2, y, sc, s, c);
}

/* alpha-blend a color over a rectangle (a = 0..256) */
static void blend(int x, int y, int w, int h, unsigned int c, int a) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    unsigned int rb = c & 0x00FF00FF, g = c & 0x0000FF00;
    for (int j = y0; j < y1; j++) {
        unsigned int *p = cur + j * STRIDE;
        for (int i = x0; i < x1; i++) {
            unsigned int d = p[i];
            unsigned int drb = d & 0x00FF00FF, dg = d & 0x0000FF00;
            drb = ((rb * a + drb * (256 - a)) >> 8) & 0x00FF00FF;
            dg = ((g * a + dg * (256 - a)) >> 8) & 0x0000FF00;
            p[i] = 0xFF000000 | drb | dg;
        }
    }
}

/* translucent disc (thick = 0) or ring of the given thickness */
static void circle(int cx0, int cy0, int r, int thick, unsigned int c, int a) {
    int ri = thick > 0 ? r - thick : -1;
    for (int dy = -r; dy <= r; dy++) {
        int ho = (int)sqrtf((float)(r * r - dy * dy));
        if (ri < 0 || dy * dy >= ri * ri) {
            blend(cx0 - ho, cy0 + dy, 2 * ho + 1, 1, c, a);
        } else {
            int hi = (int)sqrtf((float)(ri * ri - dy * dy));
            blend(cx0 - ho, cy0 + dy, ho - hi, 1, c, a);
            blend(cx0 + hi + 1, cy0 + dy, ho - hi, 1, c, a);
        }
    }
}

/* darken a screen area (for menus/overlays) */
static void dim(int x, int y, int w, int h) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    for (int j = y0; j < y1; j++) {
        unsigned int *p = cur + j * STRIDE;
        for (int i = x0; i < x1; i++) p[i] = ((p[i] >> 1) & 0x007F7F7F) | 0xFF000000;
    }
}

/* =====================================================================
 * Nyan cat
 * ===================================================================== */
static const char *head[] = {
    "..kk........kk..",
    ".kggk......kggk.",
    ".kgggk....kgggk.",
    ".kggggkkkkggggk.",
    ".kggggggggggggk.",
    "kggggggggggggggk",
    "kgggwkggggwkgggk",
    "kgggkkggkgkkgggk",
    "kgrrggggggggrrgk",
    "kgrrgkggkggkrrgk",
    "kgggggkkgkkggggk",
    ".kggggggggggggk.",
    "..kkkkkkkkkkkk..",
};

static unsigned int cat_pal(char ch) {
    switch (ch) {
    case 'k': return BLACK;
    case 'g': return RGB(0x99, 0x99, 0x99);
    case 'w': return WHITE;
    case 'r': return RGB(0xFF, 0x99, 0x99);
    default:  return 0;
    }
}

static void head_pos(int f, int *hx, int *hy) {
    int bob = (f & 2) ? -1 : 0;
    *hx = 13 + ((f & 2) ? 1 : 0);   /* head overhangs the pop-tart's right edge */
    *hy = 5 + ((f & 1) ? 0 : -1) + bob;
}

/* eye centers in screen pixels (for lasers) */
static void cat_eyes(int ox, int oy, int f, int ex[2], int *ey) {
    int hx, hy;
    head_pos(f, &hx, &hy);
    ex[0] = ox + (hx + 5) * S;
    ex[1] = ox + (hx + 11) * S;
    *ey = oy + (hy + 7) * S;
}

static void draw_cat(int ox, int oy, int f) {
    const unsigned int K = BLACK, G = cat_pal('g');
    const unsigned int TAN = RGB(0xFF, 0xCC, 0x99), PINK = RGB(0xFF, 0x99, 0xFF);
    const unsigned int SPR = RGB(0xFF, 0x33, 0x99);
    int leg = (f & 1) ? 1 : 0;
    int bob = (f & 2) ? -1 : 0;

    /* tail: stepped grey bar that wags up/down */
    static const signed char tail_dy[2][6] = {{0, 0, -1, -1, -2, -2}, {0, 0, 1, 1, 1, 0}};
    const signed char *td = tail_dy[f & 1];
    for (int i = 0; i < 6; i++) {               /* outline pass */
        int y = 10 + td[i] + bob;
        PX(ox, oy, -1 - i, y - 1, K); PX(ox, oy, -1 - i, y + 1, K);
    }
    PX(ox, oy, -7, 10 + td[5] + bob, K);         /* tip cap */
    for (int i = 0; i < 6; i++) PX(ox, oy, -1 - i, 10 + td[i] + bob, G);

    /* legs */
    static const int lx[4] = {1, 4, 15, 19};
    for (int i = 0; i < 4; i++) {
        int x = lx[i] + leg - 1;
        rect(ox + (x - 1) * S, oy + (16 + bob) * S, 4 * S, 4 * S, K);
        rect(ox + x * S, oy + (16 + bob) * S, 2 * S, 3 * S, G);
    }

    /* pop-tart body */
    int by = oy + bob * S;
    rect(ox + S, by, 19 * S, 18 * S, K);
    rect(ox, by + S, 21 * S, 16 * S, K);
    rect(ox + 2 * S, by + S, 17 * S, 16 * S, TAN);
    rect(ox + S, by + 2 * S, 19 * S, 14 * S, TAN);
    rect(ox + 4 * S, by + 3 * S, 13 * S, 12 * S, PINK);
    rect(ox + 3 * S, by + 4 * S, 15 * S, 10 * S, PINK);
    static const signed char spr[][2] = {
        {5, 4}, {9, 3}, {12, 5}, {15, 4}, {4, 8}, {7, 6}, {10, 9},
        {14, 7}, {6, 11}, {9, 13}, {5, 13}, {13, 12}, {16, 10},
    };
    for (unsigned i = 0; i < sizeof spr / sizeof spr[0]; i++)
        PX(ox, by, spr[i][0], spr[i][1], SPR);

    /* head */
    int hx, hy;
    head_pos(f, &hx, &hy);
    for (int j = 0; j < 13; j++)
        for (int i = 0; i < 15; i++) {
            char ch = head[j][i];
            if (ch != '.') PX(ox, oy, hx + i, hy + j, cat_pal(ch));
        }

    if (laser_on) {                  /* glowing eyes */
        unsigned int R = RGB(0xFF, 0x20, 0x20);
        PX(ox, oy, hx + 4, hy + 6, R); PX(ox, oy, hx + 5, hy + 6, R);
        PX(ox, oy, hx + 4, hy + 7, R); PX(ox, oy, hx + 5, hy + 7, R);
        PX(ox, oy, hx + 10, hy + 6, R); PX(ox, oy, hx + 11, hy + 6, R);
        PX(ox, oy, hx + 10, hy + 7, R); PX(ox, oy, hx + 11, hy + 7, R);
    }
    if (meow_pos >= 0) {             /* open mouth while meowing */
        rect(ox + (hx + 5) * S, oy + (hy + 9) * S, 7 * S, 3 * S, K);
        rect(ox + (hx + 7) * S, oy + (hy + 10) * S, 3 * S, S, RGB(0xFF, 0x55, 0x77));
    }
}

/* rainbow follows the cat's past y positions, so it curves when moving */
#define HIST 256
static int yhist[HIST];
static const unsigned int RAINBOW[6] = {
    RGB(0xFF, 0x00, 0x00), RGB(0xFF, 0x99, 0x00), RGB(0xFF, 0xFF, 0x00),
    RGB(0x33, 0xFF, 0x00), RGB(0x00, 0x99, 0xFF), RGB(0x66, 0x33, 0xFF),
};

static void draw_rainbow(int ox, int camy, unsigned int tick, int f) {
    int band = 3 * S, seg = 8 * S;
    for (int x = ox + 8 * S, k = 0; x > 0; x -= seg, k++) {
        int dy = ((k + f) & 1) ? S : 0;
        int y = yhist[(tick - k * 3) & (HIST - 1)] - camy;
        for (int b = 0; b < 6; b++)
            rect(x - seg, y + dy + b * band, seg, band, RAINBOW[b]);
    }
}

static void draw_laser(int x0, int y0, float slope, unsigned int tick) {
    int glow = 8 + (tick & 2) * 2;   /* flicker */
    for (int x = x0; x < W; x += 2) {
        int y = y0 + (int)((x - x0) * slope);
        rect(x, y - glow / 2, 2, glow, RGB(0xFF, 0x10, 0x30));
        rect(x, y - 2, 2, 4, RGB(0xFF, 0x90, 0x90));
        rect(x, y - 1, 2, 2, WHITE);
    }
    rect(x0 - 5, y0 - 5, 10, 10, RGB(0xFF, 0x60, 0x60));
    rect(x0 - 2, y0 - 2, 4, 4, WHITE);
}
#define LASER_SLOPE 0.10f

/* =====================================================================
 * Domo-kun
 * ===================================================================== */
#include "domo_sprite.h"      /* generated by tools/domo_from_image.py from the Domo-kun photo */
#include "title_img.h"        /* generated by tools/make_logo.py */
#define DOMO_HP 16
#define CHARGE_T 28                 /* telegraph time before a Domo attacks */

/* palette variants: normal, hit flash (washed out), charging (mouth glows, 2 phases) */
static unsigned int domo_cols[4][DOMO_NPAL];

static void domo_init_palettes(void) {
    for (int i = 0; i < DOMO_NPAL; i++) {
        int r = domo_pal_rgb[i][0], g = domo_pal_rgb[i][1], b = domo_pal_rgb[i][2];
        domo_cols[0][i] = RGB(r, g, b);
        domo_cols[1][i] = RGB((r + 3 * 255) / 4, (g + 3 * 230) / 4, (b + 3 * 200) / 4);
        domo_cols[2][i] = domo_pal_mouth[i] ? RGB(0xFF, 0xA0, 0x20) : RGB(r, g, b);
        domo_cols[3][i] = domo_pal_mouth[i] ? RGB(0xFF, 0xFF, 0x80) : RGB(r, g, b);
    }
}

/* mode: 0 normal, 1 hit flash, 2 charging (mouth glows) */
static void draw_domo(int x, int y, int mode, unsigned int tick) {
    if (x >= W || x + DOMO_W <= 0 || y >= H || y + DOMO_H <= 0) return;
    if (mode == 2) x += (tick & 2) ? 1 : -1;     /* shake while charging */
    const unsigned int *pal = domo_cols[mode == 2 ? 2 + ((tick >> 2) & 1) : mode];
    int x0 = x < 0 ? -x : 0, x1 = x + DOMO_W > W ? W - x : DOMO_W;
    for (int j = 0; j < DOMO_H; j++) {
        int sy = y + j;
        if (sy < 0 || sy >= H) continue;
        unsigned int *row = cur + sy * STRIDE + x;
        const unsigned char *p = domo_pix[j];
        for (int i = x0; i < x1; i++)
            if (p[i]) row[i] = pal[p[i]];
    }
}

/* attack types; later ones unlock as the score rises */
enum { A_BURST, A_FAN, A_LUNGE, A_HOMING, A_RING, A_LASER, A_COUNT };
static const int attack_unlock[A_COUNT] = {0, 0, 0, 4, 10, 0};
#define LZ_AIM 45                   /* eye-laser warning: tracks the cat... */
#define LZ_LOCK 16                  /* ...then locks its aim for the last frames */
#define LZ_FIRE 32                  /* beam duration */
#define LZ_HIT_R 17.f               /* beam half-width for hitting the cat */

#define MAXDOMO 16
static struct {
    int alive, hp, flash, cool, charge, kind, burst, burst_t, lunge, lz, lzf;
    float x, y0, spd, ph, lvx, lvy, lz_ang;
} domos[MAXDOMO];
static float domo_y(int i, unsigned int tick) {
    return domos[i].y0 + 18.f * sinf(tick * 0.05f + domos[i].ph);
}

/* Domo spit balls: aimed, some home in; all can be shot down with the lasers */
#define MAXB 96
static struct { int alive, homing; float x, y, vx, vy; } bullets[MAXB];

static void fire_bullet(float x, float y, float ang, float spd, int homing) {
    for (int i = 0; i < MAXB; i++) {
        if (bullets[i].alive) continue;
        bullets[i].alive = 1;
        bullets[i].homing = homing;
        bullets[i].x = x; bullets[i].y = y;
        bullets[i].vx = cosf(ang) * spd;
        bullets[i].vy = sinf(ang) * spd;
        return;
    }
}

static void draw_bullet(int x, int y, int homing, unsigned int tick) {
    unsigned int O = homing ? RGB(0x30, 0x00, 0x40) : RGB(0x50, 0x00, 0x10);
    unsigned int R = homing ? RGB(0xE0, 0x40, 0xFF) : RGB(0xFF, 0x50, 0x20);
    unsigned int C = (tick & 4) ? RGB(0xFF, 0xFF, 0x80) : WHITE;
    rect(x - 3, y - 5, 6, 10, O); rect(x - 5, y - 3, 10, 6, O); rect(x - 4, y - 4, 8, 8, O);
    rect(x - 2, y - 4, 4, 8, R);  rect(x - 4, y - 2, 8, 4, R);  rect(x - 3, y - 3, 6, 6, R);
    rect(x - 1, y - 2, 2, 4, C);  rect(x - 2, y - 1, 4, 2, C);
}

/* =====================================================================
 * Particles, stars
 * ===================================================================== */
#define MAXP 256
static struct { float x, y, vx, vy; int life; unsigned int c; } parts[MAXP];

static void explode(float x, float y, int n) {
    static const unsigned int cols[] = {
        RGB(0xFF, 0xFF, 0x80), RGB(0xFF, 0xA0, 0x20), RGB(0xFF, 0x40, 0x20),
        RGB(0x8B, 0x4A, 0x22), WHITE,
    };
    for (int i = 0; i < MAXP && n > 0; i++) {
        if (parts[i].life > 0) continue;
        float a = frnd() * 2.f * (float)M_PI, sp = 1.f + frnd() * 4.f;
        parts[i].x = x; parts[i].y = y;
        parts[i].vx = cosf(a) * sp; parts[i].vy = sinf(a) * sp;
        parts[i].life = 20 + rnd() % 25;
        parts[i].c = cols[rnd() % 5];
        n--;
    }
}

#define NSTARS 40
static struct { int x, y, spd, phase; } stars[NSTARS];

static void draw_star(int x, int y, int ph) {
    int r = 1 + (ph / 8) % 3;
    rect(x - 1, y - 1, 3, 3, WHITE);
    for (int i = 2; i <= r + 1; i++) {
        rect(x - 1 - i * 2, y - 1, 2, 3, WHITE); rect(x + 1 + (i - 1) * 2, y - 1, 2, 3, WHITE);
        rect(x - 1, y - 1 - i * 2, 3, 2, WHITE); rect(x - 1, y + 1 + (i - 1) * 2, 3, 2, WHITE);
    }
}

static const char *heart[] = {
    ".rr.rr.",
    "rrrrrrr",
    "rrrrrrr",
    ".rrrrr.",
    "..rrr..",
    "...r...",
};

/* =====================================================================
 * Game
 * ===================================================================== */
enum { TITLE, PLAYING, PAUSED, CREDITS, DEAD };
static int state, score, hiscore, lives, inv, spawn_timer, cx, cy;
static unsigned int start_tick, dead_tick;
static float camf;
static float cxf, cyf;              /* sub-pixel cat position for analog movement */

#define CAT_W (30 * S)
#define CAT_H (18 * S)
#define XMIN (6 * S)
#define XMAX (W - 31 * S)
#define YMIN (2 * S)
#define YMAX (WH - 21 * S)

static void reset(unsigned int tick) {
    state = PLAYING; score = START_SCORE; lives = 3; inv = 90; spawn_timer = 90;
    cx = 80; cy = (WH - CAT_H) / 2;
    cxf = cx; cyf = cy;
    camf = cy + CAT_H / 2 - H / 2;
    for (int i = 0; i < HIST; i++) yhist[i] = cy;
    for (int i = 0; i < MAXDOMO; i++) domos[i].alive = 0;
    for (int i = 0; i < MAXP; i++) parts[i].life = 0;
    for (int i = 0; i < MAXB; i++) bullets[i].alive = 0;
    start_tick = tick;
}

static void spawn_domo(void) {
    for (int i = 0; i < MAXDOMO; i++) {
        if (domos[i].alive) continue;
        domos[i].alive = 1;
        domos[i].hp = DOMO_HP;
        domos[i].flash = 0;
        domos[i].x = W + 10;
        domos[i].y0 = 30 + rnd() % (WH - DOMO_H - 60);
        float sp = 1.4f + frnd() + score * 0.05f;
        domos[i].spd = sp > 4.5f ? 4.5f : sp;
        domos[i].ph = frnd() * 6.28f;
        domos[i].cool = 25 + rnd() % 50;
        domos[i].charge = domos[i].burst = domos[i].lunge = domos[i].lz = domos[i].lzf = 0;
        return;
    }
}

static int cat_vx, cat_vy;           /* cat movement this frame, for leading shots */

static int level(void) { return score / 5; }
static float bullet_speed(void) {
    float s = 2.6f + level() * 0.25f;
    return s > 4.2f ? 4.2f : s;
}
/* aim at where the cat will be (lead the target) */
static float aim(float mx, float my, float spd, float lead) {
    float tx = cx + CAT_W / 2, ty = cy + CAT_H / 2;
    float d = sqrtf((tx - mx) * (tx - mx) + (ty - my) * (ty - my));
    float t = d / spd;
    tx += cat_vx * t * lead;
    ty += cat_vy * t * lead;
    return atan2f(ty - my, tx - mx);
}

static char save_path[512];
static void save_hiscore(void) {
    if (!save_path[0]) return;
    FILE *f = fopen(save_path, "w");
    if (f) { fprintf(f, "%d\n", hiscore); fclose(f); }
}
static void update_hiscore(void) {
    if (score > hiscore) { hiscore = score; save_hiscore(); }
}

static void cat_hit(unsigned int tick) {
    if (state != PLAYING || inv > 0) return;
    boom_req = 1;
    if (--lives <= 0) {
        state = DEAD; dead_tick = tick;
        explode(cx + CAT_W / 2, cy + CAT_H / 2, 80);
        update_hiscore();
    } else {
        inv = 90;
        meow_req = 1;                /* ouch */
    }
}

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* =====================================================================
 * Domo eye lasers
 * ===================================================================== */
static void domo_eyes_world(int i, int dy, float *lx, float *ly, float *rx, float *ry) {
    *lx = domos[i].x + DOMO_EYE_LX; *ly = dy + DOMO_EYE_LY;
    *rx = domos[i].x + DOMO_EYE_RX; *ry = dy + DOMO_EYE_RY;
}

/* thick beam from (x0,y0) along angle, until it leaves the screen.
 * Drawn as one 1-px span per step along the major axis instead of stamping squares. */
static void draw_beam(float x0, float y0, float ang, int glow, unsigned int gc, unsigned int cc) {
    float c = cosf(ang), s = sinf(ang);
    int g2 = glow / 2;
    if (fabsf(c) >= fabsf(s)) {               /* mostly horizontal: vertical spans */
        float slope = s / c;
        int dir = c > 0 ? 1 : -1;
        for (int x = (int)x0; x >= -1 && x <= W; x += dir) {
            int y = (int)(y0 + (x - x0) * slope);
            if (y < -glow || y > H + glow) break;
            rect(x, y - g2, 1, glow, gc);
            rect(x, y - 1, 1, 3, cc);
        }
    } else {                                  /* mostly vertical: horizontal spans */
        float slope = c / s;
        int dir = s > 0 ? 1 : -1;
        for (int y = (int)y0; y >= -1 && y <= H; y += dir) {
            int x = (int)(x0 + (y - y0) * slope);
            if (x < -glow || x > W + glow) break;
            rect(x - g2, y, glow, 1, gc);
            rect(x - 1, y, 3, 1, cc);
        }
    }
}
static void draw_aim_line(float x0, float y0, float ang, unsigned int c) {
    float dx = cosf(ang) * 8.f, dy = sinf(ang) * 8.f;
    float x = x0, y = y0;
    for (int k = 0; k < 80; k++, x += dx, y += dy) {
        if (x < 0 || x > W || y < 0 || y > H) break;
        rect((int)x - 1, (int)y - 1, 3, 3, c);
    }
}
/* does a beam from (ox,oy) along ang hit the cat? (world coords) */
static int beam_hits_cat(float ox, float oy, float ang) {
    float vx = cx + CAT_W / 2 - ox, vy = cy + CAT_H / 2 - oy;
    float c = cosf(ang), s = sinf(ang);
    float t = vx * c + vy * s;
    if (t <= 0) return 0;
    return fabsf(vx * s - vy * c) < LZ_HIT_R;
}

/* =====================================================================
 * Game frame: update + draw
 * ===================================================================== */
static int camy;

/* =====================================================================
 * Touch controls
 *   left half  : floating joystick (appears where you touch)
 *   right half : hold for laser eyes
 *   MEOW button (right), II pause button (top centre)
 * ===================================================================== */
#define MAXTOUCH 8
enum { R_NONE, R_JOY, R_FIRE, R_MEOW, R_UI };
static struct { int id, role; float x, y, bx, by; } touches[MAXTOUCH];
static int ntouch;
#define MAXTAP 8
static struct { float x, y; } taps[MAXTAP];   /* touch-downs since last frame (menus) */
static int ntap;

#define JOY_R 34.f                  /* joystick base radius */
#define JOY_FULL 18.f               /* drag distance (per axis) for full speed */
#define JOY_DEAD 3.f
#define CAT_SPEED 4.f               /* same as the PSP D-pad: 4 px per frame */
#define MEOW_X (W - 34)
#define MEOW_Y 64
#define MEOW_R 20
#define FIRE_X (W - 66)
#define FIRE_Y (H - 62)
#define FIRE_R 42
#define PAUSE_X (W / 2 - 18)
#define PAUSE_W 36
#define PAUSE_H 26

static int in_rect(float x, float y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
static int in_circle(float x, float y, int ccx, int ccy, int r) {
    return (x - ccx) * (x - ccx) + (y - ccy) * (y - ccy) <= r * r;
}

static int show_perf;

void game_touch_down(int id, float x, float y) {
    if (ntap < MAXTAP) { taps[ntap].x = x; taps[ntap].y = y; ntap++; }
    if (ntouch >= MAXTOUCH) return;
    int role = R_UI;
    if (state == PLAYING) {
        if (in_rect(x, y, PAUSE_X - 6, 0, PAUSE_W + 12, PAUSE_H + 6)) role = R_UI;  /* handled as tap */
        else if (in_rect(x, y, 0, 0, 110, 30)) { show_perf ^= 1; role = R_NONE; }
        else if (in_circle(x, y, MEOW_X, MEOW_Y, MEOW_R + 8)) { meow_req = 1; role = R_MEOW; }
        else if (x < W / 2) role = R_JOY;
        else role = R_FIRE;
    }
    touches[ntouch].id = id; touches[ntouch].role = role;
    touches[ntouch].x = touches[ntouch].bx = x;
    touches[ntouch].y = touches[ntouch].by = y;
    ntouch++;
}
void game_touch_move(int id, float x, float y) {
    for (int i = 0; i < ntouch; i++)
        if (touches[i].id == id) { touches[i].x = x; touches[i].y = y; }
}
void game_touch_up(int id) {
    for (int i = 0; i < ntouch; i++)
        if (touches[i].id == id) { touches[i] = touches[--ntouch]; i--; }
}
void game_touch_cancel_all(void) { ntouch = 0; ntap = 0; }

static int touch_role(int role) {
    for (int i = 0; i < ntouch; i++) if (touches[i].role == role) return i;
    return -1;
}
static float axis(float d) {
    float a = fabsf(d);
    if (a < JOY_DEAD) return 0.f;
    a = (a - JOY_DEAD) / (JOY_FULL - JOY_DEAD);
    if (a > 1.f) a = 1.f;
    return d < 0 ? -a : a;
}

static void game_update(unsigned int tick) {
    int prev_cx = cx, prev_cy = cy;
    if (state == PLAYING) {
        laser_on = touch_role(R_FIRE) >= 0;
        int j = touch_role(R_JOY);
        if (j >= 0) {
            float dx = touches[j].x - touches[j].bx, dy = touches[j].y - touches[j].by;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > JOY_R) {      /* drag the base along so reversing is instant */
                touches[j].bx = touches[j].x - dx / len * JOY_R;
                touches[j].by = touches[j].y - dy / len * JOY_R;
            }
            cxf += axis(dx) * CAT_SPEED;
            cyf += axis(dy) * CAT_SPEED;
        }
        if (cyf < YMIN) cyf = YMIN;
        if (cyf > YMAX) cyf = YMAX;
        if (cxf < XMIN) cxf = XMIN;
        if (cxf > XMAX) cxf = XMAX;
        cx = (int)cxf; cy = (int)cyf;
        cat_vx = cx - prev_cx; cat_vy = cy - prev_cy;
        if (inv > 0) inv--;
    } else {
        laser_on = 0;
    }
    yhist[tick & (HIST - 1)] = cy;

    /* camera: smoothly follow the cat vertically */
    float target = cy + CAT_H / 2 - H / 2;
    if (target < 0) target = 0;
    if (target > WH - H) target = WH - H;
    camf += (target - camf) * 0.15f;
    camy = (int)camf;

    for (int i = 0; i < NSTARS; i++) {
        stars[i].x -= stars[i].spd;
        if (stars[i].x < -16) { stars[i].x = W + 16; stars[i].y = rnd() % WH; }
    }

    /* ---------- update domos ---------- */
    int any_beam = 0;
    if (state == PLAYING && --spawn_timer <= 0) {
        spawn_domo();
        int t = 65 - score * 2;
        spawn_timer = t < 20 ? 20 : t;
    }
    for (int i = 0; i < MAXDOMO; i++) {
        if (!domos[i].alive) continue;
        domos[i].x -= domos[i].spd;
        if (domos[i].flash) domos[i].flash--;
        if (domos[i].x < -DOMO_W) { domos[i].alive = 0; continue; }
        int dy = (int)domo_y(i, tick);
        /* domo rams the cat */
        if (state == PLAYING && inv == 0 &&
            overlap(cx + S, cy, CAT_W - 3 * S, CAT_H,
                    (int)domos[i].x + 4, dy + 3, DOMO_W - 8, DOMO_H - 5)) {
            domos[i].alive = 0;
            explode(domos[i].x + DOMO_W / 2, dy + DOMO_H / 2, 30);
            cat_hit(tick);
            continue;
        }
        /* ---- attacks ---- */
        if (state != PLAYING) { domos[i].lz = domos[i].lzf = 0; continue; }
        float mx = domos[i].x + DOMO_MOUTH_X, my = dy + DOMO_MOUTH_Y;   /* mouth */
        if (domos[i].lunge > 0) {                 /* dash at the cat */
            domos[i].lunge--;
            domos[i].x += domos[i].lvx;
            domos[i].y0 += domos[i].lvy;
            if (domos[i].y0 < 0) domos[i].y0 = 0;
            if (domos[i].y0 > WH - DOMO_H) domos[i].y0 = WH - DOMO_H;
        }
        if (domos[i].burst > 0 && --domos[i].burst_t <= 0) {   /* rapid aimed shots */
            float sp = bullet_speed();
            fire_bullet(mx, my, aim(mx, my, sp, 0.9f), sp, 0);
            spit_req = 1;
            domos[i].burst--;
            domos[i].burst_t = 9;
        }
        /* eye lasers: aim (tracking, then locked), then fire */
        if (domos[i].lz > 0) {
            float lx, ly, rx, ry;
            domo_eyes_world(i, dy, &lx, &ly, &rx, &ry);
            if (domos[i].lz > LZ_LOCK) domos[i].lz_ang = aim((lx + rx) / 2, (ly + ry) / 2, 1.f, 0.f);
            if (--domos[i].lz == 0) domos[i].lzf = LZ_FIRE;
        } else if (domos[i].lzf > 0) {
            float lx, ly, rx, ry;
            domo_eyes_world(i, dy, &lx, &ly, &rx, &ry);
            any_beam = 1;
            if (inv == 0 && (beam_hits_cat(lx, ly, domos[i].lz_ang) || beam_hits_cat(rx, ry, domos[i].lz_ang)))
                cat_hit(tick);
            if (--domos[i].lzf == 0) {
                int c = 150 - level() * 15;
                domos[i].cool = (c < 50 ? 50 : c) + rnd() % 60;
            }
        }
        if (domos[i].charge > 0) {
            if (--domos[i].charge == 0) {
                float sp = bullet_speed();
                int lv = level();
                switch (domos[i].kind) {
                case A_BURST:
                    domos[i].burst = 3 + (lv > 4 ? 2 : lv / 2);
                    domos[i].burst_t = 0;
                    break;
                case A_FAN: {
                    int n = lv >= 2 ? 7 : 5;
                    float a = aim(mx, my, sp, 0.5f);
                    for (int k = 0; k < n; k++)
                        fire_bullet(mx, my, a + (k - n / 2) * 0.2f, sp, 0);
                    spit_req = 1;
                    break;
                }
                case A_LUNGE: {
                    float a = aim(mx, my, 7.f, 0.5f);
                    float ls = 6.f + lv * 0.4f;
                    if (ls > 8.5f) ls = 8.5f;
                    domos[i].lvx = cosf(a) * ls;
                    domos[i].lvy = sinf(a) * ls;
                    domos[i].lunge = 50;
                    boom_req = 1;
                    break;
                }
                case A_HOMING:
                    fire_bullet(mx, my, (float)M_PI - 0.8f, 2.4f, 150);
                    fire_bullet(mx, my, (float)M_PI + 0.8f, 2.4f, 150);
                    if (lv >= 3) fire_bullet(mx, my, (float)M_PI, 2.4f, 150);
                    spit_req = 1;
                    break;
                case A_RING: {
                    float off = frnd() * 0.5f;
                    for (int k = 0; k < 12; k++)
                        fire_bullet(domos[i].x + DOMO_W / 2, dy + DOMO_H / 2,
                                    off + k * 2.f * (float)M_PI / 12, sp * 0.8f, 0);
                    spit_req = 1;
                    break;
                }
                }
                int c = 150 - level() * 15;
                domos[i].cool = (c < 50 ? 50 : c) + rnd() % 60;
            }
        } else if (!domos[i].burst && !domos[i].lunge && !domos[i].lz && !domos[i].lzf &&
                   --domos[i].cool <= 0) {
            int sx = (int)domos[i].x, sy = dy - camy;
            if (sx > cx + CAT_W + 30 && sx < W - DOMO_W / 2 &&
                sy > -DOMO_H / 2 && sy < H - DOMO_H / 2) {
                int opts[A_COUNT], n = 0;
                for (int k = 0; k < A_COUNT; k++) {
                    if (score < attack_unlock[k]) continue;
                    if (k == A_LUNGE && sx - cx < 150) continue;   /* needs a run-up */
                    opts[n++] = k;
                }
                domos[i].kind = opts[rnd() % n];
                if (domos[i].kind == A_LASER) domos[i].lz = LZ_AIM;  /* has its own warning */
                else domos[i].charge = CHARGE_T;
            } else {
                domos[i].cool = 10;
            }
        }
    }
    elaser_on = any_beam;

    /* ---------- update bullets ---------- */
    for (int i = 0; i < MAXB; i++) {
        if (!bullets[i].alive) continue;
        if (bullets[i].homing > 0 && state == PLAYING) {   /* steer toward cat */
            bullets[i].homing--;
            float cur_a = atan2f(bullets[i].vy, bullets[i].vx);
            float want = atan2f(cy + CAT_H / 2 - bullets[i].y, cx + CAT_W / 2 - bullets[i].x);
            float d = want - cur_a;
            while (d > (float)M_PI) d -= 2.f * (float)M_PI;
            while (d < -(float)M_PI) d += 2.f * (float)M_PI;
            if (d > 0.045f) d = 0.045f;
            if (d < -0.045f) d = -0.045f;
            float sp = sqrtf(bullets[i].vx * bullets[i].vx + bullets[i].vy * bullets[i].vy);
            bullets[i].vx = cosf(cur_a + d) * sp;
            bullets[i].vy = sinf(cur_a + d) * sp;
        }
        bullets[i].x += bullets[i].vx;
        bullets[i].y += bullets[i].vy;
        int sy = (int)bullets[i].y - camy;
        if (bullets[i].x < -10 || bullets[i].x > W + 10 || sy < -60 || sy > H + 60) {
            bullets[i].alive = 0;
            continue;
        }
        if (state == PLAYING && inv == 0 &&
            overlap(cx + 2 * S, cy + 2 * S, CAT_W - 5 * S, CAT_H - 4 * S,
                    (int)bullets[i].x - 3, (int)bullets[i].y - 3, 6, 6)) {
            bullets[i].alive = 0;
            explode(bullets[i].x, bullets[i].y, 10);
            cat_hit(tick);
        }
    }

    /* ---------- update particles ---------- */
    for (int i = 0; i < MAXP; i++) {
        if (parts[i].life <= 0) continue;
        parts[i].x += parts[i].vx - 1.f;
        parts[i].y += parts[i].vy;
        parts[i].vy += 0.06f;
        parts[i].life--;
    }
}

#define DEAD_BX (W / 2 - 110)
#define DEAD_BW 220
#define DEAD_BH 28
#define DEAD_RETRY_Y 160
#define DEAD_TITLE_Y 200

/* translucent touch controls */
static void draw_controls(unsigned int tick) {
    const unsigned int WH_ = WHITE;
    /* pause button */
    blend(PAUSE_X, 2, PAUSE_W, PAUSE_H, BLACK, 90);
    rect(PAUSE_X + 11, 7, 5, 16, WH_);
    rect(PAUSE_X + 20, 7, 5, 16, WH_);
    /* joystick */
    int j = touch_role(R_JOY);
    if (j >= 0) {
        int bx = (int)touches[j].bx, by = (int)touches[j].by;
        float dx = touches[j].x - bx, dy = touches[j].y - by;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > JOY_R) { dx = dx / len * JOY_R; dy = dy / len * JOY_R; }
        circle(bx, by, (int)JOY_R, 3, WH_, 110);
        circle(bx + (int)dx, by + (int)dy, 14, 0, WH_, 120);
    } else {
        circle(60, H - 60, (int)JOY_R, 3, WH_, 50);
        circle(60, H - 60, 14, 0, WH_, 40);
    }
    /* laser zone hint + meow button */
    int firing = touch_role(R_FIRE) >= 0;
    circle(FIRE_X, FIRE_Y, FIRE_R, 3, firing ? RGB(0xFF, 0x40, 0x40) : WH_, firing ? 140 : 60);
    if (!firing) text(FIRE_X - 20, FIRE_Y - 4, "LASER", RGB(0xFF, 0xA0, 0xA0));
    int meowing = touch_role(R_MEOW) >= 0;
    circle(MEOW_X, MEOW_Y, MEOW_R, 0, meowing ? RGB(0xFF, 0x99, 0xFF) : BLACK, meowing ? 140 : 80);
    circle(MEOW_X, MEOW_Y, MEOW_R, 2, WH_, 110);
    text(MEOW_X - 16, MEOW_Y - 4, "MEOW", WH_);
    (void)tick;
}

/* draws the game world + HUD; cat-laser hits are resolved here (needs eye positions) */
static void game_draw(unsigned int tick, unsigned int ui) {
    int f = tick / 5;                /* ~12 animation frames per second */
    int live = state == PLAYING || state == PAUSED;   /* cat on screen */
    rect(0, 0, W, H, RGB(0x0F, 0x4D, 0x8F));
    for (int i = 0; i < NSTARS; i++) {
        int sy = stars[i].y - camy;
        if (sy > -16 && sy < H + 16) draw_star(stars[i].x, sy, tick + stars[i].phase);
    }

    if (live) draw_rainbow(cx, camy, tick, f);

    for (int i = 0; i < MAXDOMO; i++) {
        if (!domos[i].alive) continue;
        int sx = (int)domos[i].x, sy = (int)domo_y(i, tick) - camy;
        if (domos[i].lunge)
            for (int k = 0; k < 4; k++)
                rect(sx + DOMO_W + 4 + (int)(frnd() * 10), sy + 8 + k * 12, 14 + (int)(frnd() * 10), 2, WHITE);
        draw_domo(sx, sy, domos[i].flash ? 1 : (domos[i].charge && live ? 2 : 0), tick);
        if (domos[i].charge && live && (tick & 4))
            text(sx + DOMO_W / 2 - (domos[i].kind == A_LUNGE ? 8 : 4), sy - 12,
                 domos[i].kind == A_LUNGE ? "!!" : "!", RGB(0xFF, 0xFF, 0x40));
        /* arrow markers for domos above/below the visible area */
        if ((tick & 8) && sx > -DOMO_W && sx < W) {
            unsigned int R = RGB(0xFF, 0x40, 0x40);
            int mx = sx + DOMO_W / 2;
            if (sy + DOMO_H < 0) { rect(mx - 6, 2, 12, 3, R); rect(mx - 3, 0, 6, 2, R); }
            if (sy > H)          { rect(mx - 6, H - 5, 12, 3, R); rect(mx - 3, H - 2, 6, 2, R); }
        }
    }

    for (int i = 0; i < MAXB; i++)
        if (bullets[i].alive) draw_bullet((int)bullets[i].x, (int)bullets[i].y - camy, bullets[i].homing > 0, tick);

    for (int i = 0; i < MAXP; i++) {
        if (parts[i].life <= 0) continue;
        int sz = parts[i].life > 12 ? 4 : 2;
        rect((int)parts[i].x, (int)parts[i].y - camy, sz, sz, parts[i].c);
    }

    /* Domo eye lasers: warning line while aiming, then the beam */
    for (int i = 0; i < MAXDOMO; i++) {
        if (!domos[i].alive || (!domos[i].lz && !domos[i].lzf)) continue;
        float lx, ly, rx, ry;
        domo_eyes_world(i, (int)domo_y(i, tick), &lx, &ly, &rx, &ry);
        ly -= camy; ry -= camy;
        unsigned int G = RGB(0x40, 0xFF, 0x60);
        if (domos[i].lz) {
            int locked = domos[i].lz <= LZ_LOCK;
            if (locked || (tick & 4)) {
                unsigned int c = locked ? RGB(0xFF, 0xFF, 0xFF) : RGB(0x30, 0xC0, 0x50);
                draw_aim_line(lx, ly, domos[i].lz_ang, c);
                draw_aim_line(rx, ry, domos[i].lz_ang, c);
            }
            rect((int)lx - 2, (int)ly - 2, 5, 5, G); rect((int)rx - 2, (int)ry - 2, 5, 5, G);
        } else {
            int gw = 9 + (tick & 2) * 2;
            draw_beam(lx, ly, domos[i].lz_ang, gw, G, WHITE);
            draw_beam(rx, ry, domos[i].lz_ang, gw, G, WHITE);
            rect((int)lx - 5, (int)ly - 5, 11, 11, G); rect((int)rx - 5, (int)ry - 5, 11, 11, G);
            rect((int)lx - 2, (int)ly - 2, 5, 5, WHITE); rect((int)rx - 2, (int)ry - 2, 5, 5, WHITE);
        }
    }

    if (live) {
        int oy = cy - camy;
        if (!(inv > 0 && (tick & 4))) draw_cat(cx, oy, f);  /* blink when invulnerable */

        if (laser_on && state == PLAYING) {
            int ex[2], ey;
            cat_eyes(cx, oy, f, ex, &ey);
            draw_laser(ex[0], ey, -LASER_SLOPE, tick);
            draw_laser(ex[1], ey,  LASER_SLOPE, tick);

            /* lasers pop spit balls */
            for (int i = 0; i < MAXB; i++) {
                if (!bullets[i].alive) continue;
                float bx = bullets[i].x, by = bullets[i].y - camy;
                for (int e = 0; e < 2 && bullets[i].alive; e++) {
                    if (bx < ex[e]) continue;
                    float ly = ey + (bx - ex[e]) * (e ? LASER_SLOPE : -LASER_SLOPE);
                    if (fabsf(ly - by) < 8) {
                        bullets[i].alive = 0;
                        explode(bx, by + camy, 8);
                    }
                }
            }

            /* laser hits (screen space) */
            for (int i = 0; i < MAXDOMO; i++) {
                if (!domos[i].alive) continue;
                float dcx = domos[i].x + DOMO_W / 2;
                float dcy = domo_y(i, tick) - camy + DOMO_H / 2;
                for (int e = 0; e < 2; e++) {
                    if (dcx + DOMO_W / 2 < ex[e]) continue;
                    float px = dcx > ex[e] ? dcx : ex[e];
                    float ly = ey + (px - ex[e]) * (e ? LASER_SLOPE : -LASER_SLOPE);
                    if (fabsf(ly - dcy) < DOMO_H / 2 + 3) {
                        domos[i].hp--;
                        domos[i].flash = 2;
                    }
                }
                if (domos[i].hp <= 0) {
                    domos[i].alive = 0;
                    explode(dcx, dcy + camy, 30);
                    boom_req = 1;
                    score++;
                }
            }
        }
    }

    /* ---------- HUD ---------- */
    char s[64];
    snprintf(s, sizeof s, "SCORE %d", score);
    text(6, 6, s, WHITE);
    snprintf(s, sizeof s, "HI %d", hiscore > score ? hiscore : score);
    text(6, 18, s, RGB(0xFF, 0xFF, 0x80));
    for (int l = 0; l < lives; l++)
        for (int j = 0; j < 6; j++)
            for (int i = 0; i < 7; i++)
                if (heart[j][i] == 'r')
                    rect(W - 26 - l * 20 + i * 2, 6 + j * 2, 2, 2, RGB(0xFF, 0x30, 0x50));

    if (state == PLAYING) draw_controls(tick);

    if (state == PLAYING && tick - start_tick < 300) {
        text((W - 30 * 8) / 2, H / 2 - 30, "LEFT SIDE: DRAG TO MOVE", WHITE);
        text((W - 30 * 8) / 2, H / 2 - 18, "RIGHT SIDE: HOLD FOR LASERS", WHITE);
    }

    if (state == DEAD) {
        big_text(50, 5, "GAME OVER", RGB(0xFF, 0x50, 0x50));
        snprintf(s, sizeof s, "SCORE %d", score);
        big_text(110, 3, s, WHITE);
        if (tick - dead_tick > 60) {
            dim(DEAD_BX, DEAD_RETRY_Y, DEAD_BW, DEAD_BH);
            dim(DEAD_BX, DEAD_TITLE_Y, DEAD_BW, DEAD_BH);
            big_text(DEAD_RETRY_Y + 6, 2, "RETRY", (ui / 30) & 1 ? RGB(0xFF, 0xFF, 0x80) : WHITE);
            big_text(DEAD_TITLE_Y + 6, 2, "TITLE SCREEN", RGB(0xC0, 0xC0, 0xC0));
        }
    }
}

/* =====================================================================
 * Title, pause menu, credits
 * ===================================================================== */
static unsigned int title_cols[256];

static void draw_title(unsigned int ui, int sel) {
    for (int y = 0; y < TITLE_H; y++) {
        unsigned int *row = cur + y * STRIDE;
        const unsigned char *p = title_pix + y * TITLE_W;
        for (int x = 0; x < TITLE_W; x++) row[x] = title_cols[p[x]];
    }
    static const char *items[] = {"START GAME", "CREDITS"};
    dim(8, 210, 190, 56);
    for (int i = 0; i < 2; i++) {
        int on = i == sel;
        big_text_at(36, 216 + i * 24, 2, items[i], on ? RGB(0xFF, 0xFF, 0x60) : RGB(0xC0, 0xC0, 0xC0));
        if (on && (ui & 16)) big_text_at(16, 216 + i * 24, 2, ">", RGB(0xFF, 0xFF, 0x60));
    }
    if (hiscore > 0) {
        char s[32];
        snprintf(s, sizeof s, "HI-SCORE %d", hiscore);
        int n = strlen(s);
        dim(W - n * 8 - 14, H - 20, n * 8 + 10, 14);
        text(W - n * 8 - 9, H - 17, s, RGB(0xFF, 0xFF, 0x80));
    }
}

static void draw_pause(unsigned int ui, int sel) {
    dim(0, 0, W, H);
    big_text(46, 5, "PAUSED", WHITE);
    static const char *items[] = {"RESUME", "CREDITS", "QUIT TO TITLE"};
    for (int i = 0; i < 3; i++) {
        int on = i == sel;
        char s[32];
        snprintf(s, sizeof s, on ? "> %s <" : "%s", items[i]);
        big_text(118 + i * 30, 2, s, on ? RGB(0xFF, 0xFF, 0x60) : RGB(0xC0, 0xC0, 0xC0));
    }
    text((W - 28 * 8) / 2, H - 18, "TAP TO SELECT   BACK: RESUME", RGB(0xA0, 0xA0, 0xA0));
}

/* '#' = title, '*' = heading, '' = blank, else = name. Edit freely! */
static const char *credits[] = {
    "#DOMO-KUN",
    "#VS",
    "#NYAN CAT",
    "#ANDROID EDITION",
    "",
    "*IDEA & DIRECTION",
    "Mcl0v1n",
    "",
    "*PROGRAMMING & PIXEL ART",
    "CLAUDE (AI CODING ASSISTANT)",
    "",
    "*NYAN CAT CHARACTER",
    "CHRIS TORRES, 2011",
    "",
    "*DOMO-KUN CHARACTER",
    "NHK / TSUNEO GODA, 1998",
    "",
    "*TITLE ART",
    "NYAN CAT SPACE ART (IMAGES.PNG)",
    "DOMO-KUN PNG BY NICKFAMZI",
    "",
    "*IN-GAME DOMO SPRITE",
    "CONVERTED FROM A DOMO-KUN PHOTO",
    "",
    "*TOOLS",
    "PSPDEV TOOLCHAIN / PSPSDK",
    "PPSSPP EMULATOR",
    "ANDROID NDK",
    "",
    "*ANDROID PORT",
    "CLAUDE (AI CODING ASSISTANT)",
    "",
    "*SPECIAL THANKS",
    "EVERY DOMO WHO GOT LASERED",
    "",
    "",
    "*THANKS FOR PLAYING!",
};
#define NCREDITS (int)(sizeof credits / sizeof credits[0])
#define CREDIT_LINE 24

static void draw_credits(unsigned int ui, unsigned int ct) {
    rect(0, 0, W, H, RGB(0x08, 0x20, 0x48));
    for (int i = 0; i < NSTARS; i++) {
        int x = ((stars[i].x - (int)ct * stars[i].spd) % (W + 32) + W + 32) % (W + 32) - 16;
        draw_star(x, stars[i].y % H, ui + stars[i].phase);
    }
    /* a little chase across the bottom: Nyan Cat lasering a Domo */
    int f = ui / 5;
    int span = W + 400;
    int catx = (int)(ct * 2 % span) - 200;
    int caty = H - 70;
    for (int b = 0; b < 6; b++)
        for (int x = catx + 8 * S, k = 0; x > -24; x -= 24, k++)
            rect(x - 24, caty + ((k + f) & 1) * S + b * 3 * S, 24, 3 * S, RAINBOW[b]);
    draw_cat(catx, caty, f);
    draw_domo(catx + 180, caty - 6 + (int)(6 * sinf(ui * 0.1f)), (ui & 8) ? 1 : 0, ui);
    int ex[2], ey;
    cat_eyes(catx, caty, f, ex, &ey);
    if ((ui & 16) && catx > -20 && catx < W) draw_laser(ex[0], ey, 0.f, ui);

    /* scrolling text (title lines are taller) */
    int total = 0;
    for (int i = 0; i < NCREDITS; i++) total += credits[i][0] == '#' ? 34 : CREDIT_LINE;
    total += H - 90;
    int y = H - 90 - (int)(ct * 0.6f) % total;            /* loops forever */
    for (int i = 0; i < NCREDITS; i++) {
        const char *s = credits[i];
        int lh = s[0] == '#' ? 34 : CREDIT_LINE;
        int th = s[0] == '#' ? 24 : 16;
        if (y > 14 && y + th < H - 86) {                    /* only fully between header and chase */
            if (s[0] == '#')      big_text(y, 3, s + 1, RAINBOW[(i + ui / 8) % 6]);
            else if (s[0] == '*') big_text(y, 2, s + 1, RGB(0xFF, 0xE0, 0x40));
            else if (s[0])        big_text(y, 2, s, WHITE);
        }
        y += lh;
    }
    dim(0, 0, W, 14);
    text((W - 18 * 8) / 2, 3, "TAP / BACK: RETURN", RGB(0xC0, 0xC0, 0xC0));
}

/* =====================================================================
 * Frame loop (called by the platform layer at 60 Hz)
 * ===================================================================== */
static unsigned int tick;           /* game time: frozen while paused / in menus */
static unsigned int ui;             /* real frame counter (blinking etc.) */
static int sel, credits_from = TITLE;
static unsigned int credits_t;

static unsigned int now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned int)(ts.tv_sec * 1000000ull + ts.tv_nsec / 1000);
}

void game_init(const char *save_dir) {
    audio_tables_init();
    synth_meow();
    domo_init_palettes();
    for (int i = 0; i < 256; i++)
        title_cols[i] = RGB(title_pal[i][0], title_pal[i][1], title_pal[i][2]);
    rng = now_us() | 1;
    for (int i = 0; i < NSTARS; i++) {
        stars[i].x = rnd() % W; stars[i].y = rnd() % WH;
        stars[i].spd = 2 + rnd() % 4; stars[i].phase = rnd() % 24;
    }
    if (save_dir) {
        snprintf(save_path, sizeof save_path, "%s/hiscore.txt", save_dir);
        FILE *f = fopen(save_path, "r");
        if (f) { if (fscanf(f, "%d", &hiscore) != 1) hiscore = 0; fclose(f); }
    }
    reset(0);
    state = TITLE;
}

static void pause_game(void) {
    state = PAUSED; sel = 0;
    laser_on = elaser_on = 0;
}
static void open_credits(int from) {
    state = CREDITS; credits_from = from; credits_t = 0;
}
static void to_title(void) {
    update_hiscore();
    state = TITLE; sel = 0;
}

int game_back(void) {
    switch (state) {
    case TITLE:   return 0;
    case CREDITS: state = credits_from; sel = 1; break;
    case PAUSED:  state = PLAYING; break;
    case PLAYING: pause_game(); break;
    case DEAD:    if (tick - dead_tick > 60) to_title(); break;
    }
    return 1;
}
void game_menu_key(void) { if (state == PLAYING) pause_game(); }
void game_suspend(void) {
    if (state == PLAYING) pause_game();
    laser_on = elaser_on = 0;
    game_touch_cancel_all();
    update_hiscore();
}

void game_frame(void) {
    static unsigned int t_prev, work_us, frame_us = 16667;
    unsigned int t_start = now_us();
    if (t_prev) frame_us = (frame_us * 7 + (t_start - t_prev)) / 8;   /* smoothed */
    t_prev = t_start;
    if (rng == 0) rng = 1;
    rnd();                           /* stir the RNG with player timing */

    switch (state) {
    case TITLE:
        laser_on = elaser_on = 0;
        for (int t = 0; t < ntap; t++)
            for (int i = 0; i < 2; i++)
                if (in_rect(taps[t].x, taps[t].y, 0, 210 + i * 24, 210, 24)) {
                    sel = i;
                    if (i == 0) { reset(tick); meow_req = 1; }
                    else open_credits(TITLE);
                    t = ntap;
                    break;
                }
        if (state == TITLE) draw_title(ui, sel);
        else if (state == CREDITS) draw_credits(ui, credits_t);
        else game_draw(tick, ui);
        break;

    case CREDITS:
        laser_on = elaser_on = 0;
        credits_t++;
        if (ntap) { state = credits_from; sel = 1; }   /* cursor stays on CREDITS */
        draw_credits(ui, credits_t);
        break;

    case PAUSED:
        laser_on = elaser_on = 0;
        for (int t = 0; t < ntap && state == PAUSED; t++)
            for (int i = 0; i < 3; i++)
                if (in_rect(taps[t].x, taps[t].y, 60, 111 + i * 30, W - 120, 30)) {
                    sel = i;
                    if (i == 0) state = PLAYING;
                    else if (i == 1) open_credits(PAUSED);
                    else to_title();
                    break;
                }
        if (state == CREDITS) { draw_credits(ui, credits_t); break; }
        if (state == TITLE) { draw_title(ui, sel); break; }
        game_draw(tick, ui);
        if (state == PAUSED) draw_pause(ui, sel);
        break;

    case PLAYING:
        for (int t = 0; t < ntap; t++)
            if (in_rect(taps[t].x, taps[t].y, PAUSE_X - 6, 0, PAUSE_W + 12, PAUSE_H + 6)) {
                pause_game();
                game_draw(tick, ui);
                draw_pause(ui, sel);
                break;
            }
        if (state == PAUSED) break;
        /* fall through */
    case DEAD:
        if (state == DEAD && tick - dead_tick > 60) {
            for (int t = 0; t < ntap; t++) {
                if (in_rect(taps[t].x, taps[t].y, DEAD_BX, DEAD_RETRY_Y - 6, DEAD_BW, DEAD_BH + 8)) {
                    reset(tick); break;
                }
                if (in_rect(taps[t].x, taps[t].y, DEAD_BX, DEAD_TITLE_Y - 2, DEAD_BW, DEAD_BH + 8)) {
                    to_title(); break;
                }
            }
            if (state == TITLE) { draw_title(ui, sel); break; }
        }
        game_update(tick);
        game_draw(tick, ui);
        tick++;
        break;
    }
    ntap = 0;

    if (show_perf) {
        /* CPU = time spent building this frame */
        char p[48];
        unsigned int w = work_us;
        snprintf(p, sizeof p, "CPU %u.%uMS  %uFPS", w / 1000, (w / 100) % 10,
                 frame_us ? (1000000 + frame_us / 2) / frame_us : 0);
        dim(W / 2 - 76, 32, 152, 12);
        text(W / 2 - 72, 34, p, w < 16000 ? RGB(0x80, 0xFF, 0x80) : RGB(0xFF, 0x60, 0x60));
    }
    work_us = (work_us * 7 + (now_us() - t_start)) / 8;
    ui++;
}
