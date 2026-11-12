/* Software renderer: everything is drawn into the 480x272 game_fb. */
#include "nyan.h"
#include "font8x8.h"

unsigned int game_fb[STRIDE * H];
unsigned int *cur = game_fb;

void rect(int x, int y, int w, int h, unsigned c) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    for (int j = y0; j < y1; j++) {
        unsigned *p = cur + j * STRIDE;
        for (int i = x0; i < x1; i++) p[i] = c;
    }
}

void blend(int x, int y, int w, int h, unsigned c, int a) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    unsigned rb = c & 0x00FF00FF, g = c & 0x0000FF00;
    for (int j = y0; j < y1; j++) {
        unsigned *p = cur + j * STRIDE;
        for (int i = x0; i < x1; i++) {
            unsigned d = p[i];
            unsigned drb = ((rb * a + (d & 0x00FF00FF) * (256 - a)) >> 8) & 0x00FF00FF;
            unsigned dg = ((g * a + (d & 0x0000FF00) * (256 - a)) >> 8) & 0x0000FF00;
            p[i] = 0xFF000000 | drb | dg;
        }
    }
}

void circle(int cx0, int cy0, int r, int thick, unsigned c, int a) {
    if (r <= 0) return;
    int ri = thick > 0 ? r - thick : -1;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy0 + dy;
        if (y < 0 || y >= H) continue;
        int ho = (int)sqrtf((float)(r * r - dy * dy));
        if (ri < 0 || dy * dy >= ri * ri) {
            blend(cx0 - ho, y, 2 * ho + 1, 1, c, a);
        } else {
            int hi = (int)sqrtf((float)(ri * ri - dy * dy));
            blend(cx0 - ho, y, ho - hi, 1, c, a);
            blend(cx0 + hi + 1, y, ho - hi, 1, c, a);
        }
    }
}

void dim(int x, int y, int w, int h) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
    for (int j = y0; j < y1; j++) {
        unsigned *p = cur + j * STRIDE;
        for (int i = x0; i < x1; i++) p[i] = ((p[i] >> 1) & 0x007F7F7F) | 0xFF000000;
    }
}

void frame_rect(int x, int y, int w, int h, unsigned c) {
    rect(x, y, w, 1, c); rect(x, y + h - 1, w, 1, c);
    rect(x, y, 1, h, c); rect(x + w - 1, y, 1, h, c);
}

/* one 8x8 glyph of the PSP debug font (MSB = leftmost pixel) */
static void put_char(int x, int y, unsigned c, unsigned char ch) {
    const unsigned char *g = font8x8 + ch * 8;
    for (int j = 0; j < 8; j++)
        for (int i = 0; i < 8; i++)
            if (g[j] & (0x80 >> i)) cur[(y + j) * STRIDE + x + i] = c;
}

void text(int x, int y, const char *s, unsigned c) {
    for (; *s; s++, x += 8) {
        if (x < 0 || x > W - 9 || y < 0 || y > H - 9) continue;
        put_char(x + 1, y + 1, BLACK, *s);
        put_char(x, y, c, *s);
    }
}

void text_center(int y, const char *s, unsigned c) { text((W - (int)strlen(s) * 8) / 2, y, s, c); }

int text_w(const char *s, int sc) { return (int)strlen(s) * 8 * sc; }

void big_text_at(int x, int y, int sc, const char *s, unsigned c) {
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

void big_text(int y, int sc, const char *s, unsigned c) {
    big_text_at((W - text_w(s, sc)) / 2, y, sc, s, c);
}

/* move the whole picture by (dx,dy); the uncovered edge repeats the border */
void shift_fb(int dx, int dy) {
    static unsigned tmp[STRIDE * H];
    if (!dx && !dy) return;
    memcpy(tmp, game_fb, sizeof tmp);
    for (int j = 0; j < H; j++) {
        int sj = j - dy;
        sj = sj < 0 ? 0 : (sj >= H ? H - 1 : sj);
        unsigned *d = game_fb + j * STRIDE, *s = tmp + sj * STRIDE;
        for (int i = 0; i < W; i++) {
            int si = i - dx;
            d[i] = s[si < 0 ? 0 : (si >= W ? W - 1 : si)];
        }
    }
}

/* pixel art from strings: keys[k] is drawn in cols[k], other chars are transparent */
void draw_art(int x, int y, int px, const char **art, int rows, const char *keys, const unsigned *cols) {
    for (int j = 0; j < rows; j++)
        for (int i = 0; art[j][i]; i++) {
            const char *k = strchr(keys, art[j][i]);
            if (k && art[j][i] != '.') rect(x + i * px, y + j * px, px, px, cols[k - keys]);
        }
}
