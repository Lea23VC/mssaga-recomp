/* From SaintSeiyaRecomp port/gs/display.c (MIT, Copyright (c) 2026 Matheus Nunes Pereira;
 * licenses/SaintSeiyaRecomp-MIT.txt). Changes for MS Saga are noted where they are. */
/*
 * The picture: a window (SDL3) showing what the GS displays, or, with --shot, pictures saved at
 * given vertical blanks for tests (build/shot_NNNN.ppm), without a window.
 *
 * The game runs on its own host threads; this side only reads GS memory, once per vertical blank.
 */
#include <SDL3/SDL.h>
#ifdef __GLIBC__
#include <malloc.h>   /* mallinfo2: the heap in MSSAGA_PERF_LOG */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "r5900.h"
#include "runtime.h"
#include "kernel.h"
#include "gs.h"
#include "pad.h"
#include "perf.h"
#include "gpu.h"
#include "platform.h"
#include <sys/stat.h>

void gpu_set_overlay(const u32 *px, int w, int h);   /* gpu_vk.c */
void gpu_set_movie(const u32 *px, int w, int h);
int movie_take(u32 *out, int max, int *w, int *h, u32 *gen);   /* port/hle/movie.c */

int gs_display(u32 *out, int max_w, int max_h, int *w, int *h);
void gs_init(void);
void vif_stats(void);
void vu_stats(void);
extern u64 gs_prims;

static u32 frame[1024 * 1024];
static u32 render_frames;               /* pictures shown (GPU window), for the panel */

/* --press 600:CROSS,700:START,800:DOWN:20: press a button at those times for 6 vertical blanks, or for
   the given number (tests). LUP / LDOWN / LLEFT / LRIGHT push the left stick all the way. */
typedef struct { int at, len; u16 btn; s8 lx, ly; } Press;
static Press presses[4096];
static int npresses;

static const struct { const char *name; u16 bit; } button_names[] = {
    {"SELECT", PAD_SELECT}, {"START", PAD_START}, {"UP", PAD_UP}, {"RIGHT", PAD_RIGHT}, {"DOWN", PAD_DOWN},
    {"LEFT", PAD_LEFT}, {"L2", PAD_L2}, {"R2", PAD_R2}, {"L1", PAD_L1}, {"R1", PAD_R1},
    {"TRIANGLE", PAD_TRIANGLE}, {"CIRCLE", PAD_CIRCLE}, {"CROSS", PAD_CROSS}, {"SQUARE", PAD_SQUARE}};

void display_set_presses(const char *spec) {
    static char buf[1 << 16];
    snprintf(buf, sizeof buf, "%s", spec);
    for (char *tok = strtok(buf, ","); tok && npresses < (int)(sizeof presses / sizeof *presses); tok = strtok(NULL, ",")) {
        char *colon = strchr(tok, ':');
        if (!colon) continue;
        *colon = 0;
        char *len = strchr(colon + 1, ':');
        if (len) *len++ = 0;
        for (size_t i = 0; i < sizeof button_names / sizeof *button_names; i++)
            if (!strcmp(colon + 1, button_names[i].name))
                presses[npresses++] = (Press){atoi(tok), len ? atoi(len) : 6, button_names[i].bit, 0, 0};
        static const struct { const char *name; s8 lx, ly; } sticks[] = {
            {"LUP", 0, -1}, {"LDOWN", 0, 1}, {"LLEFT", -1, 0}, {"LRIGHT", 1, 0}};
        for (size_t i = 0; i < sizeof sticks / sizeof *sticks && npresses < (int)(sizeof presses / sizeof *presses); i++)
            if (!strcmp(colon + 1, sticks[i].name))
                presses[npresses++] = (Press){atoi(tok), len ? atoi(len) : 6, 0, sticks[i].lx, sticks[i].ly};
    }
}

/* the scripted left stick, if any is pushed now */
static void scripted_stick(u32 vb) {
    static int was;
    int lx = 0, ly = 0, any = 0;
    for (int i = 0; i < npresses; i++)
        if ((presses[i].lx || presses[i].ly) && (int)vb >= presses[i].at && (int)vb < presses[i].at + presses[i].len) {
            lx += presses[i].lx;
            ly += presses[i].ly;
            any = 1;
        }
    if (!any) {
        if (was) pad_sticks[0][2] = pad_sticks[0][3] = 0x80;        /* let go */
        was = 0;
        return;
    }
    was = 1;
    pad_sticks[0][2] = lx < 0 ? 0x00 : lx > 0 ? 0xFF : 0x80;
    pad_sticks[0][3] = ly < 0 ? 0x00 : ly > 0 ? 0xFF : 0x80;
}

