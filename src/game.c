/* Nyan Cat vs Domo-kun -- gameplay.
 * The core (cat, Domo attacks, eye lasers, rainbow, camera) is the PSP game.
 * New: King Domo boss fights, Domo variants, power-ups, fish currency,
 * rainbow bombs, combo multiplier, upgrades and cat skins. */
#include "nyan.h"
#include "domo_sprite.h"

#define WH 544                    /* world height: two screens tall, camera follows */
#define S 3                       /* cat art-pixel size in screen pixels */
#ifndef START_KILLS
#define START_KILLS 0             /* debug: -DSTART_KILLS=N to test later difficulty */
#endif

int state = TITLE;
unsigned tick, ui;
int score, kills, run_coins, new_best, lives, bombs;
unsigned dead_tick;
int vib_ms;

static unsigned start_tick;
static int inv, spawn_timer, cx, cy, camy, shield, triple_t, triple_max;
static int combo, combo_t, shake_t, flash_t, next_boss, banner_t;
static char banner[40];
static unsigned banner_c;
static float camf, cxf, cyf;
static int cat_vx, cat_vy;        /* cat movement this frame, for leading shots */
static int bomb_fx;
static float bomb_x, bomb_y;

#define MAX_LIVES 5
#define MAX_BOMBS 9

/* =====================================================================
 * RNG
 * ===================================================================== */
unsigned rng = 0x1234567;
unsigned rnd(void) { rng = rng * 1103515245u + 12345u; return rng >> 16; }
float frnd(void) { return (rnd() % 10000) / 10000.f; }

/* =====================================================================
 * Shop data: upgrades and skins
 * ===================================================================== */
const Upgrade upgrades[UPG_COUNT] = {
    {"LASER POWER",   "+30% LASER DAMAGE PER LEVEL",   3, {200, 500, 1200}},
    {"EXTRA LIFE",    "START WITH ONE MORE LIFE",      2, {400, 1000}},
    {"BOMB BAG",      "START WITH ONE MORE BOMB",      2, {250, 700}},
    {"BUBBLE SHIELD", "START EVERY RUN WITH A SHIELD", 1, {600}},
    {"POWER BOOST",   "LONGER POWER-UPS, MORE DROPS",  3, {150, 400, 900}},
    {"FISH MAGNET",   "PULL IN FISH FROM FARTHER",     2, {100, 350}},
};

#define RB_CLASSIC {RGB(0xFF,0x00,0x00), RGB(0xFF,0x99,0x00), RGB(0xFF,0xFF,0x00), \
                    RGB(0x33,0xFF,0x00), RGB(0x00,0x99,0xFF), RGB(0x66,0x33,0xFF)}
const Skin skins[NSKINS] = {
    {"CLASSIC", 0, 0, RGB(0x99,0x99,0x99), RGB(0xFF,0x99,0x99), RGB(0xFF,0xCC,0x99),
     RGB(0xFF,0x99,0xFF), RGB(0xFF,0x33,0x99), RB_CLASSIC},
    {"TAC NAYN", 400, 0, RGB(0x44,0x44,0x44), RGB(0x88,0x44,0x44), RGB(0xC8,0x91,0x3C),
     RGB(0xE0,0xB0,0x5A), RGB(0x8A,0x5A,0x20),
     {RGB(0x30,0x30,0x30), RGB(0x48,0x48,0x48), RGB(0x60,0x60,0x60),
      RGB(0x78,0x78,0x78), RGB(0x90,0x90,0x90), RGB(0xA8,0xA8,0xA8)}},
    {"COTTON CANDY", 600, 0, RGB(0xF4,0xF4,0xFF), RGB(0xFF,0xAA,0xCC), RGB(0xFF,0xE4,0xF0),
     RGB(0xA0,0xE0,0xFF), RGB(0xFF,0x77,0xAA),
     {RGB(0xFF,0xB3,0xBA), RGB(0xFF,0xDF,0xBA), RGB(0xFF,0xFF,0xBA),
      RGB(0xBA,0xFF,0xC9), RGB(0xBA,0xE1,0xFF), RGB(0xD7,0xBA,0xFF)}},
    {"ZOMBIE", 900, 0, RGB(0x7F,0xA3,0x6B), RGB(0xA0,0x50,0x50), RGB(0xB0,0xA0,0x80),
     RGB(0x9A,0xCD,0x32), RGB(0x55,0x6B,0x2F),
     {RGB(0x39,0xFF,0x14), RGB(0x2E,0xCC,0x40), RGB(0x1B,0x8A,0x2A),
      RGB(0x8E,0x44,0xAD), RGB(0x6A,0x0D,0xAD), RGB(0x4B,0x00,0x82)}},
    {"GOLDEN", 1500, 1, RGB(0xE8,0xC0,0x40), RGB(0xFF,0xB0,0x70), RGB(0xFF,0xF0,0xA0),
     RGB(0xFF,0xD7,0x00), RGB(0xFF,0xFF,0xFF),
     {RGB(0xFF,0xF4,0xB0), RGB(0xFF,0xE0,0x70), RGB(0xFF,0xD0,0x30),
      RGB(0xF0,0xB0,0x20), RGB(0xD0,0x90,0x10), RGB(0xA0,0x70,0x08)}},
    {"COSMIC", 2500, 3, RGB(0x50,0x50,0xB0), RGB(0xFF,0x66,0xCC), RGB(0x30,0x30,0x70),
     RGB(0x70,0x30,0xC0), RGB(0xFF,0xFF,0xFF),
     {RGB(0xFF,0x00,0xFF), RGB(0xAA,0x00,0xFF), RGB(0x55,0x00,0xFF),
      RGB(0x00,0x55,0xFF), RGB(0x00,0xAA,0xFF), RGB(0x00,0xFF,0xFF)}},
};

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
#define PX(ox, oy, ux, uy, c) rect((ox) + (ux) * S, (oy) + (uy) * S, S, S, (c))

static void head_pos(int f, int *hx, int *hy) {
    int bob = (f & 2) ? -1 : 0;
    *hx = 13 + ((f & 2) ? 1 : 0);   /* head overhangs the pop-tart's right edge */
    *hy = 5 + ((f & 1) ? 0 : -1) + bob;
}

void cat_eyes(int ox, int oy, int f, int ex[2], int *ey) {
    int hx, hy;
    head_pos(f, &hx, &hy);
    ex[0] = ox + (hx + 5) * S;
    ex[1] = ox + (hx + 11) * S;
    *ey = oy + (hy + 7) * S;
}

