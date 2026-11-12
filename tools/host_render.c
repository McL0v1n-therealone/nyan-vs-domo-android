/* Host-side test harness: runs the game core without Android, simulates
 * touches and dumps frames as PNG-ready PPM files.
 *   cc -O2 -Isrc tools/host_render.c src/game.c -lm -o build/host_render */
#include <stdio.h>
#include <stdlib.h>
#include "game.h"

static void dump(const char *name) {
    FILE *f = fopen(name, "wb");
    fprintf(f, "P6\n%d %d\n255\n", GAME_W, GAME_H);
    for (int y = 0; y < GAME_H; y++)
        for (int x = 0; x < GAME_W; x++) {
            unsigned int p = game_fb[y * GAME_STRIDE + x];
            unsigned char c[3] = {p & 255, (p >> 8) & 255, (p >> 16) & 255};
            fwrite(c, 1, 3, f);
        }
    fclose(f);
}
static void frames(int n) { short a[2048]; for (int i = 0; i < n; i++) { game_frame(); game_audio(a, 1024); } }

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : ".";
    char p[512];
    game_init(NULL);
    frames(10);
    snprintf(p, sizeof p, "%s/title.ppm", out); dump(p);
    game_touch_down(0, 100, 220); game_touch_up(0);          /* START GAME */
    frames(60);
    game_touch_down(1, 80, 200);                             /* joystick */
    game_touch_move(1, 100, 185);
    game_touch_down(2, 400, 200);                            /* laser */
    frames(200);
    snprintf(p, sizeof p, "%s/play.ppm", out); dump(p);
    game_touch_up(1); game_touch_up(2);
    game_touch_down(3, 240, 10); game_touch_up(3);           /* pause */
    frames(2);
    snprintf(p, sizeof p, "%s/pause.ppm", out); dump(p);
    game_back();                                             /* resume */
    for (int i = 0; i < 6000; i++) frames(1);                /* idle until game over */
    snprintf(p, sizeof p, "%s/dead.ppm", out); dump(p);
    game_touch_down(4, 240, 175); game_touch_up(4);          /* retry */
    frames(30);
    snprintf(p, sizeof p, "%s/retry.ppm", out); dump(p);
    printf("ok\n");
    return 0;
}
