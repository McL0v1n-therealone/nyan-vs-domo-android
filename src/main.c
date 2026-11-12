/* Android platform layer: NativeActivity (no Java), EGL + GLES2 to show the
 * game's 480x272 software framebuffer scaled to the screen, OpenSL ES audio,
 * touch + BACK/MENU keys. Works on Android 4.4 (API 19) and newer. */
#include <android_native_app_glue.h>
#include <android/log.h>
#include <android/window.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <time.h>
#include <string.h>
#include "game.h"

#define LOG(...) __android_log_print(ANDROID_LOG_INFO, "NyanVsDomo", __VA_ARGS__)
#define TEX 512                       /* texture is 512x512, game uses 480x272 of it */

/* =====================================================================
 * Audio (OpenSL ES buffer queue, the game synthesizes into it)
 * ===================================================================== */
#define AUDIO_FRAMES 1024
static SLObjectItf sl_engine_obj, sl_mix_obj, sl_player_obj;
static SLPlayItf sl_play;
static SLAndroidSimpleBufferQueueItf sl_queue;
static short audio_buf[2][AUDIO_FRAMES * 2];
static int audio_idx;

static void audio_cb(SLAndroidSimpleBufferQueueItf q, void *ctx) {
    short *b = audio_buf[audio_idx];
    audio_idx ^= 1;
    game_audio(b, AUDIO_FRAMES);
    (*q)->Enqueue(q, b, sizeof audio_buf[0]);
}

static int audio_init(void) {
    SLEngineItf eng;
    if (slCreateEngine(&sl_engine_obj, 0, NULL, 0, NULL, NULL) != SL_RESULT_SUCCESS) return 0;
    (*sl_engine_obj)->Realize(sl_engine_obj, SL_BOOLEAN_FALSE);
    (*sl_engine_obj)->GetInterface(sl_engine_obj, SL_IID_ENGINE, &eng);
    if ((*eng)->CreateOutputMix(eng, &sl_mix_obj, 0, NULL, NULL) != SL_RESULT_SUCCESS) return 0;
    (*sl_mix_obj)->Realize(sl_mix_obj, SL_BOOLEAN_FALSE);

    SLDataLocator_AndroidSimpleBufferQueue loc_q = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 2};
    SLDataFormat_PCM fmt = {SL_DATAFORMAT_PCM, 2, SL_SAMPLINGRATE_44_1,
                            SL_PCMSAMPLEFORMAT_FIXED_16, SL_PCMSAMPLEFORMAT_FIXED_16,
                            SL_SPEAKER_FRONT_LEFT | SL_SPEAKER_FRONT_RIGHT, SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource src = {&loc_q, &fmt};
    SLDataLocator_OutputMix loc_mix = {SL_DATALOCATOR_OUTPUTMIX, sl_mix_obj};
    SLDataSink sink = {&loc_mix, NULL};
    const SLInterfaceID ids[1] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean req[1] = {SL_BOOLEAN_TRUE};
    if ((*eng)->CreateAudioPlayer(eng, &sl_player_obj, &src, &sink, 1, ids, req) != SL_RESULT_SUCCESS)
        return 0;
    (*sl_player_obj)->Realize(sl_player_obj, SL_BOOLEAN_FALSE);
    (*sl_player_obj)->GetInterface(sl_player_obj, SL_IID_PLAY, &sl_play);
    (*sl_player_obj)->GetInterface(sl_player_obj, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &sl_queue);
    (*sl_queue)->RegisterCallback(sl_queue, audio_cb, NULL);
    audio_cb(sl_queue, NULL);         /* prime both buffers */
    audio_cb(sl_queue, NULL);
    return 1;
}

static void audio_set_playing(int on) {
    if (sl_play)
        (*sl_play)->SetPlayState(sl_play, on ? SL_PLAYSTATE_PLAYING : SL_PLAYSTATE_PAUSED);
}

/* =====================================================================
 * Display (EGL + GLES2)
 * ===================================================================== */
static EGLDisplay egl_dpy = EGL_NO_DISPLAY;
static EGLSurface egl_surf = EGL_NO_SURFACE;
static EGLContext egl_ctx = EGL_NO_CONTEXT;
static int scr_w, scr_h;
static float view_x, view_y, view_scale;   /* letterboxed game area on screen */
static GLuint tex, prog;

static const char *VS =
    "attribute vec2 pos; attribute vec2 uv; varying vec2 v_uv;\n"
    "void main() { v_uv = uv; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char *FS =
    "precision mediump float; varying vec2 v_uv; uniform sampler2D tex;\n"
    "void main() { gl_FragColor = texture2D(tex, v_uv); }\n";

static GLuint shader(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    return s;
}

static int display_init(ANativeWindow *win) {
    egl_dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(egl_dpy, NULL, NULL);
    const EGLint attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                           EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(egl_dpy, attr, &cfg, 1, &n) || n < 1) { LOG("no EGL config"); return 0; }
    EGLint fmt;
    eglGetConfigAttrib(egl_dpy, cfg, EGL_NATIVE_VISUAL_ID, &fmt);
    ANativeWindow_setBuffersGeometry(win, 0, 0, fmt);
    egl_surf = eglCreateWindowSurface(egl_dpy, cfg, win, NULL);
    const EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    egl_ctx = eglCreateContext(egl_dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    if (!eglMakeCurrent(egl_dpy, egl_surf, egl_surf, egl_ctx)) { LOG("eglMakeCurrent failed"); return 0; }
    eglSwapInterval(egl_dpy, 1);

    prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, VS));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, FS));
    glBindAttribLocation(prog, 0, "pos");
    glBindAttribLocation(prog, 1, "uv");
    glLinkProgram(prog);
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX, TEX, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);   /* crisp pixels */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    scr_w = scr_h = 0;
    return 1;
}