void draw_cat(int ox, int oy, int f, int skin, int glow, int mouth) {
    const Skin *sk = &skins[skin];
    const unsigned K = BLACK, G = sk->body;
    int leg = (f & 1) ? 1 : 0;
    int bob = (f & 2) ? -1 : 0;

    static const signed char tail_dy[2][6] = {{0, 0, -1, -1, -2, -2}, {0, 0, 1, 1, 1, 0}};
    const signed char *td = tail_dy[f & 1];
    for (int i = 0; i < 6; i++) {
        int y = 10 + td[i] + bob;
        PX(ox, oy, -1 - i, y - 1, K); PX(ox, oy, -1 - i, y + 1, K);
    }
    PX(ox, oy, -7, 10 + td[5] + bob, K);
    for (int i = 0; i < 6; i++) PX(ox, oy, -1 - i, 10 + td[i] + bob, G);

    static const int lx[4] = {1, 4, 15, 19};
    for (int i = 0; i < 4; i++) {
        int x = lx[i] + leg - 1;
        rect(ox + (x - 1) * S, oy + (16 + bob) * S, 4 * S, 4 * S, K);
        rect(ox + x * S, oy + (16 + bob) * S, 2 * S, 3 * S, G);
    }

    int by = oy + bob * S;
    rect(ox + S, by, 19 * S, 18 * S, K);
    rect(ox, by + S, 21 * S, 16 * S, K);
    rect(ox + 2 * S, by + S, 17 * S, 16 * S, sk->crust);
    rect(ox + S, by + 2 * S, 19 * S, 14 * S, sk->crust);
    rect(ox + 4 * S, by + 3 * S, 13 * S, 12 * S, sk->fill);
    rect(ox + 3 * S, by + 4 * S, 15 * S, 10 * S, sk->fill);
    static const signed char spr[][2] = {
        {5, 4}, {9, 3}, {12, 5}, {15, 4}, {4, 8}, {7, 6}, {10, 9},
        {14, 7}, {6, 11}, {9, 13}, {5, 13}, {13, 12}, {16, 10},
    };
    for (unsigned i = 0; i < sizeof spr / sizeof spr[0]; i++)
        PX(ox, by, spr[i][0], spr[i][1], sk->sprinkle);

    int hx, hy;
    head_pos(f, &hx, &hy);
    for (int j = 0; j < 13; j++)
        for (int i = 0; i < 15; i++) {
            char ch = head[j][i];
            unsigned c = ch == 'k' ? K : ch == 'g' ? G : ch == 'w' ? WHITE : sk->cheek;
            if (ch != '.') PX(ox, oy, hx + i, hy + j, c);
        }

    if (glow) {
        unsigned R = RGB(0xFF, 0x20, 0x20);
        PX(ox, oy, hx + 4, hy + 6, R); PX(ox, oy, hx + 5, hy + 6, R);
        PX(ox, oy, hx + 4, hy + 7, R); PX(ox, oy, hx + 5, hy + 7, R);
        PX(ox, oy, hx + 10, hy + 6, R); PX(ox, oy, hx + 11, hy + 6, R);
        PX(ox, oy, hx + 10, hy + 7, R); PX(ox, oy, hx + 11, hy + 7, R);
    }
    if (mouth) {
        rect(ox + (hx + 5) * S, oy + (hy + 9) * S, 7 * S, 3 * S, K);
        rect(ox + (hx + 7) * S, oy + (hy + 10) * S, 3 * S, S, RGB(0xFF, 0x55, 0x77));
    }
}

/* rainbow follows the cat's past y positions, so it curves when moving */
#define HIST 256
static int yhist[HIST];

static void draw_rainbow(int ox, int cam, unsigned t, int f) {
    const unsigned *rb = skins[sv.skin].rb;
    int band = 3 * S, seg = 8 * S;
    for (int x = ox + 8 * S, k = 0; x > 0; x -= seg, k++) {
        int dy = ((k + f) & 1) ? S : 0;
        int y = yhist[(t - k * 3) & (HIST - 1)] - cam;
        for (int b = 0; b < 6; b++)
            rect(x - seg, y + dy + b * band, seg, band, rb[b]);
    }
}

void draw_laser(int x0, int y0, float slope, unsigned t, unsigned glowc, unsigned mid) {
    int glow = 8 + (t & 2) * 2;
    for (int x = x0; x < W; x += 2) {
        int y = y0 + (int)((x - x0) * slope);
        rect(x, y - glow / 2, 2, glow, glowc);
        rect(x, y - 2, 2, 4, mid);
        rect(x, y - 1, 2, 2, WHITE);
    }
    rect(x0 - 5, y0 - 5, 10, 10, mid);
    rect(x0 - 2, y0 - 2, 4, 4, WHITE);
}
#define LASER_SLOPE 0.10f
#define LASER_GLOW RGB(0xFF, 0x10, 0x30)
#define LASER_MID RGB(0xFF, 0x90, 0x90)

/* =====================================================================
 * Domo-kun (sprite generated from a photo by the PSP project's tools)
 * ===================================================================== */
#define CHARGE_T 28
enum { V_NORMAL, V_GOLD, V_RED, V_COUNT };
static const int var_hp[V_COUNT] = {16, 40, 24};
static const int var_fish[V_COUNT] = {2, 8, 4};
static const int var_points[V_COUNT] = {10, 30, 20};

/* [variant][mode][palette index]; modes: normal, hit flash, charging x2 */
static unsigned domo_cols[V_COUNT][4][DOMO_NPAL];

static unsigned clamp8(float v) { return v < 0 ? 0 : v > 255 ? 255 : (unsigned)v; }

static void domo_init_palettes(void) {
    for (int v = 0; v < V_COUNT; v++)
        for (int i = 0; i < DOMO_NPAL; i++) {
            float r = domo_pal_rgb[i][0], g = domo_pal_rgb[i][1], b = domo_pal_rgb[i][2];
            float lum = (r + g + b) / 3;
            int keep = lum > 215 || lum < 30 || domo_pal_mouth[i];
            if (!keep && v == V_GOLD) { r = lum * 1.5f + 50; g = lum * 1.2f + 30; b = lum * 0.35f; }
            if (!keep && v == V_RED)  { r = r * 1.3f + 50; g = g * 0.45f; b = b * 0.55f + 10; }
            unsigned R = clamp8(r), G = clamp8(g), B = clamp8(b);
            domo_cols[v][0][i] = RGB(R, G, B);
            domo_cols[v][1][i] = RGB((R + 3 * 255) / 4, (G + 3 * 230) / 4, (B + 3 * 200) / 4);
            domo_cols[v][2][i] = domo_pal_mouth[i] ? RGB(0xFF, 0xA0, 0x20) : RGB(R, G, B);
            domo_cols[v][3][i] = domo_pal_mouth[i] ? RGB(0xFF, 0xFF, 0x80) : RGB(R, G, B);
        }
}

/* mode: 0 normal, 1 hit flash, 2 charging (mouth glows); sc = 1 or 2 */
void draw_domo(int x, int y, int mode, unsigned t, int var, int sc) {
    int w = DOMO_W * sc, h = DOMO_H * sc;
    if (x >= W || x + w <= 0 || y >= H || y + h <= 0) return;
    if (mode == 2) x += (t & 2) ? sc : -sc;
    const unsigned *pal = domo_cols[var][mode == 2 ? 2 + ((t >> 2) & 1) : mode];
    for (int j = 0; j < DOMO_H; j++)
        for (int k = 0; k < sc; k++) {
            int sy = y + j * sc + k;
            if (sy < 0 || sy >= H) continue;
            unsigned *row = cur + sy * STRIDE;
            const unsigned char *p = domo_pix[j];
            for (int i = 0; i < DOMO_W; i++) {
                if (!p[i]) continue;
                for (int m = 0; m < sc; m++) {
                    int sx = x + i * sc + m;
                    if (sx >= 0 && sx < W) row[sx] = pal[p[i]];
                }
            }
        }
}

enum { A_BURST, A_FAN, A_LUNGE, A_HOMING, A_RING, A_LASER, A_COUNT };
static const int attack_unlock[A_COUNT] = {0, 0, 0, 4, 10, 0};
#define LZ_AIM 45
#define LZ_LOCK 16
#define LZ_FIRE 32
#define LZ_HIT_R 17.f

#define MAXDOMO 20
static struct {
    int alive, flash, cool, charge, kind, burst, burst_t, lunge, lz, lzf, var;
    float hp, x, y0, spd, ph, lvx, lvy, lz_ang;
} domos[MAXDOMO];
static float domo_y(int i) { return domos[i].y0 + 18.f * sinf(tick * 0.05f + domos[i].ph); }

#define MAXB 220
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

static void draw_bullet(int x, int y, int homing, unsigned t) {
    unsigned O = homing ? RGB(0x30, 0x00, 0x40) : RGB(0x50, 0x00, 0x10);
    unsigned R = homing ? RGB(0xE0, 0x40, 0xFF) : RGB(0xFF, 0x50, 0x20);
    unsigned C = (t & 4) ? RGB(0xFF, 0xFF, 0x80) : WHITE;
    rect(x - 3, y - 5, 6, 10, O); rect(x - 5, y - 3, 10, 6, O); rect(x - 4, y - 4, 8, 8, O);
    rect(x - 2, y - 4, 4, 8, R);  rect(x - 4, y - 2, 8, 4, R);  rect(x - 3, y - 3, 6, 6, R);
    rect(x - 1, y - 2, 2, 4, C);  rect(x - 2, y - 1, 4, 2, C);
}

