/* Screens (title, shop, settings, pause, game over, credits), touch input
 * and the 60 Hz frame loop. */
#include <time.h>
#include "nyan.h"
#include "title_img.h"

Touch touches[MAXTOUCH];
int ntouch;
#define MAXTAP 8
static struct { float x, y; } taps[MAXTAP];   /* touch-downs since last frame */
static int ntap;

static int sel;                               /* highlighted menu item */
static int credits_from = TITLE, shop_from = TITLE, settings_from = TITLE;
static unsigned credits_t;
static int shop_tab;
static int toast_t;
static char toast[48];
static unsigned title_cols[256];

static int in_rect(float x, float y, int rx, int ry, int rw, int rh) {
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}
static int in_circle(float x, float y, int ccx, int ccy, int r) {
    return (x - ccx) * (x - ccx) + (y - ccy) * (y - ccy) <= r * r;
}
static int tapped(int x, int y, int w, int h) {
    for (int t = 0; t < ntap; t++)
        if (in_rect(taps[t].x, taps[t].y, x, y, w, h)) return 1;
    return 0;
}
static void show_toast(const char *s) {
    snprintf(toast, sizeof toast, "%s", s);
    toast_t = 120;
}

int touch_role(int role) {
    for (int i = 0; i < ntouch; i++) if (touches[i].role == role) return i;
    return -1;
}

/* =====================================================================
 * Touch input (called by the platform layer between frames)
 * ===================================================================== */
