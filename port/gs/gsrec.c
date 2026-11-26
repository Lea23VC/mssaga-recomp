/* From SaintSeiyaRecomp port/gs/gsrec.c (MIT, Copyright (c) 2026 Matheus Nunes Pereira;
 * licenses/SaintSeiyaRecomp-MIT.txt). Changes for MS Saga are noted where they are. */
/*
 * A recording of the GS thread's work, replayed off line: always the same work, to time changes to
 * the VIF / VU1 / GIF / GS code and to see whether what they draw changed.
 *
 * MSSAGA_GSREC=file (MSSAGA_GSREC_FROM=vertical blank, 9000 if not given; MSSAGA_GSREC_FRAMES=n,
 * 150): at the first picture shown from that vertical blank on, the state of the GS, VIF1 and VU1
 * is saved, then everything the GS thread is given (VIF1 and GIF DMA data, VIF codes in DMA tags,
 * pictures shown) for n pictures.
 *
 *     mssaga --gs-replay file [warm]
 *
 * runs it all again on this thread, without the game, and says how long the frames after the first
 * `warm` (30) pictures shown took (the game shows a picture more than once a frame: a frame is what
 * has draws when one is shown) and a hash of everything recorded for the GPU (draws, vertices,
 * textures): the same hash, the same pictures.
 *
 * MSSAGA_REPLAY_SHOTS=folder: the replayed frames are also drawn by the Vulkan renderer and saved
 * there (frame_NNNN.ppm, every MSSAGA_REPLAY_EVERY-th frame, 10 if not given; MSSAGA_SHOT_PRESENT=WxH:
 * as a window that size shows them): the same recording drawn by two builds of the renderer must
 * give the same pictures, which a fight played twice never does.
 *
 * MS Saga: MSSAGA_REPLAY_SW=1 with MSSAGA_REPLAY_SHOTS: drawn by the software GS instead (into the GS memory, as the
 * PS2 does), the picture shown at each flip saved as sw_NNNN.ppm: the same frames through both renderers.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "r5900.h"
#include "platform.h"
#include "runtime.h"
#include "gs.h"
#include "gpu.h"

#include <SDL3/SDL.h>

void gs_state_io(FILE *f, int save);
void vif_state_io(FILE *f, int save);
void vu_state_io(FILE *f, int save);
void gs_init(void);
int gs_display(u32 *out, int max_w, int max_h, int *w, int *h);   /* gs.c */
u32 kernel_vblank_count(void);

enum { EV_VIF1 = 1, EV_GIF, EV_VIFTAG, EV_FLIP };
static const char magic[16] = "mssaga gsrec 1";

static const u32 priv_regs[5] = {0x0000, 0x0070, 0x0080, 0x0090, 0x00A0};   /* PMODE, DISPFB1, DISPLAY1, DISPFB2, DISPLAY2 */

static FILE *out;
static int state = -1;          /* -1 not set up, 0 off or done, 1 waiting for the start, 2 recording */
static u32 from, frames, recorded;

static void put(u32 kind, const void *p, u32 bytes) {
    u32 head[4] = {kind, bytes, 0, 0};
    fwrite(head, sizeof head, 1, out);
    if (bytes) {
        static const u8 zero[16];
        fwrite(p, 1, bytes, out);
        if (bytes & 15) fwrite(zero, 1, 16 - (bytes & 15), out);   /* the data stays 16-byte aligned */
    }
}

/* dmac.c, on the GS thread: data given to VIF1 or the GIF, or VIF codes in a tag */
void gsrec_data(int ch, int tag, const u8 *p, u32 bytes) {
    if (state != 2 || !p) return;
    if (tag) { if (ch == 1) put(EV_VIFTAG, p, 8); }
    else if (ch == 1) put(EV_VIF1, p, bytes);
    else if (ch == 2) put(EV_GIF, p, bytes);
}

/* F9 (display.c): the next 150 pictures recorded now, into gsrec_<vblank>.gsrec in the settings folder (for bug
   reports: mssaga --gs-replay <file> shows them again) */
void display_config_path(char *p, size_t n, const char *name);     /* display.c */
static volatile int requested;
static char path[1100];
void gsrec_request(void) { requested = 1; }

/* dmac.c, on the GS thread: a picture shown */
void gsrec_flip(void) {
    if (state < 0) {
        const char *e = getenv("MSSAGA_GSREC");
        state = e && *e;
        if (state) snprintf(path, sizeof path, "%s", e);
        from = getenv("MSSAGA_GSREC_FROM") ? (u32)atoi(getenv("MSSAGA_GSREC_FROM")) : 9000;
        frames = getenv("MSSAGA_GSREC_FRAMES") ? (u32)atoi(getenv("MSSAGA_GSREC_FRAMES")) : 150;
    }
    if (requested && state != 2) {
        requested = 0;
        char name[64];
        snprintf(name, sizeof name, "gsrec_%u.gsrec", kernel_vblank_count());
        display_config_path(path, sizeof path, name);
        from = 0;
        recorded = 0;
        state = 1;
    }
    if (state == 1 && kernel_vblank_count() >= from) {
        if (!(out = fopen(path, "wb"))) { state = 0; return; }
        fwrite(magic, sizeof magic, 1, out);
        gs_state_io(out, 1);
        vif_state_io(out, 1);
        vu_state_io(out, 1);
        state = 2;
        rt_log("gsrec", "recording %u pictures from vertical blank %u into %s", frames, kernel_vblank_count(), path);
        return;
    }
    if (state != 2) return;
    u64 priv[5];                        /* the display registers: which picture is shown */
    for (int k = 0; k < 5; k++) priv[k] = gs_priv_read(priv_regs[k]);
    put(EV_FLIP, priv, sizeof priv);
    if (++recorded >= frames) {
        fclose(out);
        out = NULL;
        state = 0;
        rt_log("gsrec", "recording done");
    }
}