/* =====================================================================
 * Particles, popups, stars, items
 * ===================================================================== */
#define MAXP 400
static struct { float x, y, vx, vy; int life; unsigned c; } parts[MAXP];

static void explode_c(float x, float y, int n, const unsigned *cols, int ncols) {
    for (int i = 0; i < MAXP && n > 0; i++) {
        if (parts[i].life > 0) continue;
        float a = frnd() * 2.f * (float)M_PI, sp = 1.f + frnd() * 4.f;
        parts[i].x = x; parts[i].y = y;
        parts[i].vx = cosf(a) * sp; parts[i].vy = sinf(a) * sp;
        parts[i].life = 20 + rnd() % 25;
        parts[i].c = cols[rnd() % ncols];
        n--;
    }
}
static void explode(float x, float y, int n) {
    static const unsigned cols[] = {
        RGB(0xFF, 0xFF, 0x80), RGB(0xFF, 0xA0, 0x20), RGB(0xFF, 0x40, 0x20),
        RGB(0x8B, 0x4A, 0x22), WHITE,
    };
    explode_c(x, y, n, cols, 5);
}

#define MAXPOP 16
static struct { float x, y; int life; unsigned c; char s[24]; } pops[MAXPOP];
static void popup(float x, float y, unsigned c, const char *s) {
    int best = 0;
    for (int i = 0; i < MAXPOP; i++) {
        if (pops[i].life <= 0) { best = i; break; }
        if (pops[i].life < pops[best].life) best = i;
    }
    pops[best].x = x; pops[best].y = y; pops[best].life = 50; pops[best].c = c;
    snprintf(pops[best].s, sizeof pops[best].s, "%s", s);
}
static void show_banner(const char *s, unsigned c, int t) {
    snprintf(banner, sizeof banner, "%s", s);
    banner_c = c; banner_t = t;
}

#define NSTARS 40
static struct { int x, y, spd, phase; } stars[NSTARS];

void draw_star(int x, int y, int ph) {
    int r = 1 + (ph / 8) % 3;
    rect(x - 1, y - 1, 3, 3, WHITE);
    for (int i = 2; i <= r + 1; i++) {
        rect(x - 1 - i * 2, y - 1, 2, 3, WHITE); rect(x + 1 + (i - 1) * 2, y - 1, 2, 3, WHITE);
        rect(x - 1, y - 1 - i * 2, 3, 2, WHITE); rect(x - 1, y + 1 + (i - 1) * 2, 3, 2, WHITE);
    }
}

void draw_space(unsigned t) {
    rect(0, 0, W, H, RGB(0x08, 0x20, 0x48));
    for (int i = 0; i < NSTARS; i++) {
        int x = ((stars[i].x - (int)t * stars[i].spd) % (W + 32) + W + 32) % (W + 32) - 16;
        draw_star(x, stars[i].y % H, t + stars[i].phase);
    }
}

static const char *heart_art[] = {
    ".rr.rr.", "rrrrrrr", "rrrrrrr", ".rrrrr.", "..rrr..", "...r...",
};
static void draw_heart(int x, int y, int px) {
    static const unsigned c[] = {RGB(0xFF, 0x30, 0x50)};
    draw_art(x, y, px, heart_art, 6, "r", c);
}

static const char *fish_art[] = {
    "..oooo..o",
    ".oyyyyooy",
    "okyyyyyyo",
    ".oyyyyooy",
    "..oooo..o",
};
void draw_fish(int x, int y) {
    static const unsigned c[] = {RGB(0xB0, 0x70, 0x10), RGB(0xFF, 0xD0, 0x30), BLACK};
    draw_art(x, y, 2, fish_art, 5, "oyk", c);
}

enum { IT_FISH, IT_TRIPLE, IT_SHIELD, IT_BOMB, IT_HEART };
#define MAXI 96
static struct { int alive, type; float x, y, vx, vy; } items[MAXI];

static void spawn_item(int type, float x, float y) {
    for (int i = 0; i < MAXI; i++) {
        if (items[i].alive) continue;
        items[i].alive = 1; items[i].type = type;
        items[i].x = x; items[i].y = y;
        if (type == IT_FISH) {
            float a = frnd() * 2.f * (float)M_PI, sp = 1.f + frnd() * 3.f;
            items[i].vx = cosf(a) * sp; items[i].vy = sinf(a) * sp;
        } else {
            items[i].vx = items[i].vy = 0;
        }
        return;
    }
}

static void drop_loot(float x, float y, int fish, int pchance) {
    for (int k = 0; k < fish; k++) spawn_item(IT_FISH, x, y);
    if ((int)(rnd() % 100) < pchance) {
        int r = rnd() % 100, t;
        if (r < 35) t = IT_TRIPLE;
        else if (r < 60) t = IT_SHIELD;
        else if (r < 85) t = IT_BOMB;
        else t = IT_HEART;
        spawn_item(t, x, y);
    }
}

static void draw_item(int i, int x, int y) {
    if (items[i].type == IT_FISH) { draw_fish(x - 9, y - 5); return; }
    static const unsigned col[] = {0, RGB(0xFF, 0xC0, 0x00), RGB(0x30, 0xD0, 0xFF),
                                   RGB(0xFF, 0x40, 0xC0), RGB(0xFF, 0x60, 0x80)};
    int pulse = (ui / 4 + i) & 4 ? 1 : 0;
    circle(x, y, 13 + pulse, 0, BLACK, 140);
    circle(x, y, 11, 0, col[items[i].type], 230);
    circle(x, y, 12 + pulse, 2, WHITE, 220);
    switch (items[i].type) {
    case IT_TRIPLE: big_text_at(x - 7, y - 7, 2, "3", WHITE); break;
    case IT_SHIELD: big_text_at(x - 7, y - 7, 2, "S", WHITE); break;
    case IT_BOMB:   big_text_at(x - 7, y - 7, 2, "B", WHITE); break;
    case IT_HEART:  draw_heart(x - 7, y - 6, 2); break;
    }
}

/* =====================================================================
 * Game state
 * ===================================================================== */
#define CAT_W (30 * S)
#define CAT_H (18 * S)
#define XMIN (6 * S)
#define XMAX (W - 31 * S)
#define YMIN (2 * S)
#define YMAX (WH - 21 * S)

static int level(void) { return kills / 5; }
static int mult(void) { int m = 1 + combo / 5; return m > 8 ? 8 : m; }
static float power_scale(void) { return 1.f + 0.3f * sv.upg[UPG_POWER]; }

/* King Domo */
#define BSC 2
#define BW (DOMO_W * BSC)
#define BH (DOMO_H * BSC)
enum { B_NONE, B_WARN, B_ENTER, B_FIGHT, B_DIE };
enum { BA_SPIRAL, BA_FANS, BA_LASER, BA_SUMMON, BA_HOMING, BA_DASH, BA_COUNT };
static struct {
    int phase, t, num, atk, last_atk, atk_t, rest, flash, dash;
    float x, y, hp, maxhp, ang, lz_ang, lz_dir;
} boss;
static float boss_yb(void) { return boss.y + 8.f * sinf(tick * 0.05f); }
static int boss_visible(void) { return boss.phase >= B_ENTER; }
#define BOSS_FIRST 30
#define BOSS_EVERY 35

void play_reset(void) {
    state = PLAYING; score = 0; kills = START_KILLS; run_coins = 0; new_best = 0;
    lives = 3 + sv.upg[UPG_LIVES];
    bombs = 1 + sv.upg[UPG_BOMBS];
    shield = sv.upg[UPG_SHIELD] > 0;
    triple_t = 0; combo = 0; combo_t = 1000; inv = 90; spawn_timer = 90;
    shake_t = flash_t = bomb_fx = banner_t = 0;
    cx = 80; cy = (WH - CAT_H) / 2;
    cxf = cx; cyf = cy;
    camf = cy + CAT_H / 2 - H / 2;
    for (int i = 0; i < HIST; i++) yhist[i] = cy;
    memset(domos, 0, sizeof domos);
    memset(parts, 0, sizeof parts);
    memset(bullets, 0, sizeof bullets);
    memset(items, 0, sizeof items);
    memset(pops, 0, sizeof pops);
    memset(&boss, 0, sizeof boss);
    boss.last_atk = -1;
    next_boss = BOSS_FIRST > kills ? BOSS_FIRST : kills + 3;
    start_tick = tick;
    sv.games++;
    music_track = 1;
    laser_on = elaser_on = 0;
}