static u16 scripted(u32 vb) {
    u16 b = 0;
    for (int i = 0; i < npresses; i++)
        if ((int)vb >= presses[i].at && (int)vb < presses[i].at + presses[i].len) b |= presses[i].btn;
    return b;
}

/* keyboard: arrows, Z = CROSS, X = CIRCLE, A = SQUARE, S = TRIANGLE, Q/W = L1/R1, E/R = L2/R2,
   Enter = START, Backspace = SELECT */
static u16 keyboard(void) {
    const bool *k = SDL_GetKeyboardState(NULL);
    u16 b = 0;
    if (k[SDL_SCANCODE_UP]) b |= PAD_UP;
    if (k[SDL_SCANCODE_DOWN]) b |= PAD_DOWN;
    if (k[SDL_SCANCODE_LEFT]) b |= PAD_LEFT;
    if (k[SDL_SCANCODE_RIGHT]) b |= PAD_RIGHT;
    if (k[SDL_SCANCODE_Z]) b |= PAD_CROSS;
    if (k[SDL_SCANCODE_X]) b |= PAD_CIRCLE;
    if (k[SDL_SCANCODE_A]) b |= PAD_SQUARE;
    if (k[SDL_SCANCODE_S]) b |= PAD_TRIANGLE;
    if (k[SDL_SCANCODE_Q]) b |= PAD_L1;
    if (k[SDL_SCANCODE_W]) b |= PAD_R1;
    if (k[SDL_SCANCODE_E]) b |= PAD_L2;
    if (k[SDL_SCANCODE_R]) b |= PAD_R2;
    if (k[SDL_SCANCODE_RETURN]) b |= PAD_START;
    if (k[SDL_SCANCODE_BACKSPACE]) b |= PAD_SELECT;
    return b;
}

/* Gamepads: the first plays port 1 (with the keyboard), the second port 2. */
static SDL_Gamepad *gamepads[2];

static void gamepad_event(const SDL_Event *e) {
    if (e->type == SDL_EVENT_GAMEPAD_ADDED) {
        for (int k = 0; k < 2; k++)
            if (!gamepads[k]) {
                gamepads[k] = SDL_OpenGamepad(e->gdevice.which);
                rt_log("pad", "gamepad %d: %s", k + 1, gamepads[k] ? SDL_GetGamepadName(gamepads[k]) : SDL_GetError());
                break;
            }
    } else if (e->type == SDL_EVENT_GAMEPAD_REMOVED) {
        for (int k = 0; k < 2; k++)
            if (gamepads[k] && SDL_GetGamepadID(gamepads[k]) == e->gdevice.which) {
                SDL_CloseGamepad(gamepads[k]);
                gamepads[k] = NULL;
                rt_log("pad", "gamepad %d removed", k + 1);
            }
    }
    pad_second = gamepads[1] != NULL;
}

static u16 gamepad_buttons(int k) {
    SDL_Gamepad *gamepad = gamepads[k];
    if (!gamepad) return 0;
    static const struct { SDL_GamepadButton b; u16 bit; } map[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, PAD_CROSS}, {SDL_GAMEPAD_BUTTON_EAST, PAD_CIRCLE},
        {SDL_GAMEPAD_BUTTON_WEST, PAD_SQUARE}, {SDL_GAMEPAD_BUTTON_NORTH, PAD_TRIANGLE},
        {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, PAD_L1}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, PAD_R1},
        {SDL_GAMEPAD_BUTTON_START, PAD_START}, {SDL_GAMEPAD_BUTTON_BACK, PAD_SELECT},
        {SDL_GAMEPAD_BUTTON_DPAD_UP, PAD_UP}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, PAD_DOWN},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, PAD_LEFT}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, PAD_RIGHT},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, PAD_L3}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, PAD_R3}};
    u16 b = 0;
    for (size_t i = 0; i < sizeof map / sizeof *map; i++)
        if (SDL_GetGamepadButton(gamepad, map[i].b)) b |= map[i].bit;
    if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) b |= PAD_L2;
    if (SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) b |= PAD_R2;
    static const SDL_GamepadAxis axes[4] = {SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY,
                                           SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY};
    for (int i = 0; i < 4; i++) pad_sticks[k][i] = (u8)((SDL_GetGamepadAxis(gamepad, axes[i]) + 32768) >> 8);
    return b;
}

