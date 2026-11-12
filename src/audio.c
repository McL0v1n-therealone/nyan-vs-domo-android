/* Audio: everything is synthesized here, no sample files.
 *  - sound effects (meow, lasers, explosions, spit, fish, power-up, bomb, alarm, UI)
 *  - a tiny chiptune sequencer (pulse lead, triangle bass, noise drums) playing
 *    two original tracks: the main theme and the King Domo boss theme. */
#include "nyan.h"

#define RATE AUDIO_RATE
#define MEOW_LEN (RATE * 7 / 10)      /* 0.7 s */

volatile int meow_pos = -1, meow_req, boom_req, laser_on, spit_req, elaser_on;
volatile int coin_req, power_req, bomb_req, alarm_req, click_req, buzz_req;
volatile int music_track, music_duck;

static short meow[MEOW_LEN];

#define SINE_N 1024
static float sine_lut[SINE_N];
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

/* =====================================================================
 * Music
 * ===================================================================== */
#define MAXSTEPS 128
typedef struct {
    int bpm, len;
    signed char lead[MAXSTEPS], bass[MAXSTEPS];   /* MIDI note, 0 = hold, -1 = rest */
    char drum[MAXSTEPS];                          /* 'K' kick, 'S' snare, 'h' hat */
} Song;
static Song songs[3];
static float midi_hz[128];

/* "C5 . E5 - G#4" : note names, '.' = hold, '-' = rest. Returns step count. */
static int parse_notes(signed char *dst, const char *s) {
    int n = 0;
    while (*s && n < MAXSTEPS) {
        while (*s == ' ' || *s == '|' || *s == '\n') s++;
        if (!*s) break;
        if (*s == '.') { dst[n++] = 0; s++; continue; }
        if (*s == '-') { dst[n++] = -1; s++; continue; }
        static const int base[7] = {9, 11, 0, 2, 4, 5, 7};   /* A B C D E F G */
        int note = base[*s - 'A'];
        s++;
        if (*s == '#') { note++; s++; }
        else if (*s == 'b') { note--; s++; }
        int oct = *s++ - '0';
        dst[n++] = (signed char)(12 * (oct + 1) + note);
    }
    return n;
}
static void parse_drums(char *dst, const char *s) {
    int n = 0;
    for (; *s && n < MAXSTEPS; s++)
        if (*s == 'K' || *s == 'S' || *s == 'h' || *s == '.') dst[n++] = *s;
}

static void songs_init(void) {
    for (int i = 0; i < 128; i++) midi_hz[i] = 440.f * powf(2.f, (i - 69) / 12.f);

    /* main theme: C - Am - F - G, 150 BPM, 8 bars of 16ths */
    Song *m = &songs[1];
    m->bpm = 150;
    m->len = parse_notes(m->lead,
        "E5 . G5 . C6 . G5 . E5 . G5 . A5 G5 E5 . |"
        "A5 . C6 . E6 . C6 . A5 . G5 . E5 . C5 . |"
        "F5 . A5 . C6 . A5 . F5 . A5 . C6 . D6 . |"
        "B5 . . . G5 . . . D6 . C6 . B5 . G5 . |"
        "C6 . . G5 E5 . G5 . C6 . E6 . D6 C6 B5 . |"
        "A5 . . E5 C5 . E5 . A5 . C6 . B5 A5 G5 . |"
        "F5 . A5 C6 F6 . E6 . D6 . C6 . A5 . C6 . |"
        "D6 . . . B5 . . . G5 . A5 . B5 . D6 . |");
    parse_notes(m->bass,
        "C3 . C4 . G3 . C4 . C3 . C4 . G3 . C4 . |"
        "A2 . A3 . E3 . A3 . A2 . A3 . E3 . A3 . |"
        "F2 . F3 . C3 . F3 . F2 . F3 . C3 . F3 . |"
        "G2 . G3 . D3 . G3 . G2 . G3 . D3 . G3 . |"
        "C3 . C4 . G3 . C4 . C3 . C4 . G3 . C4 . |"
        "A2 . A3 . E3 . A3 . A2 . A3 . E3 . A3 . |"
        "F2 . F3 . C3 . F3 . F2 . F3 . C3 . F3 . |"
        "G2 . G3 . D3 . G3 . G2 G2 G3 . D3 . B2 . |");
    for (int b = 0; b < 8; b++)
        parse_drums(m->drum + b * 16, b == 7 ? "K.h.S.h.K.S.S.SS" : "K.h.S.h.K.K.S.hh");

    /* boss theme: Am - F - G - E, 172 BPM, driving */
    Song *bs = &songs[2];
    bs->bpm = 172;
    bs->len = parse_notes(bs->lead,
        "A5 . A5 C6 . A5 E6 . D6 . C6 . B5 . C6 . |"
        "F5 . F5 A5 . F5 C6 . B5 . A5 . G5 . A5 . |"
        "G5 . G5 B5 . G5 D6 . C6 . B5 . A5 . B5 . |"
        "E5 . G#5 . B5 . E6 . D6 . B5 . G#5 . E5 . |"
        "A5 . . . E6 . . . D6 . C6 . D6 . E6 . |"
        "F6 . . . E6 . D6 . C6 . . . A5 . . . |"
        "G5 . B5 . D6 . G6 . F6 . D6 . B5 . G5 . |"
        "G#5 . . . B5 . . . E6 . . . G#6 . . . |");
    static const char *bb[4] = {"A2 A2 A3 A2 ", "F2 F2 F3 F2 ", "G2 G2 G3 G2 ", "E2 E2 E3 E2 "};
    char tmp[1024] = "";
    for (int rep = 0; rep < 2; rep++)
        for (int c = 0; c < 4; c++)
            for (int k = 0; k < 4; k++) strcat(tmp, bb[c]);
    parse_notes(bs->bass, tmp);
    for (int b = 0; b < 8; b++)
        parse_drums(bs->drum + b * 16, b == 7 ? "KhShKhShKKSSKSSS" : "KhShKKShKhShKKSh");
}

