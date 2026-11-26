/* widescreen_on / widescreen_toggle follow SaintSeiyaRecomp port/hle/widescreen.c (MIT, Copyright (c) 2026 Matheus
 * Nunes Pereira; licenses/SaintSeiyaRecomp-MIT.txt). The camera hook is MS Saga's. */
/*
 * Widescreen (16:9) without stretching anything.
 *
 * Every RenderWare camera sets its field of view through RwCameraSetViewWindow(camera, &window) (0x1B8B58):
 * it keeps the window (x, y: half the view's width and height at distance 1) at +0x68 / +0x6C, their
 * reciprocals at +0x70 / +0x74, and updates the camera's frame (RwFrameUpdateObjects) if it has one. RenderWare
 * builds both the projection and the frustum it culls with from that window, so with x * 4/3 the same picture
 * holds a 16:9 view: more of the scene at the sides, nothing wider, nothing culled that is in sight. Same
 * change as nemesis2000's PCSX2 widescreen patch for SLUS-21270 (which rewrites this function in place); here
 * the function is replaced (port/hle.txt) so that F2 can switch it. The window then shows the picture at 16:9,
 * and the renderer draws the 2D (menus, HUD, text) narrower by 3/4 around the centre so it keeps its shapes
 * (gpu_vk.c).
 *
 * On by default with the GPU renderer (the software one shows the PS2's picture as it is);
 * MSSAGA_WIDESCREEN=0 or F2 (remembered): the PS2's 4:3. Cameras drawing into a box of the screen (portraits, the
 * minimap) keep their window.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r5900.h"
#include "runtime.h"
#include "gpu.h"

#define FN_RwFrameUpdateObjects 0x001BAA18u

void display_config_path(char *p, size_t n, const char *name);     /* display.c */

static int widescreen = -1;

int widescreen_on(void) {
    if (widescreen < 0) {
        const char *e = getenv("MSSAGA_WIDESCREEN");
        char path[1100], buf[8] = "1";
        if (e) widescreen = atoi(e) != 0;
        else {
            display_config_path(path, sizeof path, "widescreen");
            FILE *f = fopen(path, "r");
            if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = '1'; fclose(f); }
            widescreen = atoi(buf) != 0;
        }
    }
    return widescreen && gpu_on;
}

/* F2: switched and kept (~/.local/share/mssaga/widescreen) */
void widescreen_toggle(void) {
    widescreen_on();
    widescreen = !widescreen;
    char path[1100];
    display_config_path(path, sizeof path, "widescreen");
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", widescreen); fclose(f); }
    rt_log("display", "widescreen %s", widescreen ? "on" : "off");
}

static float rdf(Ctx *c, u32 a) { u32 w = rd32(c, a); float f; memcpy(&f, &w, 4); return f; }
static void wrf(Ctx *c, u32 a, float f) { u32 w; memcpy(&w, &f, 4); wr32(c, a, w); }

/* Each camera's view window as the game set it, and what was written: a camera that draws into a box cut out of
   the screen (a dialogue portrait, the minimap's 3D: a sub-raster narrower than the screen) keeps its own, since
   the renderer keeps that box 4:3 with the 2D around it. Which raster a camera draws into is only known when it
   starts (RwCameraBeginUpdate): the window is put right there, every frame, so F2 also applies at once. */
#define MAX_CAMS 64
static struct { u32 cam; float x; int wide, boxed; } cams[MAX_CAMS];
static int cam_next;

static int cam_slot(u32 cam) {
    for (int i = 0; i < MAX_CAMS; i++) if (cams[i].cam == cam) return i;
    int i = cam_next;                                   /* cameras come and go: the oldest slot */
    cam_next = (cam_next + 1) % MAX_CAMS;
    cams[i].cam = cam;
    cams[i].boxed = 0;
    cams[i].wide = 0;
    return i;
}

static void write_window(Ctx *c, u32 cam, float x, float y) {
    wrf(c, cam + 0x68, x);
    wrf(c, cam + 0x6C, y);
    wrf(c, cam + 0x70, 1.0f / x);
    wrf(c, cam + 0x74, 1.0f / y);
    u32 frame = rd32(c, cam + 4);
    if (frame) {                                        /* its frustum is made again from the window */
        SET64(4, frame);
        rt_call(c, FN_RwFrameUpdateObjects);
    }
}

/* RwCamera *RwCameraSetViewWindow(RwCamera *camera, const RwV2d *window) */
void hle_RwCameraSetViewWindow(Ctx *c) {
    u32 cam = RW(4), win = RW(5);
    float x = rdf(c, win), y = rdf(c, win + 4);
    if (rt_verbose > 1) {                           /* -v: each camera's window when it changes */
        static u32 seen[16][3];
        int i = 0;
        while (i < 16 && seen[i][0] && seen[i][0] != cam) i++;
        u32 xb, yb;
        memcpy(&xb, &x, 4);
        memcpy(&yb, &y, 4);
        if (i < 16 && (seen[i][1] != xb || seen[i][2] != yb)) {
            seen[i][0] = cam, seen[i][1] = xb, seen[i][2] = yb;
            rt_log("wide", "camera %08X: view window %.3f x %.3f", cam, x, y);
        }
    }
    int i = cam_slot(cam);
    cams[i].x = x;
    cams[i].wide = widescreen_on() && !cams[i].boxed;
    write_window(c, cam, cams[i].wide ? x * 4.0f / 3.0f : x, y);
    SET64(2, cam);
}

/* RwCameraBeginUpdate(camera) (0x1B89F8), before it runs: the window for the raster it draws into */
void hle_RwCameraBeginUpdate(Ctx *c) {
    u32 cam = RW(4), fb = rd32(c, cam + 0x60);
    int i = -1;
    for (int k = 0; k < MAX_CAMS; k++) if (cams[k].cam == cam) i = k;
    if (i < 0 || !fb) return;                           /* a window the hook never saw: left as it is */
    cams[i].boxed = rd32(c, fb + 0x0C) < 400;            /* narrower than the 512-wide screen: a box */
    int wide = widescreen_on() && !cams[i].boxed;
    if (wide != cams[i].wide) {
        cams[i].wide = wide;
        u32 ra = RW(4), rv = RW(31);
        write_window(c, cam, wide ? cams[i].x * 4.0f / 3.0f : cams[i].x, rdf(c, cam + 0x6C));
        SET64(4, ra);                                   /* the guest code runs next, with its own argument */
        SET64(31, rv);
    }
}