int training_key(int scancode);                      /* port/hle/training.c */
u16 training_pad(u16 buttons);
int training_hud(SDL_Surface *surf);
void gpu_set_hud(const u32 *px, int w, int h);      /* gpu_vk.c */

static void poll_pads(void) {
    pad_buttons[0] = training_pad(keyboard() | gamepad_buttons(0) | scripted(kernel_vblank_count()));
    scripted_stick(kernel_vblank_count());
    pad_buttons[1] = gamepad_buttons(1);
}
static u32 crt_interlace, crt_mode, crt_field;

void gs_set_crt(u32 interlace, u32 mode, u32 field) {
    crt_interlace = interlace;
    crt_mode = mode;
    crt_field = field;
}

static void save_ppm(const char *path, const u32 *px, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        u8 rgb[3] = {(u8)px[i], (u8)(px[i] >> 8), (u8)(px[i] >> 16)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void panel_update(void);
static void hud_update(void);

/* Headless: save the picture at each vertical blank listed (ascending), then exit. */
/* GPU renderer, headless: take the frames sealed since the last call, show the newest */
static u64 ip_draws, ip_paired, ip_verts, ip_vpaired, ip_far, ip_frames;   /* MSSAGA_INTERP_LOG: how much pairs */

static GpuVertex *interpolate(const GpuFrame *a, const GpuFrame *b, float t);

static void gpu_vblank(void) {
    if (!gpu_on) return;
    static GpuFrame *last, *prev;
    gpu_vblank_tick();
    GpuFrame *f;
    static int ilog = -1;
    if (ilog < 0) ilog = getenv("MSSAGA_INTERP_LOG") != NULL;
    while ((f = gpu_next_frame())) {
        gpu_upload(f);
        if (ilog && last) {
            interpolate(last, f, 0.5f);
            if (ip_frames % 30 == 0) {
                rt_log("interp", "last 30 frames: %llu%% of draws paired (%llu of %llu a frame), %llu%% of vertices, %llu cut",
                       (unsigned long long)(ip_draws ? ip_paired * 100 / ip_draws : 0), (unsigned long long)ip_paired / 30,
                       (unsigned long long)ip_draws / 30, (unsigned long long)(ip_verts ? ip_vpaired * 100 / ip_verts : 0),
                       (unsigned long long)ip_far);
                ip_draws = ip_paired = ip_verts = ip_vpaired = ip_far = 0;
            }
        }
        gpu_release_frame(prev);
        prev = last;
        last = f;
    }
    /* MSSAGA_INTERP_SHOT=1: the picture is the one halfway from the frame before (to see blending's flaws) */
    static int ishot = -1;
    if (ishot < 0) ishot = getenv("MSSAGA_INTERP_SHOT") != NULL;
    if (last) gpu_render(last, ishot && prev ? interpolate(prev, last, 0.5f) : NULL);
}

/* Frames shown between the game's: each draw found in both the previous frame (a) and the newest
 * (b), with the same vertices and target, gets its positions moved a fraction t of the way from a
 * to b (colours and texture coordinates are b's); draws that differ, or move too far (a cut), are
 * b's. Returns the vertices to draw b with. */
static int same_draw(const GpuDraw *da, const GpuDraw *db) {
    return da->count == db->count && da->textured == db->textured && da->tex == db->tex && da->flat2d == db->flat2d;
}

static GpuVertex *interpolate(const GpuFrame *a, const GpuFrame *b, float t) {
    static GpuVertex *out;
    static u32 cap;
    if (b->nverts > cap) { cap = b->nverts * 2; out = realloc(out, cap * sizeof *out); }
    memcpy(out, b->verts, b->nverts * sizeof *out);
    ip_frames++;
    ip_draws += b->ndraws;
    ip_verts += b->nverts;
    /* MS Saga: draws are paired in order, but a draw of b with no partner in a (an object come into view, or
       gone) no longer shifts every later pair: the next draw of a like it, up to LOOK draws ahead, is its
       partner. The target buffer is not compared: the game draws its frames into two buffers in turn. */
    enum { LOOK = 24, GW = 16, GH = 14 };
    static float fx[GH][GW], fy[GH][GW], fw[GH][GW];   /* motion b - a by screen area, from the pairs */
    static u32 *lone;                                   /* unpaired 3D draws of b */
    static u32 lone_cap;
    u32 nlone = 0;
    if (b->ndraws > lone_cap) { lone_cap = b->ndraws * 2; lone = realloc(lone, lone_cap * sizeof *lone); }
    memset(fx, 0, sizeof fx);
    memset(fy, 0, sizeof fy);
    memset(fw, 0, sizeof fw);
    u32 j = 0;
    for (u32 i = 0; i < b->ndraws && j < a->ndraws; i++) {
        const GpuDraw *db = &b->draws[i];
        u32 m = j, end = j + LOOK < a->ndraws ? j + LOOK : a->ndraws;
        while (m < end && !same_draw(&a->draws[m], db)) m++;
        if (m == end) {                             /* new in b: shown as it is */
            if (getenv("MSSAGA_INTERP_LOG")) {      /* why: the same texture nearby, other vertex counts? */
                static u64 lone, other_count, lone_v, oc_v;
                lone++;
                lone_v += db->count;
                for (u32 q = j; q < end; q++)
                    if (a->draws[q].tex == db->tex && a->draws[q].count != db->count) { other_count++; oc_v += db->count; break; }
                if (lone % 2000 == 0)
                    rt_log("interp", "unpaired draws: %llu%% have the same texture nearby with another vertex count (%llu%% of their vertices)",
                           (unsigned long long)(other_count * 100 / lone), (unsigned long long)(oc_v * 100 / (lone_v ? lone_v : 1)));
            }
            if (!db->flat2d) lone[nlone++] = i;
            continue;
        }
        j = m + 1;
        const GpuDraw *da = &a->draws[m];
        const GpuVertex *va = &a->verts[da->first], *vb = &b->verts[db->first];
        /* a real partner moves as one: every vertex about as the draw's mean (an unrelated draw, or the same
           one with its triangles in another order, doesn't); and not far (a cut) */
        float mx = 0, my = 0;
        for (u32 k = 0; k < db->count; k++) { mx += vb[k].x - va[k].x; my += vb[k].y - va[k].y; }
        mx /= (float)db->count;
        my /= (float)db->count;
        int far = fabsf(mx) > 40.0f || fabsf(my) > 40.0f;
        for (u32 k = 0; k < db->count && !far; k++)
            far = fabsf(vb[k].x - va[k].x - mx) > 6.0f || fabsf(vb[k].y - va[k].y - my) > 6.0f;
        if (far) { ip_far++; if (!db->flat2d) lone[nlone++] = i; continue; }
        if (!db->flat2d)
            for (u32 k = 0; k < db->count; k++) {
                int gx = (int)(vb[k].x * GW / 512.0f), gy = (int)(vb[k].y * GH / 448.0f);
                if (gx < 0 || gy < 0 || gx >= GW || gy >= GH) continue;
                fx[gy][gx] += vb[k].x - va[k].x;
                fy[gy][gx] += vb[k].y - va[k].y;
                fw[gy][gx] += 1.0f;
            }
        ip_paired++;
        ip_vpaired += db->count;
        GpuVertex *o = &out[db->first];
        for (u32 k = 0; k < db->count; k++) {
            o[k].x = va[k].x + (vb[k].x - va[k].x) * t;
            o[k].y = va[k].y + (vb[k].y - va[k].y) * t;
            o[k].z = va[k].z + (vb[k].z - va[k].z) * t;
        }
    }
    /* the unpaired 3D (ground and walls cut at the screen's edge into other triangles each frame, objects come
       into view): moved back by the motion around them, so that they stay with their neighbours. Areas with no
       pair take the mean of all. */
    float tx = 0, ty = 0, tw = 0;
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++) { tx += fx[y][x]; ty += fy[y][x]; tw += fw[y][x]; }
    if (tw > 0 && nlone) {
        tx /= tw;
        ty /= tw;
        for (u32 n = 0; n < nlone; n++) {
            const GpuDraw *db = &b->draws[lone[n]];
            const GpuVertex *vb = &b->verts[db->first];
            GpuVertex *o = &out[db->first];
            for (u32 k = 0; k < db->count; k++) {
                int gx = (int)(vb[k].x * GW / 512.0f), gy = (int)(vb[k].y * GH / 448.0f);
                gx = gx < 0 ? 0 : gx >= GW ? GW - 1 : gx;
                gy = gy < 0 ? 0 : gy >= GH ? GH - 1 : gy;
                float dx = tx, dy = ty, w = 0, sx = 0, sy = 0;
                for (int yy = gy - 1; yy <= gy + 1; yy++)       /* this area and the ones around it */
                    for (int xx = gx - 1; xx <= gx + 1; xx++)
                        if (yy >= 0 && xx >= 0 && yy < GH && xx < GW) { sx += fx[yy][xx]; sy += fy[yy][xx]; w += fw[yy][xx]; }
                if (w > 0) { dx = sx / w; dy = sy / w; }
                o[k].x = vb[k].x - dx * (1.0f - t);
                o[k].y = vb[k].y - dy * (1.0f - t);
            }
        }
    }
    return out;
}

/* The opening video (port/hle/movie.c): its newest picture to the GPU; 1 when what is shown changed. */
static int movie_update(void) {
    static u32 px[1024 * 1024], gen;
    static int on;
    int w, h;
    u32 before = gen;
    int now = movie_take(px, 1024 * 1024, &w, &h, &gen);
    if (now && gen != before) { gpu_set_movie(px, w, h); on = 1; return 1; }
    if (!now && on) { gpu_set_movie(NULL, 0, 0); on = 0; gen = 0; return 1; }
    return 0;
}

int display_shots(const int *at, int n) {
    gs_init();
    if (gpu_on && (!SDL_Init(SDL_INIT_VIDEO) || gpu_init(NULL))) gpu_on = 0;
    for (int k = 0; k < n; k++) {
        u32 seen = 0;
        while ((int)kernel_vblank_count() < at[k]) {
            pad_buttons[0] = training_pad(scripted(kernel_vblank_count()));
            scripted_stick(kernel_vblank_count());
            if (kernel_vblank_count() != seen) { seen = kernel_vblank_count(); panel_update(); hud_update(); gpu_vblank(); }
            usleep(2000);
        }
        int w, h;
        gpu_vblank();
        if (gpu_on) movie_update();
        static u32 *full;
        u32 *px = frame;
        int pw, ph;
        if (gpu_on && getenv("MSSAGA_SHOT_PRESENT") && sscanf(getenv("MSSAGA_SHOT_PRESENT"), "%dx%d", &pw, &ph) == 2
            && pw > 0 && ph > 0 && pw <= 4096 && ph <= 4096) {
            if (!full) full = malloc(4096 * 4096 * 4);
            px = full;
            gpu_read_presented(px, pw, ph);
            w = pw;
            h = ph;
        } else if (gpu_on && getenv("MSSAGA_SHOT_FULL")) {
            if (!full) full = malloc(4096 * 4096 * 4);
            px = full;
            gpu_read_display(px, 4096, 4096, &w, &h);
        } else if (gpu_on) gpu_read_display(frame, 1024, 1024, &w, &h);
        else gs_display(frame, 1024, 1024, &w, &h);
        u32 movie_gen = 0;
        int mw, mh;
        if (px != full && movie_take(frame, 1024 * 1024, &mw, &mh, &movie_gen)) { px = frame; w = mw; h = mh; }   /* the opening video */
        char path[64];
        snprintf(path, sizeof path, "build/shot_%04d.ppm", at[k]);
        save_ppm(path, px, w, h);
        if (getenv("MSSAGA_VRAM")) {
            void gs_dump_vram(u32 *out);
            gs_dump_vram(frame);
            snprintf(path, sizeof path, "build/vram_%04d.ppm", at[k]);
            save_ppm(path, frame, 1024, 1024);
            rt_log("gs", "PMODE %llX DISPFB1 %llX DISPLAY1 %llX DISPFB2 %llX DISPLAY2 %llX",
                   (unsigned long long)gs_priv_read(0), (unsigned long long)gs_priv_read(0x70),
                   (unsigned long long)gs_priv_read(0x80), (unsigned long long)gs_priv_read(0x90),
                   (unsigned long long)gs_priv_read(0xA0));
        }
        if (getenv("MSSAGA_RAM_DUMP") && atoi(getenv("MSSAGA_RAM_DUMP"))) {        /* the EE's 32 MB at the same moment, for finding the game's variables */
            snprintf(path, sizeof path, "build/ram_%04d.bin", at[k]);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(rt_ram, 1, 32u << 20, f); fclose(f); }
        }
        rt_log("gs", "%s (%dx%d), %llu primitives so far", path, w, h, (unsigned long long)gs_prims);
    }
    vif_stats();
    vu_stats();
    void prof_report(void);
    prof_report();
    return 0;
}