static void display_term(void) {
    if (egl_dpy != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_ctx != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy, egl_ctx);
        if (egl_surf != EGL_NO_SURFACE) eglDestroySurface(egl_dpy, egl_surf);
        eglTerminate(egl_dpy);
    }
    egl_dpy = EGL_NO_DISPLAY; egl_surf = EGL_NO_SURFACE; egl_ctx = EGL_NO_CONTEXT;
}

static void display_present(void) {
    EGLint w, h;
    eglQuerySurface(egl_dpy, egl_surf, EGL_WIDTH, &w);
    eglQuerySurface(egl_dpy, egl_surf, EGL_HEIGHT, &h);
    if (w != scr_w || h != scr_h) {   /* fit 480x272 into the screen, keep aspect */
        scr_w = w; scr_h = h;
        float sx = (float)w / GAME_W, sy = (float)h / GAME_H;
        view_scale = sx < sy ? sx : sy;
        view_x = (w - GAME_W * view_scale) / 2;
        view_y = (h - GAME_H * view_scale) / 2;
    }
    glViewport(0, 0, w, h);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glViewport((int)view_x, (int)view_y, (int)(GAME_W * view_scale + 0.5f), (int)(GAME_H * view_scale + 0.5f));
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, GAME_STRIDE, GAME_H, GL_RGBA, GL_UNSIGNED_BYTE, game_fb);
    const float u = (float)GAME_W / TEX, v = (float)GAME_H / TEX;
    const GLfloat quad[] = {   /* x, y, u, v  (framebuffer row 0 = top of screen) */
        -1, -1, 0, v,   1, -1, u, v,   -1, 1, 0, 0,   1, 1, u, 0,
    };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, quad);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, quad + 2);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    eglSwapBuffers(egl_dpy, egl_surf);
}

/* =====================================================================
 * Input + lifecycle
 * ===================================================================== */
static int focused, has_window;

static void to_game(float px, float py, float *gx, float *gy) {
    float s = view_scale > 0 ? view_scale : 1;
    *gx = (px - view_x) / s;
    *gy = (py - view_y) / s;
}

static int32_t on_input(struct android_app *app, AInputEvent *ev) {
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t a = AMotionEvent_getAction(ev);
        int act = a & AMOTION_EVENT_ACTION_MASK;
        int idx = (a & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
        float x, y;
        switch (act) {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            to_game(AMotionEvent_getX(ev, idx), AMotionEvent_getY(ev, idx), &x, &y);
            game_touch_down(AMotionEvent_getPointerId(ev, idx), x, y);
            break;
        case AMOTION_EVENT_ACTION_MOVE:
            for (size_t i = 0; i < AMotionEvent_getPointerCount(ev); i++) {
                to_game(AMotionEvent_getX(ev, i), AMotionEvent_getY(ev, i), &x, &y);
                game_touch_move(AMotionEvent_getPointerId(ev, i), x, y);
            }
            break;
        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
            game_touch_up(AMotionEvent_getPointerId(ev, idx));
            break;
        case AMOTION_EVENT_ACTION_CANCEL:
            game_touch_cancel_all();
            break;
        }
        return 1;
    }
    if (AInputEvent_getType(ev) == AINPUT_EVENT_TYPE_KEY) {
        int32_t key = AKeyEvent_getKeyCode(ev);
        int up = AKeyEvent_getAction(ev) == AKEY_EVENT_ACTION_UP;
        if (key == AKEYCODE_BACK) {
            if (up && !game_back()) ANativeActivity_finish(app->activity);
            return 1;
        }
        if (key == AKEYCODE_MENU) {
            if (up) game_menu_key();
            return 1;
        }
    }
    return 0;
}

static void on_cmd(struct android_app *app, int32_t cmd) {
    switch (cmd) {
    case APP_CMD_INIT_WINDOW:
        if (app->window && display_init(app->window)) has_window = 1;
        break;
    case APP_CMD_TERM_WINDOW:
        has_window = 0;
        display_term();
        break;
    case APP_CMD_GAINED_FOCUS:
        focused = 1;
        audio_set_playing(1);
        break;
    case APP_CMD_LOST_FOCUS:
    case APP_CMD_PAUSE:
        focused = 0;
        game_suspend();
        audio_set_playing(0);
        break;
    }
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

void android_main(struct android_app *app) {
    app->onAppCmd = on_cmd;
    app->onInputEvent = on_input;
    ANativeActivity_setWindowFlags(app->activity,
        AWINDOW_FLAG_FULLSCREEN | AWINDOW_FLAG_KEEP_SCREEN_ON, 0);

    static int inited;
    if (!inited) {                    /* the .so can outlive the activity */
        game_init(app->activity->internalDataPath);
        if (!audio_init()) LOG("audio init failed");
        inited = 1;
    }

    const double STEP = 1000.0 / 60.0;
    double acc = 0, last = now_ms();
    for (;;) {
        int events;
        struct android_poll_source *src;
        int running = focused && has_window;
        while (ALooper_pollAll(running ? 0 : -1, NULL, &events, (void **)&src) >= 0) {
            if (src) src->process(app, src);
            if (app->destroyRequested) {
                game_suspend();
                display_term();
                return;
            }
            running = focused && has_window;
        }
        if (!running) { last = now_ms(); acc = 0; continue; }

        /* fixed 60 Hz game steps; the slack absorbs vsync jitter */
        double t = now_ms();
        acc += t - last;
        last = t;
        int steps = (int)((acc + 4.0) / STEP);
        if (steps > 4) { steps = 4; acc = 0; }
        else acc -= steps * STEP;
        if (acc < -8.0) acc = -8.0;
        for (int i = 0; i < steps; i++) game_frame();
        display_present();
    }
}