void game_touch_down(int id, float x, float y) {
    if (ntap < MAXTAP) { taps[ntap].x = x; taps[ntap].y = y; ntap++; }
    if (ntouch >= MAXTOUCH) return;
    int role = R_UI;
    if (state == PLAYING) {
        if (in_rect(x, y, PAUSE_X - 6, 0, PAUSE_W + 12, PAUSE_H + 6)) role = R_UI;
        else if (in_circle(x, y, MEOW_X, MEOW_Y, BTN_R + 6)) { meow_req = 1; role = R_MEOW; }
        else if (in_circle(x, y, BOMB_X, BOMB_Y, BTN_R + 6)) { play_bomb(); role = R_BOMB; }
        else if ((x < W / 2) != (sv.lefty != 0)) role = R_JOY;
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

int game_take_vibration(void) {
    int v = sv.vibrate ? vib_ms : 0;
    vib_ms = 0;
    return v;
}

/* =====================================================================
 * Widgets
 * ===================================================================== */
typedef struct { int x, y, w, h; const char *label; int sc; } Btn;

static void draw_btn(const Btn *b, unsigned c, int hl) {
    blend(b->x, b->y, b->w, b->h, BLACK, hl ? 200 : 160);
    frame_rect(b->x, b->y, b->w, b->h, hl ? c : RGB(0x80, 0x80, 0xA0));
    int tw = text_w(b->label, b->sc), th = 8 * b->sc;
    big_text_at(b->x + (b->w - tw) / 2, b->y + (b->h - th) / 2, b->sc, b->label, c);
}
static int btn_tapped(const Btn *b) { return tapped(b->x, b->y, b->w, b->h); }

static const Btn back_btn = {6, 4, 72, 26, "< BACK", 1};

static void draw_header(const char *title) {
    blend(0, 0, W, 34, BLACK, 120);
    draw_btn(&back_btn, WHITE, 0);
    big_text(9, 2, title, YELLOW);
}
static void draw_coins(int x, int y, int n) {
    char s[16];
    snprintf(s, sizeof s, "%d", n);
    draw_fish(x, y);
    text(x + 22, y + 1, s, YELLOW);
}
static void draw_toast(void) {
    if (toast_t <= 0) return;
    toast_t--;
    int w = text_w(toast, 1) + 16;
    blend((W - w) / 2, H - 30, w, 18, BLACK, 200);
    text_center(H - 25, toast, toast_t & 8 ? WHITE : YELLOW);
}

/* =====================================================================
 * Title
 * ===================================================================== */
static const Btn title_btns[] = {
    {8, 194, 150, 36, "PLAY", 3},
    {8, 236, 84, 30, "SHOP", 2},
    {98, 236, 148, 30, "SETTINGS", 2},
    {252, 236, 132, 30, "CREDITS", 2},
};

static void open_shop(int from) { shop_from = from; state = SHOP; click_req = 1; }
static void open_settings(int from) { settings_from = from; state = SETTINGS; click_req = 1; }

static void scr_title(void) {
    music_track = 1; music_duck = 0;
    laser_on = elaser_on = 0;
    if (btn_tapped(&title_btns[0])) { play_reset(); meow_req = 1; return; }
    if (btn_tapped(&title_btns[1])) { open_shop(TITLE); return; }
    if (btn_tapped(&title_btns[2])) { open_settings(TITLE); return; }
    if (btn_tapped(&title_btns[3])) { state = CREDITS; credits_from = TITLE; credits_t = 0; click_req = 1; return; }

    for (int y = 0; y < TITLE_H; y++) {
        unsigned *row = cur + y * STRIDE;
        const unsigned char *p = title_pix + y * TITLE_W;
        for (int x = 0; x < TITLE_W; x++) row[x] = title_cols[p[x]];
    }
    for (int i = 0; i < 4; i++)
        draw_btn(&title_btns[i], i == 0 ? ((ui & 16) ? YELLOW : WHITE) : GREY, i == 0);
    blend(4, 4, 124, 30, BLACK, 150);
    draw_coins(10, 8, sv.coins);
    char s[32];
    snprintf(s, sizeof s, "BEST %d", sv.hiscore);
    text(10, 22, s, WHITE);
}

/* =====================================================================
 * Shop
 * ===================================================================== */
static const Btn tab_btns[2] = {{110, 4, 140, 26, "UPGRADES", 2}, {256, 4, 110, 26, "SKINS", 2}};

static void buy_fail(const char *why) { buzz_req = 1; show_toast(why); }

static void shop_upgrades(void) {
    for (int u = 0; u < UPG_COUNT; u++) {
        int y = 38 + u * 38;
        const Upgrade *up = &upgrades[u];
        int lv = sv.upg[u], maxed = lv >= up->max;
        int price = maxed ? 0 : up->price[lv];
        Btn b = {W - 104, y + 4, 94, 26, "", 2};
        char ps[16];
        snprintf(ps, sizeof ps, maxed ? "MAX" : "%d", price);
        if (btn_tapped(&b)) {
            if (maxed) buy_fail("ALREADY MAXED OUT");
            else if (sv.coins < price) buy_fail("NOT ENOUGH FISH - KEEP PLAYING!");
            else {
                sv.coins -= price; sv.upg[u]++; save_write();
                power_req = 1; vib_ms = 30;
                show_toast("UPGRADED!");
            }
        }
        dim(6, y, W - 12, 34);
        big_text_at(14, y + 3, 2, up->name, WHITE);
        text(14, y + 23, up->desc, GREY);
        for (int k = 0; k < up->max; k++) {
            rect(260 + k * 14, y + 12, 10, 10, k < sv.upg[u] ? YELLOW : RGB(0x40, 0x40, 0x50));
            frame_rect(260 + k * 14, y + 12, 10, 10, GREY);
        }
        int afford = !maxed && sv.coins >= price;
        blend(b.x, b.y, b.w, b.h, afford ? RGB(0x20, 0x80, 0x30) : BLACK, 200);
        frame_rect(b.x, b.y, b.w, b.h, afford ? GREEN : RGB(0x80, 0x80, 0xA0));
        if (maxed) big_text_at(b.x + (b.w - 48) / 2, b.y + 5, 2, "MAX", YELLOW);
        else {
            int tw = text_w(ps, 1);
            draw_fish(b.x + (b.w - tw - 22) / 2, b.y + 8);
            text(b.x + (b.w - tw - 22) / 2 + 22, b.y + 9, ps, afford ? WHITE : RED);
        }
    }
}

static void shop_skins(void) {
    for (int i = 0; i < NSKINS; i++) {
        int x0 = 6 + (i % 3) * 157, y0 = 38 + (i / 3) * 116;
        const Skin *sk = &skins[i];
        int owned = (sv.skins_owned >> i) & 1, locked = sv.bosses < sk->req_bosses;
        if (tapped(x0, y0, 152, 112)) {
            if (owned) { sv.skin = i; save_write(); click_req = 1; }
            else if (locked) buy_fail("DEFEAT MORE KING DOMOS FIRST");
            else if (sv.coins < sk->price) buy_fail("NOT ENOUGH FISH - KEEP PLAYING!");
            else {
                sv.coins -= sk->price; sv.skins_owned |= 1 << i; sv.skin = i; save_write();
                power_req = 1; vib_ms = 30;
                show_toast("NEW SKIN UNLOCKED!");
            }
        }
        dim(x0, y0, 152, 112);
        if (sv.skin == i) { frame_rect(x0, y0, 152, 112, YELLOW); frame_rect(x0 + 1, y0 + 1, 150, 110, YELLOW); }
        int ox = x0 + 44, oy = y0 + 14;
        for (int b = 0; b < 6; b++) rect(x0 + 6, oy + 6 + b * 9, ox + 20 - (x0 + 6), 9, sk->rb[b]);
        draw_cat(ox, oy, ui / 5, i, 0, 0);
        if (locked && !owned) blend(x0 + 2, y0 + 2, 148, 76, BLACK, 140);
        int n = strlen(sk->name);
        text(x0 + (152 - n * 8) / 2, y0 + 82, sk->name, WHITE);
        char s[32];
        if (sv.skin == i) snprintf(s, sizeof s, "EQUIPPED");
        else if (owned) snprintf(s, sizeof s, "TAP TO EQUIP");
        else if (locked) snprintf(s, sizeof s, "BEAT %d KING DOMO%s", sk->req_bosses, sk->req_bosses > 1 ? "S" : "");
        else snprintf(s, sizeof s, "%d", sk->price);
        n = strlen(s);
        if (!owned && !locked) {
            draw_fish(x0 + (152 - n * 8 - 22) / 2, y0 + 95);
            text(x0 + (152 - n * 8 - 22) / 2 + 22, y0 + 96, s, sv.coins >= sk->price ? WHITE : RED);
        } else {
            text(x0 + (152 - n * 8) / 2, y0 + 96, s, sv.skin == i ? YELLOW : (locked ? RED : GREEN));
        }
    }
}

static void scr_shop(void) {
    music_duck = 0;
    if (btn_tapped(&back_btn)) { state = shop_from; click_req = 1; return; }
    for (int i = 0; i < 2; i++)
        if (btn_tapped(&tab_btns[i]) && shop_tab != i) { shop_tab = i; click_req = 1; ntap = 0; }
    draw_space(ui);
    blend(0, 0, W, 34, BLACK, 120);
    draw_btn(&back_btn, WHITE, 0);
    for (int i = 0; i < 2; i++) draw_btn(&tab_btns[i], shop_tab == i ? YELLOW : GREY, shop_tab == i);
    draw_coins(W - 90, 12, sv.coins);
    if (shop_tab == 0) shop_upgrades(); else shop_skins();
    draw_toast();
}

/* =====================================================================
 * Settings
 * ===================================================================== */
static void scr_settings(void) {
    if (btn_tapped(&back_btn)) { state = settings_from; click_req = 1; save_write(); return; }
    int *vals[6] = {&sv.music, &sv.sfx, &sv.vibrate, &sv.shake, &sv.lefty, &sv.fps};
    static const char *names[6] = {"MUSIC", "SOUND EFFECTS", "VIBRATION", "SCREEN SHAKE", "JOYSTICK SIDE", "SHOW FPS"};
    for (int i = 0; i < 6; i++)
        if (tapped(30, 40 + i * 30, W - 60, 27)) {
            *vals[i] = !*vals[i];
            click_req = 1;
            if (i == 2 && sv.vibrate) vib_ms = 60;
            save_write();
        }

    if (settings_from == PAUSED) { play_draw(); dim(0, 0, W, H); dim(0, 0, W, H); }
    else draw_space(ui);
    draw_header("SETTINGS");
    for (int i = 0; i < 6; i++) {
        int y = 40 + i * 30;
        blend(30, y, W - 60, 27, BLACK, 200);
        big_text_at(40, y + 6, 2, names[i], WHITE);
        const char *v = i == 4 ? (*vals[i] ? "RIGHT" : "LEFT") : (*vals[i] ? "ON" : "OFF");
        unsigned c = i == 4 ? YELLOW : (*vals[i] ? GREEN : RED);
        big_text_at(W - 40 - text_w(v, 2), y + 6, 2, v, c);
    }
    char s[96];
    snprintf(s, sizeof s, "GAMES %d   DOMOS LASERED %d   KING DOMOS %d", sv.games, sv.total_kills, sv.bosses);
    text_center(226, s, GREY);
    snprintf(s, sizeof s, "BEST SCORE %d   MOST KILLS IN A RUN %d", sv.hiscore, sv.best_kills);
    text_center(240, s, GREY);
}

/* =====================================================================
 * Pause, game over
 * ===================================================================== */
static const char *pause_items[] = {"RESUME", "SETTINGS", "QUIT TO TITLE"};

static void to_title(void) {
    state = TITLE; sel = 0;
    if (score > sv.hiscore) sv.hiscore = score;
    save_write();
}

static void scr_paused(void) {
    music_duck = 1;
    laser_on = elaser_on = 0;
    for (int i = 0; i < 3; i++)
        if (tapped(60, 111 + i * 30, W - 120, 30)) {
            click_req = 1;
            if (i == 0) { state = PLAYING; music_duck = 0; }
            else if (i == 1) open_settings(PAUSED);
            else to_title();
            return;
        }
    play_draw();
    dim(0, 0, W, H);
    big_text(46, 5, "PAUSED", WHITE);
    for (int i = 0; i < 3; i++) {
        Btn b = {W / 2 - 120, 113 + i * 30, 240, 26, pause_items[i], 2};
        draw_btn(&b, i == 0 ? YELLOW : GREY, i == 0);
    }
    text_center(H - 18, "TAP TO SELECT   BACK: RESUME", RGB(0xA0, 0xA0, 0xA0));
}

static const Btn dead_btns[3] = {
    {74, 184, 104, 34, "RETRY", 2}, {188, 184, 104, 34, "SHOP", 2}, {302, 184, 104, 34, "MENU", 2},
};

static void scr_dead(void) {
    int ready = tick - dead_tick > 60;
    if (ready) {
        if (btn_tapped(&dead_btns[0])) { play_reset(); click_req = 1; return; }
        if (btn_tapped(&dead_btns[1])) { open_shop(DEAD); return; }
        if (btn_tapped(&dead_btns[2])) { click_req = 1; to_title(); return; }
    }
    play_update();
    play_draw();
    tick++;
    big_text(24, 5, "GAME OVER", RGB(0xFF, 0x50, 0x50));
    char s[64];
    snprintf(s, sizeof s, "SCORE %d", score);
    big_text(74, 3, s, WHITE);
    if (new_best && (ui & 16)) big_text(104, 2, "NEW BEST!", YELLOW);
    snprintf(s, sizeof s, "DOMOS %d", kills);
    big_text_at(W / 2 - 150, 132, 2, s, GREY);
    draw_fish(W / 2 + 30, 135);
    snprintf(s, sizeof s, "+%d", run_coins);
    big_text_at(W / 2 + 54, 132, 2, s, YELLOW);
    if (ready) {
        for (int i = 0; i < 3; i++) draw_btn(&dead_btns[i], i == 0 ? YELLOW : WHITE, i == 0);
        if (sv.coins >= 100 && (ui & 32)) text_center(226, "SPEND YOUR FISH IN THE SHOP!", GREEN);
    }
}

/* =====================================================================
 * Credits
 * ===================================================================== */
/* '#' = title, '*' = heading, '' = blank, else = name */
static const char *credits[] = {
    "#DOMO-KUN",
    "#VS",
    "#NYAN CAT",
    "#ANDROID EDITION",
    "",
    "*IDEA & DIRECTION",
    "Mcl0v1n",
    "",
    "*PROGRAMMING, PIXEL ART & MUSIC",
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
    "*MUSIC & SOUND",
    "ORIGINAL CHIPTUNES,",
    "SYNTHESIZED IN CODE",
    "",
    "*TOOLS",
    "PSPDEV TOOLCHAIN / PSPSDK",
    "PPSSPP EMULATOR",
    "ANDROID NDK",
    "",
    "*SPECIAL THANKS",
    "EVERY DOMO WHO GOT LASERED",
    "",
    "",
    "*THANKS FOR PLAYING!",
};
#define NCREDITS (int)(sizeof credits / sizeof credits[0])
#define CREDIT_LINE 24

static void scr_credits(void) {
    music_duck = 0;
    credits_t++;
    if (ntap) { state = credits_from; sel = 1; click_req = 1; return; }
    draw_space(credits_t);
    int f = ui / 5;
    int span = W + 400;
    int catx = (int)(credits_t * 2 % span) - 200;
    int caty = H - 70;
    const unsigned *rb = skins[sv.skin].rb;
    for (int b = 0; b < 6; b++)
        for (int x = catx + 24, k = 0; x > -24; x -= 24, k++)
            rect(x - 24, caty + ((k + f) & 1) * 3 + b * 9, 24, 9, rb[b]);
    draw_cat(catx, caty, f, sv.skin, ui & 16, 0);
    draw_domo(catx + 180, caty - 6 + (int)(6 * sinf(ui * 0.1f)), (ui & 8) ? 1 : 0, ui, 0, 1);
    int ex[2], ey;
    cat_eyes(catx, caty, f, ex, &ey);
    if ((ui & 16) && catx > -20 && catx < W) draw_laser(ex[0], ey, 0.f, ui, RGB(0xFF, 0x10, 0x30), RGB(0xFF, 0x90, 0x90));

    int total = 0;
    for (int i = 0; i < NCREDITS; i++) total += credits[i][0] == '#' ? 34 : CREDIT_LINE;
    total += H - 90;
    int y = H - 90 - (int)(credits_t * 0.6f) % total;
    for (int i = 0; i < NCREDITS; i++) {
        const char *s = credits[i];
        int lh = s[0] == '#' ? 34 : CREDIT_LINE;
        int th = s[0] == '#' ? 24 : 16;
        if (y > 14 && y + th < H - 86) {
            if (s[0] == '#')      big_text(y, 3, s + 1, skins[0].rb[(i + ui / 8) % 6]);
            else if (s[0] == '*') big_text(y, 2, s + 1, RGB(0xFF, 0xE0, 0x40));
            else if (s[0])        big_text(y, 2, s, WHITE);
        }
        y += lh;
    }
    dim(0, 0, W, 14);
    text_center(3, "TAP / BACK: RETURN", GREY);
}

/* =====================================================================
 * Frame loop + system keys
 * ===================================================================== */
static unsigned now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned)(ts.tv_sec * 1000000ull + ts.tv_nsec / 1000);
}