/* ---- performance panel (F3): refreshed once a second */
typedef struct { char line[4][96]; } Panel;
/* Window size: 1920 x 1080 unless MSSAGA_WINDOW=WxH or =full says otherwise, or F1 chose another
   (kept in ~/.local/share/mssaga/window). F1 goes through the sizes that fit the screen, then full
   screen. w 0: full screen. */
typedef struct { int w, h; } WinSize;
static const WinSize win_sizes[] = {{1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3840, 2160}, {0, 0}};
static WinSize win_size = {1920, 1080};

/* ~/.local/share/mssaga/<name>: settings chosen with the F keys */
void display_config_path(char *p, size_t n, const char *name) {
    platform_data_dir(p, n);
    snprintf(p + strlen(p), n - strlen(p), "/%s", name);
}

static int win_size_parse(const char *s, WinSize *ws) {
    int w, h;
    if (!strncmp(s, "full", 4)) { *ws = (WinSize){0, 0}; return 1; }
    if (sscanf(s, "%dx%d", &w, &h) == 2 && w >= 320 && h >= 240 && w <= 16384 && h <= 16384) { *ws = (WinSize){w, h}; return 1; }
    return 0;
}

static void win_size_load(void) {
    const char *e = getenv("MSSAGA_WINDOW");
    if (e && win_size_parse(e, &win_size)) return;
    char path[1100], buf[32] = "";
    display_config_path(path, sizeof path, "window");
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(buf, sizeof buf, f)) win_size_parse(buf, &win_size);
    fclose(f);
}

