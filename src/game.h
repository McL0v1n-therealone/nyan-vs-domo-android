/* Platform-independent game core (ported from the PSP version).
 * The game draws into a 480x272 software framebuffer; the platform layer
 * (main.c) uploads it to the GPU, feeds touches/keys in and pulls audio out. */
#ifndef NYAN_GAME_H
#define NYAN_GAME_H

#define GAME_W 480
#define GAME_H 272
#define GAME_STRIDE 512                 /* framebuffer row length in pixels */
#define AUDIO_RATE 44100

/* RGBA bytes in memory (= 0xAABBGGRR as a little-endian uint32) */
extern unsigned int game_fb[GAME_STRIDE * GAME_H];

void game_init(const char *save_dir);
void game_frame(void);                  /* one 60 Hz step: update + draw */
void game_audio(short *out, int frames); /* fill interleaved stereo s16 */

/* touch input, coordinates in game pixels (may be outside the screen) */
void game_touch_down(int id, float x, float y);
void game_touch_move(int id, float x, float y);
void game_touch_up(int id);
void game_touch_cancel_all(void);

int  game_back(void);                   /* BACK key; returns 0 = quit the app */
void game_menu_key(void);               /* MENU key: pause */
void game_suspend(void);                /* app lost focus: pause + save */
int  game_take_vibration(void);         /* ms to vibrate now (0 = none) */

#endif
