/* Internal header shared by the game modules:
 *   gfx.c    software renderer (rects, alpha blending, text)
 *   audio.c  synthesized sound effects + chiptune music
 *   save.c   persistent progress and settings
 *   game.c   gameplay: cat, Domos, King Domo, items, HUD
 *   ui.c     menus, shop, settings, touch handling, frame loop */
#ifndef NYAN_H
#define NYAN_H

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "game.h"

#define W GAME_W
#define H GAME_H
#define STRIDE GAME_STRIDE
#define RGB(r, g, b) (0xFF000000u | ((unsigned)(b) << 16) | ((unsigned)(g) << 8) | (unsigned)(r))
#define BLACK RGB(0, 0, 0)
#define WHITE RGB(0xFF, 0xFF, 0xFF)
#define YELLOW RGB(0xFF, 0xFF, 0x60)
#define GREY RGB(0xC0, 0xC0, 0xC0)
#define RED RGB(0xFF, 0x50, 0x50)
#define GREEN RGB(0x80, 0xFF, 0x80)

/* ---------------- gfx.c ---------------- */
extern unsigned int *cur;
void rect(int x, int y, int w, int h, unsigned c);
void blend(int x, int y, int w, int h, unsigned c, int a);        /* a = 0..256 */
void circle(int cx, int cy, int r, int thick, unsigned c, int a); /* thick 0 = filled */
void dim(int x, int y, int w, int h);
void frame_rect(int x, int y, int w, int h, unsigned c);         /* 1 px outline */
void text(int x, int y, const char *s, unsigned c);
void text_center(int y, const char *s, unsigned c);
void big_text_at(int x, int y, int sc, const char *s, unsigned c);
void big_text(int y, int sc, const char *s, unsigned c);          /* centered */
int  text_w(const char *s, int sc);
void shift_fb(int dx, int dy);                                    /* screen shake */
void draw_art(int x, int y, int px, const char **art, int rows, const char *keys, const unsigned *cols);

/* ---------------- rng ---------------- */
extern unsigned rng;
unsigned rnd(void);
float frnd(void);

/* ---------------- audio.c ---------------- */
extern volatile int meow_pos, meow_req, boom_req, laser_on, spit_req, elaser_on;
extern volatile int coin_req, power_req, bomb_req, alarm_req, click_req, buzz_req;
extern volatile int music_track;   /* 0 silence, 1 main theme, 2 boss theme */
extern volatile int music_duck;    /* 1 = quieter (pause menu) */
void audio_setup(void);

/* ---------------- save.c ---------------- */
enum { UPG_LASER, UPG_LIVES, UPG_BOMBS, UPG_SHIELD, UPG_POWER, UPG_MAGNET, UPG_COUNT };
#define NSKINS 6
typedef struct {
    int hiscore, coins;
    int upg[UPG_COUNT];
    int skins_owned, skin;                   /* bitmask, selected */
    int music, sfx, vibrate, shake, lefty, fps;
    int games, total_kills, bosses, best_kills;
} Save;
extern Save sv;
void save_load(const char *dir);
void save_write(void);

typedef struct { const char *name, *desc; int max, price[3]; } Upgrade;
extern const Upgrade upgrades[UPG_COUNT];
typedef struct {
    const char *name; int price, req_bosses;
    unsigned body, cheek, crust, fill, sprinkle, rb[6];
} Skin;
extern const Skin skins[NSKINS];

/* ---------------- game.c ---------------- */
enum { TITLE, PLAYING, PAUSED, CREDITS, DEAD, SHOP, SETTINGS };
extern int state;
extern unsigned tick, ui;           /* game time (frozen when paused), real frames */
extern int score, kills, run_coins, new_best, lives, bombs;
extern unsigned dead_tick;
extern int vib_ms;                  /* pending vibration request */
void game_assets_init(void);
void play_reset(void);
void play_update(void);
void play_draw(void);
void play_bomb(void);
void draw_cat(int ox, int oy, int f, int skin, int glow, int mouth);
void cat_eyes(int ox, int oy, int f, int ex[2], int *ey);
void draw_laser(int x0, int y0, float slope, unsigned t, unsigned glow, unsigned mid);
void draw_domo(int x, int y, int mode, unsigned t, int var, int sc);
void draw_star(int x, int y, int ph);
void draw_fish(int x, int y);
void draw_space(unsigned t);        /* starry menu background */

/* ---------------- touch (ui.c) ---------------- */
enum { R_NONE, R_JOY, R_FIRE, R_MEOW, R_BOMB, R_UI };
#define MAXTOUCH 8
typedef struct { int id, role; float x, y, bx, by; } Touch;
extern Touch touches[MAXTOUCH];
extern int ntouch;
int touch_role(int role);

/* on-screen control layout (mirrored for left-handed play) */
#define JOY_R 34.f
#define MEOW_X (sv.lefty ? 34 : W - 34)
#define MEOW_Y 66
#define BOMB_X MEOW_X
#define BOMB_Y 118
#define BTN_R 20
#define FIRE_X (sv.lefty ? 66 : W - 66)
#define FIRE_Y (H - 62)
#define FIRE_R 42
#define PAUSE_X (W / 2 - 18)
#define PAUSE_W 36
#define PAUSE_H 26

#endif
