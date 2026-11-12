/* Host-side test harness: runs the game without Android, drives it with
 * simulated touches (a simple bot plays the game), dumps screenshots and
 * the audio. Build + run:
 *   cc -O1 -g -fsanitize=address,undefined -Isrc -DSTART_KILLS=24 \
 *      tools/host_render.c src/gfx.c src/audio.c src/save.c src/game.c src/ui.c -lm -o build/host_render
 *   ./build/host_render build/host */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "nyan.h"

static const char *out = ".";
static FILE *wav;
static long wav_frames;
static double audio_peak, audio_sq;

static void dump(const char *name) {
    char p[512];
    snprintf(p, sizeof p, "%s/%s.ppm", out, name);
    FILE *f = fopen(p, "wb");
    fprintf(f, "P6\n%d %d\n255\n", GAME_W, GAME_H);
    for (int y = 0; y < GAME_H; y++)
        for (int x = 0; x < GAME_W; x++) {
            unsigned p = game_fb[y * GAME_STRIDE + x];
            unsigned char c[3] = {p & 255, (p >> 8) & 255, (p >> 16) & 255};
            fwrite(c, 1, 3, f);
        }
    fclose(f);
}
static void frames(int n) {
    static short a[735 * 2];
    for (int i = 0; i < n; i++) {
        game_frame();
        game_audio(a, 735);
        for (int k = 0; k < 735; k++) {
            double v = a[2 * k] / 32768.0;
            if (fabs(v) > audio_peak) audio_peak = fabs(v);
            audio_sq += v * v;
        }
        wav_frames += 735;
        if (wav) fwrite(a, 4, 735, wav);
        if (game_take_vibration() < 0) abort();
    }
}
static void tap(float x, float y) { static int id = 100; game_touch_down(id, x, y); game_touch_up(id++); frames(2); }

static void wav_header(FILE *f, long n) {
    int bytes = (int)(n * 4);
    fwrite("RIFF", 1, 4, f); int v = 36 + bytes; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    short s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    v = 44100; fwrite(&v, 4, 1, f); v = 44100 * 4; fwrite(&v, 4, 1, f);
    s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
}

/* bot: hold the laser, steer toward the nearest threat's height, dodge sometimes */
static void bot_play(int n, int dump_every, const char *prefix) {
    game_touch_down(1, 80, 200);          /* joystick */
    game_touch_down(2, 400, 220);         /* laser */
    for (int i = 0; i < n && state == PLAYING; i++) {
        float dy = 14.f * sinf(i * 0.02f), dx = 6.f * sinf(i * 0.013f);
        game_touch_move(1, 80 + dx, 200 + dy);
        if (i % 600 == 300) { game_touch_down(3, BOMB_X, BOMB_Y); game_touch_up(3); }
        frames(1);
        if (dump_every && i % dump_every == dump_every - 1) {
            char nm[64]; snprintf(nm, sizeof nm, "%s%03d", prefix, i / dump_every); dump(nm);
        }
    }
    game_touch_up(1); game_touch_up(2);
}

int main(int argc, char **argv) {
    out = argc > 1 ? argv[1] : ".";
    char p[512];
    snprintf(p, sizeof p, "%s/save", out); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/save/save.txt", out); remove(p);
    snprintf(p, sizeof p, "%s/audio.wav", out);
    wav = fopen(p, "wb"); wav_header(wav, 0);

    snprintf(p, sizeof p, "%s/save", out);
    game_init(p);
    frames(10); dump("title");

    tap(40, 250); frames(2); dump("shop_empty");               /* SHOP */
    sv.coins = 3000;
    tap(W - 60, 38 + 4 + 10);                                   /* buy LASER POWER */
    tap(W - 60, 38 + 38 * 5 + 14);                              /* buy FISH MAGNET */
    frames(5); dump("shop_upgrades");
    tap(300, 15); frames(2);                                    /* SKINS tab */
    tap(6 + 157 + 70, 38 + 50);                                 /* buy TAC NAYN */
    tap(6 + 157 * 2 + 70, 38 + 116 + 50);                       /* COSMIC: locked */
    frames(5); dump("shop_skins");
    tap(30, 15); frames(2);                                     /* back */
    tap(150, 250); frames(2); dump("settings");                 /* SETTINGS */
    tap(30, 15); frames(2);

    tap(80, 210); frames(60); dump("play_start");               /* PLAY */
    bot_play(6000, 300, "run");
    printf("after run 1: state=%d kills=%d score=%d coins=%d bosses=%d lives=%d\n",
           state, kills, score, run_coins, sv.bosses, lives);
    frames(70); dump("dead");
    tap(126, 200); frames(30);                                  /* RETRY */
    game_touch_down(9, 240, 10); game_touch_up(9); frames(3); dump("pause");
    tap(240, 113 + 30 + 10); frames(3); dump("settings_from_pause");
    game_back(); frames(2);
    game_back(); frames(2);                                     /* resume */
    game_back(); frames(2); tap(240, 113 + 60 + 10); frames(3); /* pause -> quit */
    printf("state after quit: %d (title=%d)\n", state, TITLE);
    tap(300, 250); frames(400); dump("credits");

    /* fuzz: random taps everywhere in every state */
    srand(1);
    for (int i = 0; i < 20000; i++) {
        int r = rand() % 100;
        if (r < 3) tap(rand() % W, rand() % H);
        else if (r < 4) game_back();
        else if (r < 6) { game_touch_down(50 + i % 4, rand() % W, rand() % H); }
        else if (r < 8) game_touch_up(50 + rand() % 4);
        else if (r < 9) game_suspend();
        frames(1);
        if (state == TITLE && rand() % 50 == 0) tap(80, 210);
    }
    printf("fuzz done: state=%d games=%d bosses=%d coins=%d\n", state, sv.games, sv.bosses, sv.coins);

    fseek(wav, 0, SEEK_SET); wav_header(wav, wav_frames); fclose(wav);
    printf("audio: peak %.2f  rms %.3f\n", audio_peak, sqrt(audio_sq / wav_frames));
    printf("ok\n");
    return 0;
}