static void spawn_domo_at(float x, float y0, int var) {
    for (int i = 0; i < MAXDOMO; i++) {
        if (domos[i].alive) continue;
        memset(&domos[i], 0, sizeof domos[i]);
        domos[i].alive = 1;
        domos[i].var = var;
        domos[i].hp = var_hp[var];
        domos[i].x = x;
        domos[i].y0 = y0;
        float sp = 1.4f + frnd() + kills * 0.05f;
        if (sp > 4.5f) sp = 4.5f;
        if (var == V_GOLD) sp *= 0.7f;
        domos[i].spd = sp;
        domos[i].ph = frnd() * 6.28f;
        domos[i].cool = 25 + rnd() % 50;
        return;
    }
}

static void spawn_domo(void) {
    int var = V_NORMAL, r = rnd() % 100;
    int gold = kills >= 10 ? 5 + kills / 3 : 0, red = kills >= 18 ? kills / 2 : 0;
    if (gold > 18) gold = 18;
    if (red > 25) red = 25;
    if (r < gold) var = V_GOLD;
    else if (r < gold + red) var = V_RED;
    spawn_domo_at(W + 10, 30 + rnd() % (WH - DOMO_H - 60), var);
}

static float bullet_speed(void) {
    float s = 2.6f + level() * 0.25f;
    return s > 4.2f ? 4.2f : s;
}
static float aim(float mx, float my, float spd, float lead) {
    float tx = cx + CAT_W / 2, ty = cy + CAT_H / 2;
    float d = sqrtf((tx - mx) * (tx - mx) + (ty - my) * (ty - my));
    float t = d / spd;
    tx += cat_vx * t * lead;
    ty += cat_vy * t * lead;
    return atan2f(ty - my, tx - mx);
}

static void end_run(void) {
    if (score > sv.hiscore) { sv.hiscore = score; new_best = 1; }
    if (kills > sv.best_kills) sv.best_kills = kills;
    save_write();
}

static void cat_hit(void) {
    if (state != PLAYING || inv > 0) return;
    if (shield) {
        static const unsigned cy_[] = {RGB(0x30, 0xD0, 0xFF), WHITE, RGB(0x80, 0xE8, 0xFF)};
        shield = 0; inv = 60;
        boom_req = 1; vib_ms = 40;
        explode_c(cx + CAT_W / 2, cy + CAT_H / 2, 40, cy_, 3);
        popup(cx + CAT_W / 2, cy - 4, RGB(0x30, 0xD0, 0xFF), "SHIELD BROKE!");
        return;
    }
    boom_req = 1; vib_ms = 120; shake_t = 14; combo = 0;
    if (--lives <= 0) {
        state = DEAD; dead_tick = tick;
        explode(cx + CAT_W / 2, cy + CAT_H / 2, 80);
        laser_on = elaser_on = 0;
        music_track = 1;
        end_run();
    } else {
        inv = 90;
        meow_req = 1;
    }
}

