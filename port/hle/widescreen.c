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
 * MSSAGA_WIDESCREEN=0 or F2 (remembered): the PS2's 4:3. A camera picks up F2 when it next sets its window.
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

/* RwCamera *RwCameraSetViewWindow(RwCamera *camera, const RwV2d *window) */
void hle_RwCameraSetViewWindow(Ctx *c) {
    u32 cam = RW(4), win = RW(5);
    float x = rdf(c, win), y = rdf(c, win + 4);
    if (rt_verbose) {                               /* -v: each camera's window when it changes */
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
    if (widescreen_on()) x *= 4.0f / 3.0f;
    wrf(c, cam + 0x68, x);
    wrf(c, cam + 0x6C, y);
    wrf(c, cam + 0x70, 1.0f / x);
    wrf(c, cam + 0x74, 1.0f / y);
    u32 frame = rd32(c, cam + 4);
    if (frame) {
        SET64(4, frame);
        rt_call(c, FN_RwFrameUpdateObjects);
    }
    SET64(2, cam);
}