static void win_size_apply(SDL_Window *win) {
    if (!win_size.w) { SDL_SetWindowFullscreen(win, true); return; }
    SDL_SetWindowFullscreen(win, false);
    SDL_SetWindowSize(win, win_size.w, win_size.h);
    SDL_SetWindowPosition(win, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

/* F1: the next size up that fits the screen, then full screen, then the smallest again. */
static void win_size_next(SDL_Window *win) {
    const SDL_DisplayMode *m = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(win));
    int n = sizeof win_sizes / sizeof *win_sizes, i;
    long cur = win_size.w ? (long)win_size.w * win_size.h : -1;
    if (cur < 0) i = 0;
    else {
        for (i = 0; i < n - 1; i++)
            if ((long)win_sizes[i].w * win_sizes[i].h > cur) break;
    }
    while (i < n - 1 && m && (win_sizes[i].w > m->w || win_sizes[i].h > m->h)) i = n - 1;
    win_size = win_sizes[i];
    win_size_apply(win);
    char path[1100];
    display_config_path(path, sizeof path, "window");
    FILE *f = fopen(path, "w");
    if (f) {
        if (win_size.w) fprintf(f, "%dx%d\n", win_size.w, win_size.h);
        else fprintf(f, "full\n");
        fclose(f);
    }
    if (win_size.w) rt_log("display", "window %dx%d", win_size.w, win_size.h);
    else rt_log("display", "full screen");
}

static SDL_Window *win_create(void) {
    win_size_load();
    SDL_Window *win = SDL_CreateWindow("MS Saga: A New Dawn", win_size.w ? win_size.w : 1920,
                                       win_size.w ? win_size.h : 1080, SDL_WINDOW_RESIZABLE);
    if (win && !win_size.w) SDL_SetWindowFullscreen(win, true);
    return win;
}

static Panel panel;
static int panel_on = 1;
/* (VU1 time includes the XGKICKs it makes, which gs_ns counts too) */

static double cpu_seconds(void) { return platform_cpu_seconds(); }

/* Called at every vertical blank shown. A game frame is counted when primitives were drawn since
   the previous vertical blank. */
volatile int perf_timing;

static void panel_update(void) {
    static int log = -1;
    if (log < 0) log = getenv("MSSAGA_PERF_LOG") != NULL;
    perf_timing = panel_on || log;
    static u64 t0, prims0, gs0, vu0, last_prims;
    static u32 vb0, frames;
    static double cpu0;
    if (perf.prims != last_prims) frames++;
    last_prims = perf.prims;
    u64 now = perf_now();
    if (!t0) { t0 = now; vb0 = kernel_vblank_count(); cpu0 = cpu_seconds(); return; }
    double dt = (now - t0) / 1e9;
    if (dt < 1.0) return;
    double cpu = cpu_seconds();
    double gs = (perf.gs_ns - gs0) / 1e9, vu = (perf.vu_ns - vu0) / 1e9, total = cpu - cpu0;
    double game = total - gs - vu;
    if (game < 0) game = 0;
    static u32 rf0, sealed0;
    u32 sealed = gpu_on ? gpu_frames_sealed() : 0;
    snprintf(panel.line[0], 96, "logic %4.1f fps  display %5.1f fps  vblank %4.1f Hz", gpu_on ? (sealed - sealed0) / dt : frames / dt,
             (render_frames - rf0) / dt, (kernel_vblank_count() - vb0) / dt);
    rf0 = render_frames;
    sealed0 = sealed;
    snprintf(panel.line[1], 96, "CPU %3.0f%%  GS %3.0f%%  VU1 %3.0f%%  game %3.0f%%", 100 * total / dt,
             100 * gs / dt, 100 * vu / dt, 100 * game / dt);
    extern u64 upload_skipped, upload_written;
    static u64 sk0, wr0;
    snprintf(panel.line[2], 96, "%.0f primitives/s  uploads %.0f/s (%.0f skipped)", (perf.prims - prims0) / dt,
             (upload_written - wr0 + upload_skipped - sk0) / dt, (upload_skipped - sk0) / dt);
    sk0 = upload_skipped;
    wr0 = upload_written;
    char size[24];
    if (win_size.w) snprintf(size, sizeof size, "%dx%d", win_size.w, win_size.h);
    else snprintf(size, sizeof size, "full screen");
    snprintf(panel.line[3], 96, "F1: %s  F2: %s  F3: hide  F4: vsync %s", size, widescreen_on() ? "16:9" : "4:3", gpu_on && gpu_vsync() ? "on" : "off");
    if (getenv("MSSAGA_PERF_LOG")) {
#ifdef __GLIBC__
        struct mallinfo2 mi = mallinfo2();
        fprintf(stderr, "[perf %5u] %s | %s | %s | heap %zu MB mmap %zu MB\n", kernel_vblank_count(), panel.line[0], panel.line[1],
                panel.line[2], mi.uordblks >> 20, mi.hblkhd >> 20);
#else
        fprintf(stderr, "[perf %5u] %s | %s | %s\n", kernel_vblank_count(), panel.line[0], panel.line[1], panel.line[2]);
#endif
    }
    t0 = now; prims0 = perf.prims; gs0 = perf.gs_ns; vu0 = perf.vu_ns; vb0 = kernel_vblank_count(); cpu0 = cpu;
    frames = 0;
}

static void panel_draw(SDL_Renderer *ren) {
    if (!panel_on || !panel.line[0][0]) return;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 170);
    SDL_FRect bg = {4, 4, 8 * 56 + 8, 4 * 10 + 8};
    SDL_RenderFillRect(ren, &bg);
    SDL_SetRenderDrawColor(ren, 255, 255, 0, 255);
    for (int i = 0; i < 4; i++) SDL_RenderDebugText(ren, 8, 8 + i * 10, panel.line[i]);
}