static int overlap(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh) {
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

static void add_kill_score(float x, float y, int base) {
    combo = combo_t < 150 ? combo + 1 : 1;
    combo_t = 0;
    int pts = base * mult();
    score += pts;
    char s[16];
    snprintf(s, sizeof s, "+%d", pts);
    popup(x, y, mult() > 1 ? YELLOW : WHITE, s);
    if (combo > 1 && combo % 5 == 0) {
        snprintf(s, sizeof s, "COMBO x%d!", mult());
        show_banner(s, YELLOW, 60);
    }
}

static void domo_killed(int i) {
    float x = domos[i].x + DOMO_W / 2, y = domo_y(i) + DOMO_H / 2;
    int v = domos[i].var;
    domos[i].alive = 0;
    explode(x, y, v == V_GOLD ? 50 : 30);
    boom_req = 1;
    kills++; sv.total_kills++;
    add_kill_score(x, y - 30, var_points[v]);
    int fish = var_fish[v] + (rnd() % 100 < 30);
    drop_loot(x, y, fish, (v == V_GOLD ? 25 : 10) + 3 * sv.upg[UPG_POWER]);
}

static void collect(int i) {
    float x = items[i].x, y = items[i].y;
    items[i].alive = 0;
    switch (items[i].type) {
    case IT_FISH:
        run_coins++; sv.coins++; coin_req = 1;
        return;
    case IT_TRIPLE:
        triple_max = triple_t = (int)(600 * power_scale());
        popup(x, y - 16, RGB(0xFF, 0xC0, 0x00), "TRIPLE LASER!");
        break;
    case IT_SHIELD:
        shield = 1;
        popup(x, y - 16, RGB(0x30, 0xD0, 0xFF), "SHIELD!");
        break;
    case IT_BOMB:
        if (bombs < MAX_BOMBS) bombs++;
        popup(x, y - 16, RGB(0xFF, 0x40, 0xC0), "+1 BOMB");
        break;
    case IT_HEART:
        if (lives < MAX_LIVES) { lives++; popup(x, y - 16, RGB(0xFF, 0x60, 0x80), "+1 LIFE"); }
        else { score += 500; popup(x, y - 16, YELLOW, "+500"); }
        break;
    }
    power_req = 1;
    vib_ms = 30;
}

/* =====================================================================
 * Beams
 * ===================================================================== */
static void domo_eyes_world(int i, int dy, float *lx, float *ly, float *rx, float *ry) {
    *lx = domos[i].x + DOMO_EYE_LX; *ly = dy + DOMO_EYE_LY;
    *rx = domos[i].x + DOMO_EYE_RX; *ry = dy + DOMO_EYE_RY;
}
static void boss_eyes_world(float *lx, float *ly, float *rx, float *ry) {
    float yb = boss_yb();
    *lx = boss.x + DOMO_EYE_LX * BSC; *ly = yb + DOMO_EYE_LY * BSC;
    *rx = boss.x + DOMO_EYE_RX * BSC; *ry = yb + DOMO_EYE_RY * BSC;
}

static void draw_beam(float x0, float y0, float ang, int glow, unsigned gc, unsigned cc) {
    float c = cosf(ang), s = sinf(ang);
    int g2 = glow / 2;
    if (fabsf(c) >= fabsf(s)) {
        float slope = s / c;
        int dir = c > 0 ? 1 : -1;
        for (int x = (int)x0; x >= -1 && x <= W; x += dir) {
            int y = (int)(y0 + (x - x0) * slope);
            if (y < -glow || y > H + glow) break;
            rect(x, y - g2, 1, glow, gc);
            rect(x, y - 1, 1, 3, cc);
        }
    } else {
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
static void draw_aim_line(float x0, float y0, float ang, unsigned c) {
    float dx = cosf(ang) * 8.f, dy = sinf(ang) * 8.f;
    float x = x0, y = y0;
    for (int k = 0; k < 80; k++, x += dx, y += dy) {
        if (x < 0 || x > W || y < 0 || y > H) break;
        rect((int)x - 1, (int)y - 1, 3, 3, c);
    }
}
static int beam_hits_cat(float ox, float oy, float ang) {
    float vx = cx + CAT_W / 2 - ox, vy = cy + CAT_H / 2 - oy;
    float c = cosf(ang), s = sinf(ang);
    float t = vx * c + vy * s;
    if (t <= 0) return 0;
    return fabsf(vx * s - vy * c) < LZ_HIT_R;
}
/* does the cat's beam (from x0,y0 with slope) cross a box centered at (bx,by)? */
static int beam_hits_box(float x0, float y0, float slope, float bx, float by, float hw, float hh) {
    if (bx + hw < x0) return 0;
    float px = bx > x0 ? bx : x0;
    float ly = y0 + (px - x0) * slope;
    return fabsf(ly - by) < hh + 3;
}

/* =====================================================================
 * King Domo
 * ===================================================================== */
static void boss_start_attack(void) {
    int opts[BA_COUNT], n = 0;
    for (int k = 0; k < BA_COUNT; k++) {
        if (k == boss.last_atk) continue;
        if (k == BA_HOMING && boss.num == 0) continue;
        opts[n++] = k;
    }
    boss.atk = opts[rnd() % n];
    boss.last_atk = boss.atk;
    static const int dur[BA_COUNT] = {150, 110, 140, 50, 70, 260};
    boss.atk_t = dur[boss.atk];
    if (boss.atk == BA_LASER) boss.lz_dir = (rnd() & 1) ? 1.f : -1.f;
}

static void boss_defeated(void) {
    static const unsigned gold[] = {RGB(0xFF, 0xD7, 0x00), WHITE, RGB(0xFF, 0xA0, 0x20), RGB(0xFF, 0xFF, 0x80)};
    float yb = boss_yb(), mx = boss.x + BW / 2, my = yb + BH / 2;
    explode_c(mx, my, 160, gold, 4);
    boom_req = 1; bomb_req = 1;
    vib_ms = 400; shake_t = 30; flash_t = 12;
    drop_loot(mx, my, 40 + 10 * boss.num, 0);
    spawn_item(IT_HEART, mx - 20, my - 20);
    spawn_item(rnd() & 1 ? IT_TRIPLE : IT_BOMB, mx + 20, my + 20);
    int pts = 1000 * (boss.num + 1);
    score += pts;
    char s[24];
    snprintf(s, sizeof s, "+%d", pts);
    popup(mx, my - 40, YELLOW, s);
    show_banner("KING DOMO DEFEATED!", YELLOW, 150);
    sv.bosses++;
    boss.num++;
    boss.phase = B_NONE;
    next_boss = kills + BOSS_EVERY;
    music_track = 1;
    elaser_on = 0;
}

static void boss_hurt(float dmg) {
    if (boss.phase != B_FIGHT) return;
    boss.hp -= dmg;
    boss.flash = 2;
    if (boss.hp <= 0) {
        boss.hp = 0;
        boss.phase = B_DIE; boss.t = 0; boss.dash = 0;
        for (int i = 0; i < MAXB; i++)
            if (bullets[i].alive) { bullets[i].alive = 0; explode(bullets[i].x, bullets[i].y, 4); }
    }
}

static void boss_fight(void) {
    int p2 = boss.hp < boss.maxhp * 0.5f;
    float yb = boss_yb();
    float home = W - BW - 30 + 14 * sinf(tick * 0.02f);

    /* movement */
    if (boss.dash == 0) {
        float ty = cy + CAT_H / 2 - BH / 2;
        if (ty < 0) ty = 0;
        if (ty > WH - BH) ty = WH - BH;
        float follow = boss.atk == BA_DASH ? 0.06f : (p2 ? 0.025f : 0.015f);
        boss.y += (ty - boss.y) * follow;
        boss.x += (home - boss.x) * 0.05f;
    } else if (boss.dash == 1) {
        boss.x -= 9.f + boss.num;
        if (boss.x < 4) boss.dash = 2;
    } else {
        boss.x += 4.f;
        if (boss.x >= home) boss.dash = 0;
    }

    if (inv == 0 && overlap(cx + S, cy, CAT_W - 3 * S, CAT_H,
                            (int)boss.x + 10, (int)yb + 8, BW - 20, BH - 12))
        cat_hit();

    if (boss.atk < 0) {
        if (--boss.rest <= 0) boss_start_attack();
        return;
    }
    int t = --boss.atk_t;
    float mx = boss.x + DOMO_MOUTH_X * BSC, my = yb + DOMO_MOUTH_Y * BSC;
    float sp = bullet_speed();
    switch (boss.atk) {
    case BA_SPIRAL:
        if (t < 120 && t % (p2 ? 4 : 5) == 0) {
            int n = p2 ? 3 : 2;
            for (int k = 0; k < n; k++)
                fire_bullet(mx, my, boss.ang + k * 2.f * (float)M_PI / n, 2.3f, 0);
            boss.ang += 0.26f;
            if (t % 10 == 0) spit_req = 1;
        }
        break;
    case BA_FANS:
        if (t == 80 || t == 55 || t == 30 || (p2 && t == 5)) {
            int n = 7 + 2 * p2;
            float a = aim(mx, my, sp, 0.5f);
            for (int k = 0; k < n; k++) fire_bullet(mx, my, a + (k - n / 2) * 0.17f, sp, 0);
            spit_req = 1;
        }
        break;
    case BA_LASER: {
        float lx, ly, rx, ry;
        boss_eyes_world(&lx, &ly, &rx, &ry);
        if (t > 90) boss.lz_ang = aim((lx + rx) / 2, (ly + ry) / 2, 1.f, 0.f) - boss.lz_dir * 0.35f;
        if (t <= 70) {
            boss.lz_ang += boss.lz_dir * (p2 ? 0.0105f : 0.0075f);
            if (inv == 0 && (beam_hits_cat(lx, ly, boss.lz_ang) || beam_hits_cat(rx, ry, boss.lz_ang)))
                cat_hit();
        }
        break;
    }
    case BA_SUMMON:
        if (t == 10) {
            int n = 2 + p2;
            for (int k = 0; k < n; k++) {
                float y0 = yb + BH / 2 - DOMO_H / 2 + (k - (n - 1) / 2.f) * 70.f;
                if (y0 < 0) y0 = 0;
                if (y0 > WH - DOMO_H) y0 = WH - DOMO_H;
                spawn_domo_at(boss.x - 10, y0, V_NORMAL);
            }
            spit_req = 1;
        }
        break;
    case BA_HOMING:
        if (t == 35) {
            static const float off[4] = {-1.3f, -0.6f, 0.6f, 1.3f};
            for (int k = 0; k < 4; k++) fire_bullet(mx, my, (float)M_PI + off[k], 2.5f, 130);
            if (p2) fire_bullet(mx, my, (float)M_PI, 2.5f, 130);
            spit_req = 1;
        }
        break;
    case BA_DASH:
        if (t == 260 - 45) { boss.dash = 1; boom_req = 1; vib_ms = 60; }
        else if (t < 260 - 46 && boss.dash == 0) boss.atk_t = 0;
        break;
    }
    if (boss.atk_t <= 0) {
        boss.atk = -1;
        int r = 75 - 8 * boss.num;
        if (r < 30) r = 30;
        boss.rest = p2 ? r * 3 / 5 : r;
    }
}

static void boss_update(void) {
    if (boss.phase == B_NONE) {
        if (state == PLAYING && kills >= next_boss) {
            boss.phase = B_WARN; boss.t = 0;
            alarm_req = 1; music_track = 2; vib_ms = 200;
        }
        return;
    }
    boss.t++;
    if (boss.flash) boss.flash--;
    switch (boss.phase) {
    case B_WARN:
        if (boss.t >= 150) {
            boss.phase = B_ENTER;
            boss.x = W + 10;
            boss.y = cy + CAT_H / 2 - BH / 2;
            if (boss.y < 0) boss.y = 0;
            if (boss.y > WH - BH) boss.y = WH - BH;
            boss.maxhp = boss.hp = 450 + 250 * boss.num;
            boss.atk = -1; boss.rest = 60; boss.dash = 0;
        }
        break;
    case B_ENTER:
        boss.x -= 2.f;
        if (boss.x <= W - BW - 30) boss.phase = B_FIGHT;
        break;
    case B_FIGHT:
        if (state == PLAYING) boss_fight();
        break;
    case B_DIE:
        if (boss.t % 6 == 0) {
            float yb = boss_yb();
            explode(boss.x + frnd() * BW, yb + frnd() * BH, 20);
            boom_req = 1; shake_t = 6;
        }
        if (boss.t >= 110) boss_defeated();
        break;
    }
}

/* =====================================================================
 * Bomb
 * ===================================================================== */
void play_bomb(void) {
    if (state != PLAYING) return;
    if (bombs <= 0) { buzz_req = 1; return; }
    bombs--;
    bomb_fx = 45; flash_t = 10; shake_t = 20; vib_ms = 150;
    bomb_x = cx + CAT_W / 2; bomb_y = cy + CAT_H / 2;
    bomb_req = 1;
    if (inv < 45) inv = 45;
    for (int i = 0; i < MAXB; i++)
        if (bullets[i].alive) { bullets[i].alive = 0; explode(bullets[i].x, bullets[i].y, 5); }
    for (int i = 0; i < MAXDOMO; i++) {
        if (!domos[i].alive || domos[i].x > W) continue;
        domos[i].lz = domos[i].lzf = 0;
        domos[i].hp -= 40;
        domos[i].flash = 6;
        if (domos[i].hp <= 0) domo_killed(i);
    }
    if (boss.phase == B_FIGHT) {
        boss_hurt(boss.maxhp * 0.08f);
        if (boss.atk == BA_LASER) boss.atk_t = 1;
    }
}

/* =====================================================================
 * Update
 * ===================================================================== */
#define JOY_FULL 18.f
#define JOY_DEAD 3.f
#define CAT_SPEED 4.f

static float axis(float d) {
    float a = fabsf(d);
    if (a < JOY_DEAD) return 0.f;
    a = (a - JOY_DEAD) / (JOY_FULL - JOY_DEAD);
    if (a > 1.f) a = 1.f;
    return d < 0 ? -a : a;
}

static void update_cat_lasers(void) {
    int f = tick / 5, ex[2], ey;
    cat_eyes(cx, cy - camy, f, ex, &ey);
    float bx[3] = {ex[0], ex[1], (ex[0] + ex[1]) / 2.f};
    float sl[3] = {-LASER_SLOPE, LASER_SLOPE, 0.f};
    int nb = triple_t > 0 ? 3 : 2;
    float dmg = 1.f + 0.3f * sv.upg[UPG_LASER];

    for (int i = 0; i < MAXB; i++) {        /* lasers pop spit balls */
        if (!bullets[i].alive) continue;
        float x = bullets[i].x, y = bullets[i].y - camy;
        for (int b = 0; b < nb; b++) {
            if (x < bx[b]) continue;
            if (fabsf(ey + (x - bx[b]) * sl[b] - y) < 8) {
                bullets[i].alive = 0;
                explode(x, bullets[i].y, 8);
                score += 1;
                break;
            }
        }
    }
    for (int i = 0; i < MAXDOMO; i++) {
        if (!domos[i].alive) continue;
        float dcx = domos[i].x + DOMO_W / 2, dcy = domo_y(i) - camy + DOMO_H / 2;
        for (int b = 0; b < nb; b++)
            if (beam_hits_box(bx[b], ey, sl[b], dcx, dcy, DOMO_W / 2, DOMO_H / 2)) {
                domos[i].hp -= dmg;
                domos[i].flash = 2;
            }
        if (domos[i].hp <= 0) domo_killed(i);
    }
    if (boss.phase == B_FIGHT) {
        float bcx = boss.x + BW / 2, bcy = boss_yb() - camy + BH / 2;
        for (int b = 0; b < nb; b++)
            if (beam_hits_box(bx[b], ey, sl[b], bcx, bcy, BW / 2, BH / 2 - 8)) boss_hurt(dmg);
    }
}

void play_update(void) {
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
        if (triple_t > 0) triple_t--;
    } else {
        laser_on = 0;
    }
    if (combo_t < 100000) combo_t++;
    yhist[tick & (HIST - 1)] = cy;

    float target = cy + CAT_H / 2 - H / 2;
    if (target < 0) target = 0;
    if (target > WH - H) target = WH - H;
    camf += (target - camf) * 0.15f;
    camy = (int)camf;

    for (int i = 0; i < NSTARS; i++) {
        stars[i].x -= stars[i].spd;
        if (stars[i].x < -16) { stars[i].x = W + 16; stars[i].y = rnd() % WH; }
    }

    /* ---------- domos ---------- */
    int any_beam = 0;
    if (state == PLAYING && boss.phase == B_NONE && --spawn_timer <= 0) {
        spawn_domo();
        int t = 65 - kills * 2;
        spawn_timer = t < 20 ? 20 : t;
    }
    for (int i = 0; i < MAXDOMO; i++) {
        if (!domos[i].alive) continue;
        domos[i].x -= domos[i].spd;
        if (domos[i].flash) domos[i].flash--;
        if (domos[i].x < -DOMO_W) { domos[i].alive = 0; continue; }
        int dy = (int)domo_y(i);
        if (state == PLAYING && inv == 0 &&
            overlap(cx + S, cy, CAT_W - 3 * S, CAT_H,
                    (int)domos[i].x + 4, dy + 3, DOMO_W - 8, DOMO_H - 5)) {
            domos[i].alive = 0;
            explode(domos[i].x + DOMO_W / 2, dy + DOMO_H / 2, 30);
            cat_hit();
            continue;
        }
        if (state != PLAYING) { domos[i].lz = domos[i].lzf = 0; continue; }
        float mx = domos[i].x + DOMO_MOUTH_X, my = dy + DOMO_MOUTH_Y;
        if (domos[i].lunge > 0) {
            domos[i].lunge--;
            domos[i].x += domos[i].lvx;
            domos[i].y0 += domos[i].lvy;
            if (domos[i].y0 < 0) domos[i].y0 = 0;
            if (domos[i].y0 > WH - DOMO_H) domos[i].y0 = WH - DOMO_H;
        }
        if (domos[i].burst > 0 && --domos[i].burst_t <= 0) {
            float sp = bullet_speed();
            fire_bullet(mx, my, aim(mx, my, sp, 0.9f), sp, 0);
            spit_req = 1;
            domos[i].burst--;
            domos[i].burst_t = 9;
        }
        int cool_base = 150 - level() * 15;
        if (cool_base < 50) cool_base = 50;
        if (domos[i].var == V_RED) cool_base = cool_base * 3 / 5;
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
                cat_hit();
            if (--domos[i].lzf == 0) domos[i].cool = cool_base + rnd() % 60;
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
                    for (int k = 0; k < n; k++) fire_bullet(mx, my, a + (k - n / 2) * 0.2f, sp, 0);
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
                domos[i].cool = cool_base + rnd() % 60;
            }
        } else if (!domos[i].burst && !domos[i].lunge && !domos[i].lz && !domos[i].lzf &&
                   --domos[i].cool <= 0) {
            int sx = (int)domos[i].x, sy = dy - camy;
            if (sx > cx + CAT_W + 30 && sx < W - DOMO_W / 2 &&
                sy > -DOMO_H / 2 && sy < H - DOMO_H / 2) {
                int opts[A_COUNT], n = 0;
                for (int k = 0; k < A_COUNT; k++) {
                    if (kills < attack_unlock[k]) continue;
                    if (k == A_LUNGE && sx - cx < 150) continue;
                    opts[n++] = k;
                }
                domos[i].kind = opts[rnd() % n];
                if (domos[i].kind == A_LASER) domos[i].lz = LZ_AIM;
                else domos[i].charge = CHARGE_T;
            } else {
                domos[i].cool = 10;
            }
        }
    }

    boss_update();
    if (boss.phase == B_FIGHT && boss.atk == BA_LASER && boss.atk_t <= 70) any_beam = 1;
    elaser_on = any_beam && state == PLAYING;

    /* ---------- bullets ---------- */
    for (int i = 0; i < MAXB; i++) {
        if (!bullets[i].alive) continue;
        if (bullets[i].homing > 0 && state == PLAYING) {
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
            cat_hit();
        }
    }

    if (laser_on && state == PLAYING) update_cat_lasers();

    /* ---------- items ---------- */
    float ccx = cx + CAT_W / 2, ccy = cy + CAT_H / 2;
    float magnet = 60.f + 45.f * sv.upg[UPG_MAGNET];
    for (int i = 0; i < MAXI; i++) {
        if (!items[i].alive) continue;
        float dx = ccx - items[i].x, dy = ccy - items[i].y;
        float d = sqrtf(dx * dx + dy * dy) + 0.001f;
        if (items[i].type == IT_FISH) {
            items[i].vx *= 0.94f; items[i].vy *= 0.94f;
            items[i].x += items[i].vx - 1.4f;
            items[i].y += items[i].vy;
            if (state == PLAYING && d < magnet) {
                float pull = 3.f + 5.f * (1.f - d / magnet);
                items[i].x += dx / d * pull;
                items[i].y += dy / d * pull;
            }
        } else {
            items[i].x -= 0.8f;
            items[i].y += sinf((tick + i * 20) * 0.08f) * 0.6f;
        }
        if (state == PLAYING && d < (items[i].type == IT_FISH ? 22.f : 32.f)) collect(i);
        else if (items[i].x < -20) items[i].alive = 0;
    }

    /* ---------- effects ---------- */
    for (int i = 0; i < MAXP; i++) {
        if (parts[i].life <= 0) continue;
        parts[i].x += parts[i].vx - 1.f;
        parts[i].y += parts[i].vy;
        parts[i].vy += 0.06f;
        parts[i].life--;
    }
    for (int i = 0; i < MAXPOP; i++)
        if (pops[i].life > 0) { pops[i].life--; pops[i].y -= 0.6f; }
    if (banner_t > 0) banner_t--;
    if (shake_t > 0) shake_t--;
    if (flash_t > 0) flash_t--;
    if (bomb_fx > 0) bomb_fx--;
}