static u64 fnv(u64 h, const void *p, size_t n) {
    const u8 *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 0x100000001B3ull;
    return h;
}

/* Draws with the same state one after the other hash as one: the hash says what is drawn, not how
 * it is cut into draws. */
static u64 hash_frame(u64 h, const GpuFrame *f) {
    GpuDraw last;
    for (u32 i = 0; i < f->ndraws; i++) {
        GpuDraw d = f->draws[i];
        d.first = d.count = 0;
        if (i == 0 || memcmp(&d, &last, sizeof d)) h = fnv(h, &d, sizeof d);
        last = d;
        h = fnv(h, f->verts + f->draws[i].first, (size_t)f->draws[i].count * sizeof *f->verts);
    }
    for (u32 i = 0; i < f->nuploads; i++) {
        const GpuTexUpload *u = &f->uploads[i];
        h = fnv(h, &u->id, sizeof u->id);
        h = fnv(h, &u->w, sizeof u->w);
        h = fnv(h, &u->h, sizeof u->h);
        if (u->pw) {
            h = fnv(h, &u->x, 5 * sizeof u->x);
            h = fnv(h, u->px, (size_t)u->pw * u->ph * 4);
        } else h = fnv(h, u->px, (size_t)u->w * u->h * 4);
    }
    return fnv(h, f->frees, (size_t)f->nfrees * sizeof *f->frees);
}

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