/* =====================================================================
 * Mixer: called from the audio thread for every buffer
 * ===================================================================== */
void audio_setup(void) {
    for (int i = 0; i < SINE_N; i++) sine_lut[i] = sinf(2.f * (float)M_PI * i / SINE_N);
    synth_meow();
    songs_init();
}

static inline float sq(float ph, float duty) { return ph < duty ? 1.f : -1.f; }

void game_audio(short *out, int frames) {
    /* sound-effect state */
    static float lphase, lfo, lvol, bphase, ephase, elfo, evol, benv;
    static int boom_t = -1, spit_t = -1, coin_t = -1, power_t = -1, bomb_t = -1, alarm_t = -1;
    static int click_t = -1, buzz_t = -1;
    static float sfx_ph, bomb_ph, bomb_env, bomb_lp;
    static unsigned nseed = 1;
    /* music state */
    static int cur_song = 0, step, step_samp;
    static float mvol, lead_ph, lead_f, lead_env, bass_ph, bass_f, bass_env;
    static float kick_env, kick_fenv, kick_ph, snare_env, hat_env, prev_noise;
    static const float boom_decay = 0.999796f;           /* expf(-9/RATE) */

    int sfx = sv.sfx, mus = sv.music;
    if (meow_req) { meow_req = 0; meow_pos = 0; }
    if (boom_req) { boom_req = 0; boom_t = 0; benv = 1.f; }
    if (spit_req) { spit_req = 0; spit_t = 0; }
    if (coin_req) { coin_req = 0; coin_t = 0; }
    if (power_req) { power_req = 0; power_t = 0; }
    if (bomb_req) { bomb_req = 0; bomb_t = 0; bomb_env = 1.f; }
    if (alarm_req) { alarm_req = 0; alarm_t = 0; }
    if (click_req) { click_req = 0; click_t = 0; }
    if (buzz_req) { buzz_req = 0; buzz_t = 0; }

    int want = mus ? music_track : 0;
    float mtarget = want ? (music_duck ? 0.3f : 1.f) : 0.f;
    int pos = meow_pos;

    for (int i = 0; i < frames; i++) {
        float s = 0.f;          /* sound effects */
        if (pos >= 0 && pos < MEOW_LEN) s += meow[pos++];
        lvol += ((laser_on ? 1.f : 0.f) - lvol) * 0.002f;
        if (lvol > 0.001f) {
            lfo += 30.f / RATE; if (lfo >= 1.f) lfo -= 1.f;
            lphase += (300.f + 120.f * fsin01(lfo)) / RATE; if (lphase >= 1.f) lphase -= 1.f;
            s += (lphase * 2.f - 1.f) * 2600.f * lvol;
        }
        evol += ((elaser_on ? 1.f : 0.f) - evol) * 0.003f;
        if (evol > 0.001f) {
            elfo += 55.f / RATE; if (elfo >= 1.f) elfo -= 1.f;
            ephase += (130.f + 40.f * fsin01(elfo)) / RATE; if (ephase >= 1.f) ephase -= 1.f;
            s += sq(ephase, 0.5f) * 2200.f * evol;
        }
        nseed = nseed * 1103515245u + 12345u;
        float noise = (float)((nseed >> 16) & 0xFFFF) / 32768.f - 1.f;
        if (boom_t >= 0) {
            float t = (float)boom_t / RATE;
            if (t > 0.45f) boom_t = -1;
            else {
                bphase += (150.f - 220.f * t) / RATE; if (bphase >= 1.f) bphase -= 1.f;
                benv *= boom_decay;
                s += (noise * 0.6f + fsin01(bphase) * 0.8f) * benv * 9000.f;
                boom_t++;
            }
        }
        if (spit_t >= 0) {
            float t = (float)spit_t / RATE;
            if (t > 0.15f) spit_t = -1;
            else {
                float fq = 500.f - 2000.f * t;
                s += ((int)(spit_t * fq / RATE * 2.f) & 1 ? 2000.f : -2000.f) * (1.f - t / 0.15f);
                spit_t++;
            }
        }
        if (coin_t >= 0) {      /* fish: two quick rising blips */
            float t = (float)coin_t / RATE;
            if (t > 0.11f) coin_t = -1;
            else {
                sfx_ph += (t < 0.04f ? 1318.5f : 1760.f) / RATE; if (sfx_ph >= 1.f) sfx_ph -= 1.f;
                s += sq(sfx_ph, 0.5f) * 1300.f * (1.f - t / 0.11f);
                coin_t++;
            }
        }
        if (power_t >= 0) {     /* power-up: rising arpeggio */
            float t = (float)power_t / RATE;
            if (t > 0.32f) power_t = -1;
            else {
                static const float arp[4] = {1046.5f, 1318.5f, 1568.f, 2093.f};
                sfx_ph += arp[(int)(t / 0.08f) & 3] / RATE; if (sfx_ph >= 1.f) sfx_ph -= 1.f;
                s += sq(sfx_ph, 0.25f) * 1600.f * (1.f - t / 0.4f);
                power_t++;
            }
        }
        if (bomb_t >= 0) {      /* rainbow bomb: big whoosh + rumble */
            float t = (float)bomb_t / RATE;
            if (t > 1.1f) bomb_t = -1;
            else {
                bomb_lp += (noise - bomb_lp) * (0.05f + 0.3f * (1.f - t));
                bomb_ph += (90.f - 60.f * t) / RATE; if (bomb_ph >= 1.f) bomb_ph -= 1.f;
                bomb_env *= 0.99995f;
                s += (bomb_lp * 1.2f + fsin01(bomb_ph) * 0.9f) * bomb_env * 9000.f;
                bomb_t++;
            }
        }
        if (alarm_t >= 0) {     /* boss warning siren */
            float t = (float)alarm_t / RATE;
            if (t > 2.2f) alarm_t = -1;
            else {
                sfx_ph += (((int)(t / 0.18f) & 1) ? 660.f : 880.f) / RATE; if (sfx_ph >= 1.f) sfx_ph -= 1.f;
                s += sq(sfx_ph, 0.5f) * 1800.f;
                alarm_t++;
            }
        }
        if (click_t >= 0) {
            float t = (float)click_t / RATE;
            if (t > 0.03f) click_t = -1;
            else { s += sq(fmodf(t * 1500.f, 1.f), 0.5f) * 1400.f * (1.f - t / 0.03f); click_t++; }
        }
        if (buzz_t >= 0) {      /* "can't do that" */
            float t = (float)buzz_t / RATE;
            if (t > 0.18f) buzz_t = -1;
            else { s += sq(fmodf(t * 110.f, 1.f), 0.5f) * 2000.f; buzz_t++; }
        }
        if (!sfx) s = 0.f;

        /* ---------------- music ---------------- */
        mvol += ((cur_song == want ? mtarget : 0.f) - mvol) * 0.0005f;
        if (cur_song != want && mvol < 0.01f) {   /* switch tracks once faded out */
            cur_song = want; step = 0; step_samp = 0;
            lead_env = bass_env = 0.f;
        }
        if (cur_song) {
            const Song *sg = &songs[cur_song];
            if (step_samp == 0) {
                int n = sg->lead[step];
                if (n > 0) { lead_f = midi_hz[n]; lead_env = 1.f; } else if (n < 0) lead_env = 0.f;
                n = sg->bass[step];
                if (n > 0) { bass_f = midi_hz[n]; bass_env = 1.f; } else if (n < 0) bass_env = 0.f;
                switch (sg->drum[step]) {
                case 'K': kick_env = 1.f; kick_fenv = 1.f; break;
                case 'S': snare_env = 1.f; break;
                case 'h': hat_env = 1.f; break;
                }
            }
            if (++step_samp >= RATE * 15 / sg->bpm) { step_samp = 0; if (++step >= sg->len) step = 0; }

            float m = 0.f;
            if (lead_env > 0.002f) {
                lead_ph += lead_f / RATE; if (lead_ph >= 1.f) lead_ph -= 1.f;
                m += sq(lead_ph, 0.25f) * 1300.f * (0.35f + 0.65f * lead_env);
                lead_env *= 0.99993f;
            }
            if (bass_env > 0.002f) {
                bass_ph += bass_f / RATE; if (bass_ph >= 1.f) bass_ph -= 1.f;
                m += (4.f * fabsf(bass_ph - 0.5f) - 1.f) * 2600.f * bass_env;
                bass_env *= 0.99990f;
            }
            if (kick_env > 0.002f) {
                kick_ph += (45.f + 120.f * kick_fenv) / RATE; if (kick_ph >= 1.f) kick_ph -= 1.f;
                m += fsin01(kick_ph) * 3200.f * kick_env;
                kick_env *= 0.99983f; kick_fenv *= 0.9993f;
            }
            if (snare_env > 0.002f) { m += noise * 1300.f * snare_env; snare_env *= 0.99962f; }
            if (hat_env > 0.002f) { m += (noise - prev_noise) * 450.f * hat_env; hat_env *= 0.9982f; }
            s += m * mvol * 0.75f;
        }
        prev_noise = noise;

        int v = (int)s;
        v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
        out[2 * i] = out[2 * i + 1] = (short)v;
    }
    if (pos >= 0) meow_pos = pos >= MEOW_LEN ? -1 : pos;
}