/* =====================================================================
 * Draw
 * ===================================================================== */
static const char *crown_art[] = {
    "y.....y.....y",
    "yy...yyy...yy",
    "yyy.yyyyy.yyy",
    "yyyyyyyyyyyyy",
    "dyrrydbbdyrrd",
    "ddddddddddddd",
};

static void draw_boss(void) {
    if (!boss_visible()) return;
    int dying = boss.phase == B_DIE;
    if (dying && (ui & 2)) return;
    int sx = (int)boss.x, sy = (int)boss_yb() - camy;
    int p2 = boss.hp < boss.maxhp * 0.5f;
    int tele = boss.phase == B_FIGHT &&
               ((boss.atk == BA_FANS && boss.atk_t > 80) || (boss.atk == BA_SPIRAL && boss.atk_t > 120) ||
                (boss.atk == BA_DASH && boss.atk_t > 260 - 45));
    if (dying) sx += (int)(frnd() * 6) - 3;
    if (boss.dash == 1)
        for (int k = 0; k < 6; k++)
            rect(sx + BW + 4 + (int)(frnd() * 16), sy + 10 + k * 18, 24 + (int)(frnd() * 16), 3, WHITE);
    draw_domo(sx, sy, boss.flash ? 1 : (tele ? 2 : 0), ui, p2 ? V_RED : V_NORMAL, BSC);
    static const unsigned cc[] = {RGB(0xFF, 0xD7, 0x00), RGB(0xC8, 0x9B, 0x00), RGB(0xFF, 0x20, 0x40), RGB(0x30, 0x80, 0xFF)};
    draw_art(sx + BW / 2 - 18, sy - 14, 3, crown_art, 6, "ydrb", cc);
    if (tele && (ui & 4))
        big_text_at(sx + BW / 2 - (boss.atk == BA_DASH ? 16 : 8), sy - 34, 2,
                    boss.atk == BA_DASH ? "!!" : "!", YELLOW);
    if ((ui & 8) && sx > -BW && sx < W) {
        int mx = sx + BW / 2;
        if (sy + BH < 0) { rect(mx - 10, 2, 20, 4, RED); rect(mx - 5, 0, 10, 2, RED); }
        if (sy > H)      { rect(mx - 10, H - 6, 20, 4, RED); rect(mx - 5, H - 2, 10, 2, RED); }
    }
}