/* The training layer (port/hle/training.c), over the whole picture: 640 x 360, or 480 x 360 in 4:3. */
static void hud_update(void) {
    static SDL_Surface *surf[2];
    if (!gpu_on) return;
    int wide = widescreen_on();
    if (!surf[wide]) surf[wide] = SDL_CreateSurface(wide ? 640 : 480, 360, SDL_PIXELFORMAT_ABGR8888);
    static int was;
    if (surf[wide] && training_hud(surf[wide])) { gpu_set_hud(surf[wide]->pixels, surf[wide]->w, surf[wide]->h); was = 1; }
    else if (was) { gpu_set_hud(NULL, 0, 0); was = 0; }
}

/* Window: shows the displayed buffer at every vertical blank until it is closed. */
static void overlay_update(void) {
    static SDL_Surface *surf;
    static SDL_Renderer *sr;
    if (!panel_on) { gpu_set_overlay(NULL, 0, 0); return; }
    if (!surf) {
        surf = SDL_CreateSurface(8 * 56 + 8, 4 * 10 + 8, SDL_PIXELFORMAT_ABGR8888);
        sr = SDL_CreateSoftwareRenderer(surf);
    }
    SDL_SetRenderDrawColor(sr, 0, 0, 0, 170);
    SDL_RenderClear(sr);
    SDL_SetRenderDrawColor(sr, 255, 255, 0, 255);
    for (int i = 0; i < 4; i++) SDL_RenderDebugText(sr, 4, 4 + i * 10, panel.line[i]);
    SDL_RenderPresent(sr);
    gpu_set_overlay(surf->pixels, surf->w, surf->h);
}