/* the renderer's picture once frame n was drawn, saved as dir/frame_NNNN.ppm */
static void replay_shot(const char *dir, u32 n) {
    static u32 *px;
    int w = 0, h = 0, pw, ph;
    if (!px) px = malloc(4096 * 4096 * 4);
    const char *e = getenv("MSSAGA_SHOT_PRESENT");
    if (e && sscanf(e, "%dx%d", &pw, &ph) == 2 && pw > 0 && ph > 0 && pw <= 4096 && ph <= 4096) {
        gpu_read_presented(px, pw, ph);
        w = pw;
        h = ph;
    } else gpu_read_display(px, 1024, 1024, &w, &h);
    char file[1100];
    snprintf(file, sizeof file, "%s/frame_%04u.ppm", dir, n);
    FILE *f = fopen(file, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        u8 rgb[3] = {(u8)px[i], (u8)(px[i] >> 8), (u8)(px[i] >> 16)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

int gsrec_replay(const char *path, int warm) {
    FILE *f = fopen(path, "rb");
    char m[16];
    if (!f || fread(m, sizeof m, 1, f) != 1 || memcmp(m, magic, sizeof m)) {
        fprintf(stderr, "%s: not a GS recording\n", path);
        return 1;
    }
    extern int gpu_on;
    int sw = getenv("MSSAGA_REPLAY_SW") && atoi(getenv("MSSAGA_REPLAY_SW"));
    gpu_on = !sw;               /* recorded for the GPU, as in the game (drawn only for MSSAGA_REPLAY_SHOTS) */
    const char *shots = getenv("MSSAGA_REPLAY_SHOTS");
    u32 every = getenv("MSSAGA_REPLAY_EVERY") ? (u32)atoi(getenv("MSSAGA_REPLAY_EVERY")) : 10;
    if (!every) every = 1;
    if (shots && !sw && (!SDL_Init(SDL_INIT_VIDEO) || gpu_init(NULL))) { fprintf(stderr, "no GPU for MSSAGA_REPLAY_SHOTS\n"); return 1; }
    gs_init();
    gs_state_io(f, 0);
    vif_state_io(f, 0);
    vu_state_io(f, 0);
    long at = ftell(f);
    fseek(f, 0, SEEK_END);
    size_t size = (size_t)(ftell(f) - at);
    fseek(f, at, SEEK_SET);
    u8 *ev = platform_aligned_alloc(64, (size + 63) & ~(size_t)63);
    if (!ev || fread(ev, 1, size, f) != size) { fprintf(stderr, "%s: cannot read\n", path); return 1; }
    fclose(f);
    u64 h = 0xCBF29CE484222325ull;
    u32 pictures = 0, sealed = 0;
    u64 draws = 0;
    double t0 = now_ms(), tw = t0, hashing = 0;     /* the hash is not part of the time */
    for (size_t i = 0; i + 16 <= size;) {
        u32 kind, bytes;
        memcpy(&kind, ev + i, 4);
        memcpy(&bytes, ev + i + 4, 4);
        const u8 *p = ev + i + 16;
        i += 16 + ((bytes + 15) & ~15u);
        switch (kind) {
        case EV_VIF1: vif1_dma(p, bytes / 16); break;
        case EV_GIF: gif_dma(p, bytes / 16); break;
        case EV_VIFTAG: vif_tag(1, p); break;
        case EV_FLIP: {
            if (bytes >= 40)                    /* (recordings made before 2026-11-08 don't have them) */
                for (int k = 0; k < 5; k++) { u64 v; memcpy(&v, p + 8 * k, 8); gs_priv_write(priv_regs[k], v); }
            if (sw) {                           /* the software GS: the picture shown now */
                static u32 nflip, *px;
                if (!px) px = malloc(1024 * 1024 * 4);
                int w = 0, hh = 0;
                if (shots && nflip % every == 0 && (gs_display(px, 1024, 1024, &w, &hh), w && hh)) {
                    char file[1100];
                    snprintf(file, sizeof file, "%s/sw_%04u.ppm", shots, nflip);
                    FILE *o = fopen(file, "wb");
                    if (o) {
                        fprintf(o, "P6\n%d %d\n255\n", w, hh);
                        for (int k = 0; k < w * hh; k++) { u8 rgb[3] = {(u8)px[k], (u8)(px[k] >> 8), (u8)(px[k] >> 16)}; fwrite(rgb, 1, 3, o); }
                        fclose(o);
                    }
                }
                nflip++;
                pictures++;
                break;
            }
            gpu_seal_frame();
            GpuFrame *fr;
            double th = now_ms();
            while ((fr = gpu_next_frame())) {
                static u32 nframe;
                if (pictures >= (u32)warm) { h = hash_frame(h, fr); draws += fr->ndraws; sealed++; }
                {   /* MSSAGA_REPLAY_DUMP=n: frame n's textured draws (texture, size, place, blending) */
                    static int dump = -2;
                    if (dump == -2) dump = getenv("MSSAGA_REPLAY_DUMP") ? atoi(getenv("MSSAGA_REPLAY_DUMP")) : -1;
                    if ((int)nframe == dump)
                        for (u32 k = 0; k < fr->ndraws; k++) {
                            const GpuDraw *d = &fr->draws[k];
                            if (!d->textured && !d->flat2d && !getenv("MSSAGA_REPLAY_DUMP_ALL")) continue;
                            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
                            for (u32 q = d->first; q < d->first + d->count; q++) {
                                const GpuVertex *v = &fr->verts[q];
                                x0 = v->x < x0 ? v->x : x0; x1 = v->x > x1 ? v->x : x1;
                                y0 = v->y < y0 ? v->y : y0; y1 = v->y > y1 ? v->y : y1;
                            }
                            const GpuVertex *v = &fr->verts[d->first];
                            printf("draw %u: tex %d %ux%u verts %u at %.0f-%.0f x %.0f-%.0f rgba %.0f,%.0f,%.0f,%.0f st %.3f,%.3f q %.3f "
                                   "fst %u abe %u alpha %llx tfx %u tcc %u filter %u flat2d %u fbp %X\n",
                                   k, d->tex, d->tw, d->th, d->count, x0, x1, y0, y1, v->r, v->g, v->b, v->a, v->s, v->t, v->q,
                                   d->fst, d->abe, (unsigned long long)d->alpha, d->tfx, d->tcc, d->filter, d->flat2d, d->fbp);
                            printf("   z %.6f..%.6f ztst %u zwrite %u sides %u fullw %u box %u scissor %u-%u x %u-%u\n",
                                   fr->verts[d->first].z, fr->verts[d->first + d->count - 1].z, d->ztst, d->zwrite, d->sides,
                                   d->fullw, d->box, d->scissor[0], d->scissor[1], d->scissor[2], d->scissor[3]);
                            if (((d->alpha) & 0xFF) == 0x48)
                                for (u32 q = d->first; q < d->first + d->count; q++)
                                    printf("   v %.1f,%.1f s %.4f t %.4f q %.4f\n", fr->verts[q].x, fr->verts[q].y, fr->verts[q].s,
                                           fr->verts[q].t, fr->verts[q].q);
                        }
                }
                if (shots) {
                    gpu_upload(fr);                         /* every frame drawn: the picture shown is often */
                    gpu_render(fr, NULL);                   /* the one drawn the frame before (double buffering) */
                    if (nframe % every == 0) replay_shot(shots, nframe);
                }
                nframe++;
                gpu_release_frame(fr);
            }
            if (pictures >= (u32)warm) hashing += now_ms() - th;
            if (++pictures == (u32)warm) tw = now_ms();
            break;
        }
        }
    }
    double t1 = now_ms();
    u32 timed = pictures > (u32)warm ? pictures - (u32)warm : 0;
    printf("%u pictures shown (%u timed): %u frames, %llu draws, %.3f ms per frame, all %.0f ms, hash %016llx\n", pictures,
           timed, sealed, (unsigned long long)draws, sealed ? (t1 - tw - hashing) / sealed : 0.0, t1 - t0, (unsigned long long)h);
    return 0;
}