static void draw_boss_lasers(void) {
    if (boss.phase != B_FIGHT || boss.atk != BA_LASER) return;
    float lx, ly, rx, ry;
    boss_eyes_world(&lx, &ly, &rx, &ry);
    ly -= camy; ry -= camy;
    unsigned G = RGB(0x40, 0xFF, 0x60);
    if (boss.atk_t > 70) {
        int locked = boss.atk_t <= 90;
        if (locked || (ui & 4)) {
            unsigned c = locked ? WHITE : RGB(0x30, 0xC0, 0x50);
            draw_aim_line(lx, ly, boss.lz_ang, c);
            draw_aim_line(rx, ry, boss.lz_ang, c);
        }
        rect((int)lx - 4, (int)ly - 4, 9, 9, G); rect((int)rx - 4, (int)ry - 4, 9, 9, G);
    } else {
        int gw = 13 + (ui & 2) * 2;
        draw_beam(lx, ly, boss.lz_ang, gw, G, WHITE);
        draw_beam(rx, ry, boss.lz_ang, gw, G, WHITE);
        rect((int)lx - 7, (int)ly - 7, 15, 15, G); rect((int)rx - 7, (int)ry - 7, 15, 15, G);
        rect((int)lx - 3, (int)ly - 3, 7, 7, WHITE); rect((int)rx - 3, (int)ry - 3, 7, 7, WHITE);
    }
}

static void draw_controls(void) {
    blend(PAUSE_X, 2, PAUSE_W, PAUSE_H, BLACK, 90);
    rect(PAUSE_X + 11, 7, 5, 16, WHITE);
    rect(PAUSE_X + 20, 7, 5, 16, WHITE);
    int j = touch_role(R_JOY);
    if (j >= 0) {
        int bx = (int)touches[j].bx, by = (int)touches[j].by;
        float dx = touches[j].x - bx, dy = touches[j].y - by;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > JOY_R) { dx = dx / len * JOY_R; dy = dy / len * JOY_R; }
        circle(bx, by, (int)JOY_R, 3, WHITE, 110);
        circle(bx + (int)dx, by + (int)dy, 14, 0, WHITE, 120);
    } else {
        int jx = sv.lefty ? W - 60 : 60;
        circle(jx, H - 60, (int)JOY_R, 3, WHITE, 50);
        circle(jx, H - 60, 14, 0, WHITE, 40);
    }
    int firing = touch_role(R_FIRE) >= 0;
    circle(FIRE_X, FIRE_Y, FIRE_R, 3, firing ? RGB(0xFF, 0x40, 0x40) : WHITE, firing ? 140 : 60);
    if (!firing) text(FIRE_X - 20, FIRE_Y - 4, "LASER", RGB(0xFF, 0xA0, 0xA0));

    int meowing = touch_role(R_MEOW) >= 0;
    circle(MEOW_X, MEOW_Y, BTN_R, 0, meowing ? RGB(0xFF, 0x99, 0xFF) : BLACK, meowing ? 140 : 80);
    circle(MEOW_X, MEOW_Y, BTN_R, 2, WHITE, 110);
    text(MEOW_X - 16, MEOW_Y - 4, "MEOW", WHITE);

    int bombing = touch_role(R_BOMB) >= 0;
    unsigned bc = RGB(0xFF, 0x40, 0xC0);
    circle(BOMB_X, BOMB_Y, BTN_R, 0, bombing ? bc : BLACK, bombing ? 160 : 80);
    circle(BOMB_X, BOMB_Y, BTN_R, 2, bombs ? bc : GREY, bombs ? 200 : 90);
    text(BOMB_X - 16, BOMB_Y - 9, "BOMB", bombs ? WHITE : GREY);
    char s[8];
    snprintf(s, sizeof s, "x%d", bombs);
    text(BOMB_X - 8, BOMB_Y + 2, s, bombs ? YELLOW : GREY);
}