/* Window with the GPU renderer: as fast as possible (or as often as the screen takes pictures
 * with vsync: MSSAGA_VSYNC=1 or F4), show the game's newest frame, moved part of the way from the one before
 * it by the time since it came: MSSAGA_INTERP=1; MS Saga: off by default, it still shows artifacts while moving). */
static int display_window_gpu(void) {
    SDL_Window *win = win_create();
    if (!win || gpu_init(win)) return -1;
    int interp = getenv("MSSAGA_INTERP") && atoi(getenv("MSSAGA_INTERP"));   /* MS Saga: off unless asked (artifacts) */
    GpuFrame *fa = NULL, *fb = NULL;    /* the frame before, the newest */
    u64 tb = 0, period = 20000000;      /* when the newest came; time between frames (ns) */
    u32 last_vb = 0;
    int final_shown = 0;
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) { SDL_Quit(); _exit(0); }
            gamepad_event(&e);
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F1 && !e.key.repeat) win_size_next(win);
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F2 && !e.key.repeat) widescreen_toggle();
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F3) panel_on = !panel_on;
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F4) gpu_set_vsync(!gpu_vsync());
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F9 && !e.key.repeat) {
                void gsrec_request(void);                   /* gsrec.c: a recording for bug reports */
                gsrec_request();
            }
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) training_key(e.key.scancode);
        }
        poll_pads();
        u32 vb = kernel_vblank_count();
        if (vb != last_vb) {
            last_vb = vb;
            gpu_vblank_tick();
            panel_update();
            hud_update();
            static u32 last_overlay;
            if (vb - last_overlay >= 25) { last_overlay = vb; overlay_update(); }
        }
        if (movie_update()) final_shown = 0;
        GpuFrame *f;
        u64 now = SDL_GetTicksNS();
        while ((f = gpu_next_frame())) {
            gpu_upload(f);
            if (fb) {
                u64 dt = now - tb;
                if (dt < 5000000) dt = 5000000;
                if (dt > 100000000) dt = 100000000;
                period = (period * 3 + dt) / 4;
            }
            gpu_release_frame(fa);
            fa = fb;
            fb = f;
            tb = now;
            final_shown = 0;
        }
        if (!fb) { SDL_Delay(1); continue; }
        float t = interp && fa ? (float)(now - tb) / (float)period : 1.0f;
        if (t >= 1.0f) {
            if (final_shown) { SDL_Delay(1); continue; }
            t = 1.0f;
            final_shown = 1;
        }
        gpu_render(fb, t < 1.0f ? interpolate(fa, fb, t) : NULL);
        gpu_present();
        render_frames++;
    }
}

