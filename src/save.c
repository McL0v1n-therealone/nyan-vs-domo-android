/* Persistent progress + settings: a small "key value" text file in the app's
 * private storage, written atomically (tmp file + rename). */
#include <stdlib.h>
#include "nyan.h"

Save sv = { .skins_owned = 1, .music = 1, .sfx = 1, .vibrate = 1, .shake = 1 };
static char path[512];

static const struct { const char *key; int *val; } fields[] = {
    {"hiscore", &sv.hiscore}, {"coins", &sv.coins},
    {"skins", &sv.skins_owned}, {"skin", &sv.skin},
    {"music", &sv.music}, {"sfx", &sv.sfx}, {"vibrate", &sv.vibrate},
    {"shake", &sv.shake}, {"lefty", &sv.lefty}, {"fps", &sv.fps},
    {"games", &sv.games}, {"total_kills", &sv.total_kills},
    {"bosses", &sv.bosses}, {"best_kills", &sv.best_kills},
};
#define NFIELDS (int)(sizeof fields / sizeof fields[0])

void save_load(const char *dir) {
    if (!dir) return;
    snprintf(path, sizeof path, "%s/save.txt", dir);
    FILE *f = fopen(path, "r");
    if (f) {
        char key[32];
        int v;
        while (fscanf(f, "%31s %d", key, &v) == 2) {
            for (int i = 0; i < NFIELDS; i++)
                if (!strcmp(key, fields[i].key)) *fields[i].val = v;
            if (!strncmp(key, "upg", 3)) {
                int u = atoi(key + 3);
                if (u >= 0 && u < UPG_COUNT) sv.upg[u] = v;
            }
        }
        fclose(f);
    }
    /* sanitize */
    sv.skins_owned |= 1;
    if (sv.skin < 0 || sv.skin >= NSKINS || !(sv.skins_owned & (1 << sv.skin))) sv.skin = 0;
    for (int u = 0; u < UPG_COUNT; u++) {
        if (sv.upg[u] < 0) sv.upg[u] = 0;
        if (sv.upg[u] > upgrades[u].max) sv.upg[u] = upgrades[u].max;
    }
    if (sv.coins < 0) sv.coins = 0;
}

void save_write(void) {
    if (!path[0]) return;
    char tmp[520];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int i = 0; i < NFIELDS; i++) fprintf(f, "%s %d\n", fields[i].key, *fields[i].val);
    for (int u = 0; u < UPG_COUNT; u++) fprintf(f, "upg%d %d\n", u, sv.upg[u]);
    if (fclose(f) == 0) rename(tmp, path);
}