static void draw_hud(void) {
    char s[48];
    snprintf(s, sizeof s, "SCORE %d", score);
    text(6, 6, s, WHITE);
    snprintf(s, sizeof s, "HI %d", sv.hiscore > score ? sv.hiscore : score);
    text(6, 18, s, YELLOW);
    if (mult() > 1 && combo_t < 150) {
        snprintf(s, sizeof s, "x%d", mult());
        big_text_at(6, 30, 2, s, (ui & 4) ? YELLOW : RGB(0xFF, 0xA0, 0x20));
        rect(6, 50, (150 - combo_t) * 40 / 150, 3, YELLOW);
    }
    if (triple_t > 0 && triple_max > 0) {
        int y = 58;
        text(6, y, "TRIPLE", RGB(0xFF, 0xC0, 0x00));
        rect(56, y + 2, 42, 5, BLACK);
        rect(57, y + 3, triple_t * 40 / triple_max, 3, RGB(0xFF, 0xC0, 0x00));
    }
    for (int l = 0; l < lives; l++) draw_heart(W - 24 - l * 18, 6, 2);
    draw_fish(W - 82, 24);
    snprintf(s, sizeof s, "%d", run_coins);
    text(W - 60, 25, s, YELLOW);

    if (boss_visible() && state != DEAD) {
        int bw = 200, bx = (W - bw) / 2, by = 32;
        dim(bx - 2, by - 2, bw + 4, 16);
        int fill = (int)(boss.hp / boss.maxhp * bw);
        rect(bx, by, fill, 12, boss.hp < boss.maxhp * 0.5f ? RGB(0xFF, 0x30, 0x30) : RGB(0xFF, 0x90, 0x20));
        snprintf(s, sizeof s, "KING DOMO LV%d", boss.num + 1);
        text_center(by + 2, s, WHITE);
    }
    if (boss.phase == B_WARN) {
        if ((boss.t / 8) & 1) {
            blend(0, 70, W, 70, RGB(0xC0, 0x00, 0x00), 90);
            big_text(78, 4, "WARNING!", RGB(0xFF, 0x40, 0x40));
        }
        text_center(118, "KING DOMO IS COMING", WHITE);
    }
    if (banner_t > 0 && boss.phase != B_WARN)
        big_text(96, 2, banner, (banner_t & 4) ? banner_c : WHITE);
}

void play_draw(void) {
    int f = tick / 5;
    int live = state == PLAYING || state == PAUSED;
    rect(0, 0, W, H, RGB(0x0F, 0x4D, 0x8F));
    for (int i = 0; i < NSTARS; i++) {
        int sy = stars[i].y - camy;
        if (sy > -16 && sy < H + 16) draw_star(stars[i].x, sy, tick + stars[i].phase);
    }
    if (live) draw_rainbow(cx, camy, tick, f);

    for (int i = 0; i < MAXDOMO; i++) {
        if (!domos[i].alive) continue;
        int sx = (int)domos[i].x, sy = (int)domo_y(i) - camy;
        if (domos[i].lunge)
            for (int k = 0; k < 4; k++)
                rect(sx + DOMO_W + 4 + (int)(frnd() * 10), sy + 8 + k * 12, 14 + (int)(frnd() * 10), 2, WHITE);
        draw_domo(sx, sy, domos[i].flash ? 1 : (domos[i].charge && live ? 2 : 0), tick, domos[i].var, 1);
        if (domos[i].charge && live && (tick & 4))
            text(sx + DOMO_W / 2 - (domos[i].kind == A_LUNGE ? 8 : 4), sy - 12,
                 domos[i].kind == A_LUNGE ? "!!" : "!", RGB(0xFF, 0xFF, 0x40));
        if ((tick & 8) && sx > -DOMO_W && sx < W) {
            int mx = sx + DOMO_W / 2;
            if (sy + DOMO_H < 0) { rect(mx - 6, 2, 12, 3, RED); rect(mx - 3, 0, 6, 2, RED); }
            if (sy > H)          { rect(mx - 6, H - 5, 12, 3, RED); rect(mx - 3, H - 2, 6, 2, RED); }
        }
    }
    draw_boss();

    for (int i = 0; i < MAXI; i++)
        if (items[i].alive) draw_item(i, (int)items[i].x, (int)items[i].y - camy);
    for (int i = 0; i < MAXB; i++)
        if (bullets[i].alive) draw_bullet((int)bullets[i].x, (int)bullets[i].y - camy, bullets[i].homing > 0, tick);
    for (int i = 0; i < MAXP; i++) {
        if (parts[i].life <= 0) continue;
        int sz = parts[i].life > 12 ? 4 : 2;
        rect((int)parts[i].x, (int)parts[i].y - camy, sz, sz, parts[i].c);
    }

    for (int i = 0; i < MAXDOMO; i++) {
        if (!domos[i].alive || (!domos[i].lz && !domos[i].lzf)) continue;
        float lx, ly, rx, ry;
        domo_eyes_world(i, (int)domo_y(i), &lx, &ly, &rx, &ry);
        ly -= camy; ry -= camy;
        unsigned G = RGB(0x40, 0xFF, 0x60);
        if (domos[i].lz) {
            int locked = domos[i].lz <= LZ_LOCK;
            if (locked || (tick & 4)) {
                unsigned c = locked ? WHITE : RGB(0x30, 0xC0, 0x50);
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
    draw_boss_lasers();

    if (live) {
        int oy = cy - camy;
        if (!(inv > 0 && (tick & 4) && !shield)) draw_cat(cx, oy, f, sv.skin, laser_on, meow_pos >= 0);
        if (shield) {
            int r = 46 + ((tick >> 3) & 1);
            circle(cx + CAT_W / 2 - 6, oy + CAT_H / 2, r, 0, RGB(0x30, 0xD0, 0xFF), 40);
            circle(cx + CAT_W / 2 - 6, oy + CAT_H / 2, r, 3, RGB(0x80, 0xE8, 0xFF), 160);
        }
        if (laser_on && state == PLAYING) {
            int ex[2], ey;
            cat_eyes(cx, oy, f, ex, &ey);
            draw_laser(ex[0], ey, -LASER_SLOPE, tick, LASER_GLOW, LASER_MID);
            draw_laser(ex[1], ey, LASER_SLOPE, tick, LASER_GLOW, LASER_MID);
            if (triple_t > 0)
                draw_laser((ex[0] + ex[1]) / 2, ey, 0.f, tick, RGB(0xFF, 0xB0, 0x00), RGB(0xFF, 0xF0, 0x80));
        }
    }

    if (bomb_fx > 0) {
        int r = (45 - bomb_fx) * 14;
        const unsigned *rb = skins[sv.skin].rb;
        for (int b = 0; b < 6; b++)
            circle((int)bomb_x, (int)bomb_y - camy, r - b * 7, 7, rb[b], 170);
    }

    for (int i = 0; i < MAXPOP; i++) {
        if (pops[i].life <= 0) continue;
        int n = strlen(pops[i].s);
        text((int)pops[i].x - n * 4, (int)pops[i].y - camy, pops[i].s, pops[i].c);
    }

    if (shake_t > 0 && sv.shake) {
        int a = shake_t > 10 ? 4 : shake_t > 4 ? 2 : 1;
        shift_fb((int)(rnd() % (2 * a + 1)) - a, (int)(rnd() % (2 * a + 1)) - a);
    }
    if (flash_t > 0) blend(0, 0, W, H, WHITE, flash_t * 18);

    draw_hud();
    if (state == PLAYING) draw_controls();
    if (state == PLAYING && tick - start_tick < 300 && sv.games <= 3) {
        text_center(H / 2 - 30, sv.lefty ? "RIGHT SIDE: DRAG TO MOVE" : "LEFT SIDE: DRAG TO MOVE", WHITE);
        text_center(H / 2 - 18, sv.lefty ? "LEFT SIDE: HOLD FOR LASERS" : "RIGHT SIDE: HOLD FOR LASERS", WHITE);
        text_center(H / 2 - 6, "COLLECT FISH TO BUY UPGRADES", YELLOW);
    }
}

void game_assets_init(void) {
    audio_setup();
    domo_init_palettes();
    for (int i = 0; i < NSTARS; i++) {
        stars[i].x = rnd() % W; stars[i].y = rnd() % WH;
        stars[i].spd = 2 + rnd() % 4; stars[i].phase = rnd() % 24;
    }
}