void game_init(const char *save_dir) {
    rng = now_us() | 1;
    save_load(save_dir);
    game_assets_init();
    for (int i = 0; i < 256; i++)
        title_cols[i] = RGB(title_pal[i][0], title_pal[i][1], title_pal[i][2]);
    play_reset();
    sv.games--;                 /* play_reset counted a game; this one is just the backdrop */
    state = TITLE;
}

int game_back(void) {
    click_req = 1;
    switch (state) {
    case TITLE:    save_write(); return 0;
    case CREDITS:  state = credits_from; break;
    case SHOP:     state = shop_from; break;
    case SETTINGS: state = settings_from; save_write(); break;
    case PAUSED:   state = PLAYING; music_duck = 0; break;
    case PLAYING:  state = PAUSED; sel = 0; break;
    case DEAD:     if (tick - dead_tick > 60) to_title(); break;
    }
    return 1;
}
void game_menu_key(void) { if (state == PLAYING) { state = PAUSED; sel = 0; } }
void game_suspend(void) {
    if (state == PLAYING) { state = PAUSED; sel = 0; }
    laser_on = elaser_on = 0;
    game_touch_cancel_all();
    save_write();
}

void game_frame(void) {
    static unsigned t_prev, work_us, frame_us = 16667;
    unsigned t_start = now_us();
    if (t_prev) frame_us = (frame_us * 7 + (t_start - t_prev)) / 8;
    t_prev = t_start;
    if (rng == 0) rng = 1;
    rnd();

    switch (state) {
    case TITLE:    scr_title(); break;
    case SHOP:     scr_shop(); break;
    case SETTINGS: scr_settings(); break;
    case CREDITS:  scr_credits(); break;
    case PAUSED:   scr_paused(); break;
    case DEAD:     scr_dead(); break;
    case PLAYING:
        music_duck = 0;
        if (tapped(PAUSE_X - 6, 0, PAUSE_W + 12, PAUSE_H + 6)) {
            state = PAUSED; sel = 0; click_req = 1;
            laser_on = elaser_on = 0;
            scr_paused();
            break;
        }
        play_update();
        play_draw();
        tick++;
        break;
    }
    /* a screen switch this frame leaves the old picture; draw the new one next frame */
    ntap = 0;

    if (sv.fps) {
        char p[48];
        unsigned w = work_us;
        snprintf(p, sizeof p, "CPU %u.%uMS  %uFPS", w / 1000, (w / 100) % 10,
                 frame_us ? (1000000 + frame_us / 2) / frame_us : 0);
        dim(W / 2 - 76, H - 14, 152, 12);
        text(W / 2 - 72, H - 12, p, w < 16000 ? GREEN : RED);
    }
    work_us = (work_us * 7 + (now_us() - t_start)) / 8;
    ui++;
}