int display_window(void) {
    gs_init();
    if (gpu_on) {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
        if (display_window_gpu() == 0) return 0;
        fprintf(stderr, "GPU renderer unavailable, using the software one\n");
        gpu_on = 0;
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = win_create();
    SDL_Renderer *ren = SDL_CreateRenderer(win, NULL);
    SDL_Texture *tex = NULL;
    int tw = 0, th = 0;
    u32 shown = 0;
    for (;;) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) { SDL_Quit(); _exit(0); }
            gamepad_event(&e);
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F1 && !e.key.repeat) win_size_next(win);
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_F3) panel_on = !panel_on;
        }
        poll_pads();
        u32 vb = kernel_vblank_count();
        if (vb == shown) { SDL_Delay(2); continue; }
        shown = vb;
        int w, h;
        static u32 movie_gen;
        static int mw, mh;
        if (movie_take(frame, 1024 * 1024, &mw, &mh, &movie_gen)) { w = mw; h = mh; }   /* the opening video */
        else { movie_gen = 0; gs_display(frame, 1024, 1024, &w, &h); }
        if (!tex || w != tw || h != th) {
            if (tex) SDL_DestroyTexture(tex);
            tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, h);
            tw = w;
            th = h;
        }
        SDL_UpdateTexture(tex, NULL, frame, w * 4);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_FRect dst;
        int ww, wh;
        SDL_GetCurrentRenderOutputSize(ren, &ww, &wh);
        dst.h = (float)wh;
        dst.w = ww * 3 < wh * 4 ? (float)ww : wh * 4.0f / 3.0f;     /* 4:3, bars at the sides */
        if (ww * 3 < wh * 4) dst.h = ww * 3.0f / 4.0f;
        dst.x = (ww - dst.w) / 2;
        dst.y = (wh - dst.h) / 2;
        SDL_RenderTexture(ren, tex, NULL, &dst);
        panel_update();
        panel_draw(ren);
        SDL_RenderPresent(ren);
    }
}
