/* From SaintSeiyaRecomp port/gs/texpack.c (MIT, Copyright (c) 2026 Matheus Nunes Pereira;
 * licenses/SaintSeiyaRecomp-MIT.txt). Changes for MS Saga are noted where they are. */
/*
 * Texture packs: the game's textures saved as PNG files, and PNG files used in their place.
 *
 * A texture is named by its decoded content (RGBA pixels and size, 16 hex digits), the same
 * whatever GS memory it was read from and from one run to the next:
 *   MSSAGA_TEX_DUMP=1   each texture decoded for the first time is saved as
 *                       ~/.local/share/mssaga/textures/dump/<name>.png (dump/ of the folder
 *                       MSSAGA_TEXTURES names, when it names one)
 *   textures/<name>.png is used instead of that texture, at any size (a 4x upscale, a repaint):
 *                       draws keep addressing it in the original's texels, so nothing else changes.
 * MSSAGA_TEXTURES=0 turns replacements off; MSSAGA_TEXTURES=<folder> takes them from elsewhere.
 * PNG alpha is 0..255; the GS's is 0..0x80 (1.0), converted both ways.
 *
 * Everything here runs on the GS thread (gs.c decodes textures there).
 */
#include <SDL3/SDL.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "r5900.h"
#include "runtime.h"
#include "platform.h"

void display_config_path(char *p, size_t n, const char *name);     /* display.c */

/* PNG files need SDL 3.4 (SDL_LoadPNG, SDL_SavePNG); with an older SDL (the AppImage's, Debian 13's
 * 3.2) there are no texture packs */
#if !SDL_VERSION_ATLEAST(3, 4, 0)
static SDL_Surface *SDL_LoadPNG(const char *path) { (void)path; SDL_SetError("texture packs need SDL 3.4"); return NULL; }
static bool SDL_SavePNG(SDL_Surface *s, const char *path) { (void)s; (void)path; SDL_SetError("texture packs need SDL 3.4"); return false; }
#endif

static char dir[1024];               /* replacements */
static int inited, dump_on, packs_off;
static u64 *names;                   /* replacements present, sorted */
static u32 nnames;

typedef struct { u64 name; u32 w, h; u32 *px; } Loaded;
static Loaded *loaded;               /* replacements read so far (kept: textures come back) */
static u32 nloaded, cap_loaded;

static int cmp_u64(const void *a, const void *b) {
    u64 x = *(const u64 *)a, y = *(const u64 *)b;
    return x < y ? -1 : x > y;
}

static void init(void) {
    inited = 1;
    const char *e = getenv("MSSAGA_TEX_DUMP");
    dump_on = e && atoi(e);
    e = getenv("MSSAGA_TEXTURES");
    if (e && *e && strcmp(e, "0")) snprintf(dir, sizeof dir, "%s", e);
    else display_config_path(dir, sizeof dir, "textures");
    if (e && !strcmp(e, "0")) { packs_off = 1; return; }
    mkdir(dir, 0755);
    DIR *d = opendir(dir);
    if (!d) return;
    u32 cap = 0;
    for (struct dirent *de; (de = readdir(d));) {
        u64 n;
        char tail[8];
        if (strlen(de->d_name) != 20 || sscanf(de->d_name, "%16llx%7s", (unsigned long long *)&n, tail) != 2) continue;
        if (strcmp(tail, ".png")) continue;
        if (nnames == cap) names = realloc(names, (cap = cap ? cap * 2 : 256) * sizeof *names);
        names[nnames++] = n;
    }
    closedir(d);
    qsort(names, nnames, sizeof *names, cmp_u64);
    if (nnames) rt_log("tex", "%u replacement textures in %s", nnames, dir);
}

u64 texpack_name(const u32 *px, u32 w, u32 h) {
    u64 a = 0x9E3779B97F4A7C15ull ^ ((u64)w << 32 | h), b = 0xC2B2AE3D27D4EB4Full;
    u32 n = w * h, i = 0;
    for (; i + 1 < n; i += 2) {
        a = (a ^ px[i]) * 0x100000001B3ull;
        b = (b ^ px[i + 1]) * 0xFF51AFD7ED558CCDull;
    }
    if (i < n) a = (a ^ px[i]) * 0x100000001B3ull;
    return (a ^ (b >> 29) ^ (b << 7)) * 0x9E3779B97F4A7C15ull;
}

/* MSSAGA_TEX_DUMP=1: textures/dump/<name>.png, once */
void texpack_dump(u64 name, const u32 *px, u32 w, u32 h) {
    if (!inited) init();
    if (!dump_on) return;
    static u64 *seen;
    static u32 nseen, cap_seen;
    for (u32 i = 0; i < nseen; i++)
        if (seen[i] == name) return;
    if (nseen == cap_seen) seen = realloc(seen, (cap_seen = cap_seen ? cap_seen * 2 : 1024) * sizeof *seen);
    seen[nseen++] = name;
    char path[1200];
    snprintf(path, sizeof path, "%s", dir);
    mkdir(path, 0755);
    strcat(path, "/dump");
    mkdir(path, 0755);
    snprintf(path + strlen(path), sizeof path - strlen(path), "/%016llx.png", (unsigned long long)name);
    struct stat st;
    if (stat(path, &st) == 0) return;
    SDL_Surface *s = SDL_CreateSurface((int)w, (int)h, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return;
    for (u32 y = 0; y < h; y++) {
        u32 *row = (u32 *)((u8 *)s->pixels + (size_t)y * s->pitch);
        for (u32 x = 0; x < w; x++) {
            u32 c = px[y * w + x], a = c >> 24;
            a = a >= 0x80 ? 255 : a * 255 / 128;
            row[x] = (c & 0xFFFFFF) | a << 24;
        }
    }
    if (!SDL_SavePNG(s, path)) rt_log("tex", "%s: %s", path, SDL_GetError());
    SDL_DestroySurface(s);
}

/* ---- pictures
 * The game's TIM2 pictures (tools/tex_extract.py writes <pack>/index.txt from the disc image) are
 * found inside the textures decoded: a texture is often larger than its picture (the rest of GS
 * memory is garbage), so a texture's name changes from one run to the next while its picture's
 * does not. A picture's name hashes its rows (each a polynomial of its texels, then mixed), folded
 * in order, with its size: the same in tex_extract.py. Replacements: <pack>/hd/<name>.png, of any
 * size, drawn in place of the picture's texels (gs.c redirects the draws inside the picture). */
typedef struct { u64 name; u32 w, h; } Pic;
static Pic *pics;                          /* sorted by name */
static u32 npics;
static u64 *row0s;                         /* (first row's hash, width) keys, sorted */
typedef struct { u32 w, maxh; } PicWidth;
static PicWidth *widths;                   /* the pictures' widths, ascending */
static u32 nwidths;
static u64 *hd_names;                      /* pictures with a replacement in hd/, sorted */
static u32 nhd;
static int pics_inited;

#define ROW_K 0x9E3779B97F4A7C15ull
static u64 kpow[1024];

static u64 fmix(u64 h) {
    h ^= h >> 33; h *= 0xFF51AFD7ED558CCDull;
    h ^= h >> 33; h *= 0xC4CEB9FE1A85EC53ull;
    return h ^ h >> 33;
}
static u64 row_hash(const u32 *px, u32 w) {
    u64 s = 0;
    for (u32 x = 0; x < w; x++) s += (u64)px[x] * kpow[x];
    return fmix(s);
}
static u64 row0_key(u64 r0, u32 w) { return fmix(r0 ^ (u64)w * ROW_K); }
static int cmp_pic(const void *a, const void *b) { return cmp_u64(&((const Pic *)a)->name, &((const Pic *)b)->name); }

static void pics_init(void) {
    pics_inited = 1;
    if (!inited) init();
    if (packs_off) return;
    u64 k = 1;
    for (u32 i = 0; i < 1024; i++, k *= ROW_K) kpow[i] = k;
    char path[1200];
    snprintf(path, sizeof path, "%s/index.txt", dir);
    FILE *f = fopen(path, "r");
    u32 cap = 0;
    char line[512];
    while (f && fgets(line, sizeof line, f)) {
        unsigned long long n, r0;
        u32 w, h;
        if (sscanf(line, "%llx %u %u %llx", &n, &w, &h, &r0) != 4 || !w || !h || w > 1024 || h > 1024) continue;
        if (npics == cap) {
            cap = cap ? cap * 2 : 1024;
            pics = realloc(pics, cap * sizeof *pics);
            row0s = realloc(row0s, cap * sizeof *row0s);
        }
        row0s[npics] = row0_key(r0, w);
        pics[npics++] = (Pic){n, w, h};
        u32 k = 0;
        while (k < nwidths && widths[k].w != w) k++;
        if (k == nwidths) {
            widths = realloc(widths, (nwidths + 1) * sizeof *widths);
            widths[nwidths++] = (PicWidth){w, 0};
        }
        if (h > widths[k].maxh) widths[k].maxh = h;
    }
    if (f) fclose(f);
    qsort(pics, npics, sizeof *pics, cmp_pic);
    qsort(row0s, npics, sizeof *row0s, cmp_u64);
    for (u32 i = 1; i < nwidths; i++)              /* ascending widths */
        for (u32 j = i; j > 0 && widths[j - 1].w > widths[j].w; j--) {
            PicWidth t = widths[j]; widths[j] = widths[j - 1]; widths[j - 1] = t;
        }
    snprintf(path, sizeof path, "%s/hd", dir);
    DIR *d = opendir(path);
    u32 hcap = 0;
    for (struct dirent *de; d && (de = readdir(d));) {
        unsigned long long n;
        char tail[8];
        if (strlen(de->d_name) != 20 || sscanf(de->d_name, "%16llx%7s", &n, tail) != 2 || strcmp(tail, ".png")) continue;
        if (nhd == hcap) hd_names = realloc(hd_names, (hcap = hcap ? hcap * 2 : 256) * sizeof *hd_names);
        hd_names[nhd++] = n;
    }
    if (d) closedir(d);
    qsort(hd_names, nhd, sizeof *hd_names, cmp_u64);
    rt_log("tex", "%u pictures (%u widths) in %s, %u with a replacement", npics, nwidths, path, nhd);
}

/* The largest picture of the index at the top left of a tw x th texture: its name (0: none) and
   size. Widths whose first row is no picture's are skipped after one row. */
u64 texpack_identify(const u32 *px, u32 tw, u32 th, u32 *pw, u32 *ph) {
    if (!pics_inited) pics_init();
    u64 best = 0;
    u32 best_area = 0;
    for (u32 k = 0; k < nwidths && widths[k].w <= tw; k++) {
        u32 w = widths[k].w, maxh = widths[k].maxh < th ? widths[k].maxh : th;
        if ((u64)w * maxh <= best_area) continue;
        u64 r = row_hash(px, w), key = row0_key(r, w);
        if (!bsearch(&key, row0s, npics, sizeof *row0s, cmp_u64)) continue;
        u64 a = fmix(0x5EEDull ^ w);
        for (u32 y = 0; y < maxh; y++) {
            if (y) r = row_hash(px + (size_t)y * tw, w);
            a = fmix((a ^ r) * 0x100000001B3ull);
            Pic key2 = {fmix(a ^ (u64)(y + 1) << 32), 0, 0};
            Pic *p = bsearch(&key2, pics, npics, sizeof *pics, cmp_pic);
            if (p && p->w == w && p->h == y + 1 && w * (y + 1) > best_area) {
                best = p->name;
                best_area = w * (y + 1);
                *pw = w;
                *ph = y + 1;
            }
        }
    }
    return best;
}

/* The name of the w x h picture at x, y of a texture's texels (stride texels a row) */
u64 texpack_rect_name(const u32 *px, u32 stride, u32 x, u32 y, u32 w, u32 h) {
    if (!pics_inited) pics_init();
    u64 a = fmix(0x5EEDull ^ w);
    for (u32 r = 0; r < h; r++) a = fmix((a ^ row_hash(px + (size_t)(y + r) * stride + x, w)) * 0x100000001B3ull);
    return fmix(a ^ (u64)h << 32);
}

int texpack_known(u64 pic) {
    Pic key = {pic, 0, 0};
    return npics && bsearch(&key, pics, npics, sizeof *pics, cmp_pic) != NULL;
}

/* pictures are looked for (uploads noted, textures searched): there is an index of the disc's
   pictures, replacements of pictures, or the dump is on */
int texpack_pictures_on(void) {
    if (!pics_inited) pics_init();
    return npics || nhd || dump_on;
}

/* MSSAGA_TEX_DUMP=1: a picture found by its upload but not in the index: saved as
   dump/<name>.png, once, and listed in dump/index.txt (texture pack tools take both) */
void texpack_dump_picture(u64 pic, const u32 *px, u32 stride, u32 x, u32 y, u32 w, u32 h) {
    if (!dump_on) return;
    static u64 *seen;
    static u32 nseen, cap_seen;
    for (u32 i = 0; i < nseen; i++)
        if (seen[i] == pic) return;
    if (nseen == cap_seen) seen = realloc(seen, (cap_seen = cap_seen ? cap_seen * 2 : 1024) * sizeof *seen);
    seen[nseen++] = pic;
    u32 *tmp = malloc((size_t)w * h * 4);
    for (u32 r = 0; r < h; r++) memcpy(tmp + (size_t)r * w, px + (size_t)(y + r) * stride + x, (size_t)w * 4);
    char path[1200];
    snprintf(path, sizeof path, "%s/dump/index.txt", dir);
    struct stat st;
    char png[1200];
    snprintf(png, sizeof png, "%s/dump/%016llx.png", dir, (unsigned long long)pic);
    if (stat(png, &st) != 0) {
        texpack_dump(pic, tmp, w, h);                       /* (it saves only while the dump is on) */
        FILE *f = fopen(path, "a");
        if (f) {
            fprintf(f, "%016llx %u %u %016llx dump/%016llx.png\n", (unsigned long long)pic, w, h,
                    (unsigned long long)row_hash(tmp, w), (unsigned long long)pic);
            fclose(f);
        }
    }
    free(tmp);
}

int texpack_has_hd(u64 pic) {
    if (!pics_inited) pics_init();
    return nhd && bsearch(&pic, hd_names, nhd, sizeof *hd_names, cmp_u64) != NULL;
}

/* A picture's replacement, read from hd/<name>.png: RGBA (malloc'd, GS alpha) and its size, or NULL. */
u32 *texpack_load_hd(u64 pic, u32 *w, u32 *h) {
    char path[1200];
    snprintf(path, sizeof path, "%s/hd/%016llx.png", dir, (unsigned long long)pic);
    SDL_Surface *s = SDL_LoadPNG(path), *c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888) : NULL;
    if (s) SDL_DestroySurface(s);
    if (!c || c->w > 8192 || c->h > 8192) {
        rt_log("tex", "%s: %s", path, c ? "larger than 8192" : SDL_GetError());
        if (c) SDL_DestroySurface(c);
        return NULL;
    }
    *w = (u32)c->w;
    *h = (u32)c->h;
    u32 *out = malloc((size_t)*w * *h * 4);
    for (u32 y = 0; y < *h; y++) {
        const u32 *row = (const u32 *)((const u8 *)c->pixels + (size_t)y * c->pitch);
        for (u32 x = 0; x < *w; x++) {
            u32 p = row[x], a = p >> 24;
            out[y * *w + x] = (p & 0xFFFFFF) | ((a * 128 + 127) / 255) << 24;
        }
    }
    SDL_DestroySurface(c);
    return out;
}

/* The replacement for a texture: new RGBA pixels (malloc'd, GS alpha) and their size, or NULL. */
u32 *texpack_find(u64 name, u32 *w, u32 *h) {
    if (!inited) init();
    if (!nnames || !bsearch(&name, names, nnames, sizeof *names, cmp_u64)) return NULL;
    Loaded *l = NULL;
    for (u32 i = 0; i < nloaded; i++)
        if (loaded[i].name == name) l = &loaded[i];
    if (!l) {
        char path[1200];
        snprintf(path, sizeof path, "%s/%016llx.png", dir, (unsigned long long)name);
        SDL_Surface *s = SDL_LoadPNG(path), *c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888) : NULL;
        if (s) SDL_DestroySurface(s);
        if (nloaded == cap_loaded) loaded = realloc(loaded, (cap_loaded = cap_loaded ? cap_loaded * 2 : 64) * sizeof *loaded);
        l = &loaded[nloaded++];
        *l = (Loaded){name, 0, 0, NULL};
        if (!c || c->w > 8192 || c->h > 8192) {
            rt_log("tex", "%s: %s", path, c ? "larger than 8192" : SDL_GetError());
            if (c) SDL_DestroySurface(c);
            return NULL;
        }
        l->w = (u32)c->w;
        l->h = (u32)c->h;
        l->px = malloc((size_t)l->w * l->h * 4);
        for (u32 y = 0; y < l->h; y++) {
            const u32 *row = (const u32 *)((const u8 *)c->pixels + (size_t)y * c->pitch);
            for (u32 x = 0; x < l->w; x++) {
                u32 p = row[x], a = p >> 24;
                l->px[y * l->w + x] = (p & 0xFFFFFF) | ((a * 128 + 127) / 255) << 24;
            }
        }
        SDL_DestroySurface(c);
        rt_log("tex", "replaced %016llx (%ux%u)", (unsigned long long)name, l->w, l->h);
    }
    if (!l->px) return NULL;
    u32 *out = malloc((size_t)l->w * l->h * 4);
    memcpy(out, l->px, (size_t)l->w * l->h * 4);
    *w = l->w;
    *h = l->h;
    return out;
}

/* ---- PCSX2 texture packs
 * A pack made for PCSX2 works as it is: <textures>/SLUS-21270/replacements/ (and its subfolders), PNG files named as
 * PCSX2 names textures: <texture hash>-<palette hash>-<bits>.png, or <texture hash>-<bits>.png without a palette
 * (gs.c pcsx2_name works the same names out: an xxHash3 of the texture's GS memory blocks and of its palette, the
 * format and size). The PNGs keep the PS2's alpha (0x80 opaque), as PCSX2 reads them. They are many and large (one
 * pack: 17,000 files, 4 GB): read by two threads in the background, the texture drawn as the game's own until its
 * replacement is in; the ones read stay in memory up to a budget, the least recently used dropped beyond it.
 * F1 > Video > HD texture pack (file `texture_pack`, MSSAGA_TEXTURE_PACK=0) turns them off. */
typedef struct { u64 t, c; u32 bits, path; } P2Name;   /* path: offset in p2_paths */
typedef struct { u32 *px; u32 w, h, last; u8 state; } P2Tex;   /* state: 0 not read, 1 queued, 2 ready, 3 failed */
static P2Name *p2;
static P2Tex *p2tex;
static u32 np2, p2_inited;
static char *p2_paths;
static size_t p2_paths_len, p2_paths_cap;
static char p2_dir[1100];
static SDL_Mutex *p2_mu;
static SDL_Condition *p2_cv;
static u32 *p2_queue, p2_qhead, p2_qtail, p2_qcap;  /* ring of indices to read */
static volatile u32 p2_done;                       /* replacements read so far (gs.c polls it) */
static u64 p2_bytes;
#define P2_BUDGET (1536ull << 20)
static int p2_off = -1;

int texpack_option(void);

static int cmp_p2(const void *a, const void *b) {
    const P2Name *x = a, *y = b;
    if (x->t != y->t) return x->t < y->t ? -1 : 1;
    if (x->c != y->c) return x->c < y->c ? -1 : 1;
    return x->bits < y->bits ? -1 : x->bits > y->bits;
}

static void p2_scan(const char *d, int depth) {
    DIR *dd = opendir(d);
    if (!dd) return;
    for (struct dirent *de; (de = readdir(dd));) {
        if (de->d_name[0] == '.') continue;
        char path[2048];
        snprintf(path, sizeof path, "%s/%s", d, de->d_name);
        size_t n = strlen(de->d_name);
        if (n > 4 && !strcmp(de->d_name + n - 4, ".png")) {
            unsigned long long t, c;
            unsigned bits;
            char dot;
            P2Name e = {0, 0, 0, 0};
            if (sscanf(de->d_name, "%llx-%llx-%08x%c", &t, &c, &bits, &dot) == 4 && dot == '.')
                e = (P2Name){t, c, bits & ~(1u << 14), 0};      /* bit 14 (an old field, TCC) is ignored, as PCSX2 does */
            else if (sscanf(de->d_name, "%llx-%08x%c", &t, &bits, &dot) == 3 && dot == '.')
                e = (P2Name){t, 0, bits & ~(1u << 14), 0};
            else continue;
            size_t len = strlen(path) - strlen(p2_dir) + 1;    /* relative to the pack's folder */
            if (p2_paths_len + len > p2_paths_cap)
                p2_paths = realloc(p2_paths, p2_paths_cap = (p2_paths_cap + len) * 2);
            memcpy(p2_paths + p2_paths_len, path + strlen(p2_dir), len);
            e.path = (u32)p2_paths_len;
            p2_paths_len += len;
            static u32 cap;
            if (np2 == cap) p2 = realloc(p2, (cap = cap ? cap * 2 : 4096) * sizeof *p2);
            p2[np2++] = e;
        } else if (depth < 8) {
            struct stat st;
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) p2_scan(path, depth + 1);
        }
    }
    closedir(dd);
}

static int SDLCALL p2_worker(void *arg) {
    (void)arg;
    for (;;) {
        SDL_LockMutex(p2_mu);
        while (p2_qhead == p2_qtail) SDL_WaitCondition(p2_cv, p2_mu);
        u32 i = p2_queue[p2_qhead++ % p2_qcap];
        SDL_UnlockMutex(p2_mu);
        char path[2400];
        snprintf(path, sizeof path, "%s%s", p2_dir, p2_paths + p2[i].path);
        SDL_Surface *s = SDL_LoadPNG(path), *c = s ? SDL_ConvertSurface(s, SDL_PIXELFORMAT_ABGR8888) : NULL;
        if (s) SDL_DestroySurface(s);
        u32 *px = NULL, w = 0, h = 0;
        if (c && c->w <= 8192 && c->h <= 8192) {
            w = (u32)c->w;
            h = (u32)c->h;
            px = malloc((size_t)w * h * 4);
            for (u32 y = 0; px && y < h; y++)               /* alpha as it is: the PS2's scale, as PCSX2 dumps it */
                memcpy(px + (size_t)y * w, (const u8 *)c->pixels + (size_t)y * c->pitch, (size_t)w * 4);
        } else
            rt_log("tex", "%s: %s", path, c ? "larger than 8192" : SDL_GetError());
        if (c) SDL_DestroySurface(c);
        SDL_LockMutex(p2_mu);
        p2tex[i].px = px;
        p2tex[i].w = w;
        p2tex[i].h = h;
        p2tex[i].state = px ? 2 : 3;
        if (px) p2_bytes += (u64)w * h * 4;
        p2_done++;
        SDL_UnlockMutex(p2_mu);
    }
    return 0;
}

static SDL_InitState p2_once;                      /* the GS thread and the menu may both ask first */

static void p2_init(void) {
    if (!SDL_ShouldInit(&p2_once)) { p2_inited = 1; return; }
    if (!inited) init();
    if (packs_off) { SDL_SetInitialized(&p2_once, true); p2_inited = 1; return; }
    snprintf(p2_dir, sizeof p2_dir, "%s/SLUS-21270/replacements", dir);
    p2_scan(p2_dir, 0);
    if (!np2) { SDL_SetInitialized(&p2_once, true); p2_inited = 1; return; }
    qsort(p2, np2, sizeof *p2, cmp_p2);
    p2tex = calloc(np2, sizeof *p2tex);
    p2_qcap = np2 + 1;
    p2_queue = malloc(p2_qcap * sizeof *p2_queue);
    p2_mu = SDL_CreateMutex();
    p2_cv = SDL_CreateCondition();
    for (int k = 0; k < 2; k++) SDL_DetachThread(SDL_CreateThread(p2_worker, "texpack", NULL));
    rt_log("tex", "PCSX2 texture pack: %u textures in %s", np2, p2_dir);
    SDL_SetInitialized(&p2_once, true);
    p2_inited = 1;
}

/* a PCSX2 pack is there and on */
int texpack_pcsx2_on(void) {
    if (!p2_inited) p2_init();
    if (p2_off < 0) p2_off = !texpack_option();
    return np2 && !p2_off;
}

void texpack_pcsx2_set(int on) { p2_off = !on; }

/* the pack's texture of that name: its index, or -1 */
s32 texpack_pcsx2_find(u64 t, u64 c, u32 bits) {
    if (!texpack_pcsx2_on()) return -1;
    P2Name key = {t, c, bits, 0};
    P2Name *e = bsearch(&key, p2, np2, sizeof *p2, cmp_p2);
    if (!e && c) {                                     /* a pack's texture made without its palette's hash */
        key.c = 0;
        e = bsearch(&key, p2, np2, sizeof *p2, cmp_p2);
    }
    return e ? (s32)(e - p2) : -1;
}

/* drops the least recently used replacements read, down to the budget (gs thread) */
static void p2_trim(u32 now) {
    while (p2_bytes > P2_BUDGET) {
        u32 old = ~0u, k = ~0u;
        for (u32 i = 0; i < np2; i++)
            if (p2tex[i].state == 2 && now - p2tex[i].last >= 2 && (k == ~0u || p2tex[i].last < old)) {
                old = p2tex[i].last;
                k = i;
            }
        if (k == ~0u) return;
        free(p2tex[k].px);
        p2_bytes -= (u64)p2tex[k].w * p2tex[k].h * 4;
        p2tex[k] = (P2Tex){NULL, 0, 0, 0, 0};
    }
}

/* The replacement's pixels (a malloc'd copy, for an upload) and size when it has been read; else NULL, and it is
   queued to be read (0 in *failed), or it can't be (1). `now`: the frame, for the budget. */
u32 *texpack_pcsx2_get(s32 i, u32 *w, u32 *h, u32 now, int *failed) {
    *failed = 0;
    if (i < 0 || (u32)i >= np2) { *failed = 1; return NULL; }
    SDL_LockMutex(p2_mu);
    P2Tex *t = &p2tex[i];
    u32 *out = NULL;
    if (t->state == 2) {
        t->last = now;
        *w = t->w;
        *h = t->h;
        out = malloc((size_t)t->w * t->h * 4);
        if (out) memcpy(out, t->px, (size_t)t->w * t->h * 4);
    } else if (t->state == 3) *failed = 1;
    else if (t->state == 0) {
        t->state = 1;
        t->last = now;
        p2_queue[p2_qtail++ % p2_qcap] = (u32)i;
        SDL_SignalCondition(p2_cv);
    }
    int over = p2_bytes > P2_BUDGET;
    SDL_UnlockMutex(p2_mu);
    if (over) {
        SDL_LockMutex(p2_mu);
        p2_trim(now);
        SDL_UnlockMutex(p2_mu);
    }
    return out;
}

/* the pack's textures (0: none): the settings menu */
int texpack_pcsx2_count(void) {
    if (!p2_inited) p2_init();
    return (int)np2;
}

/* replacements read so far: gs.c looks at the ones it waits for when this changes */
u32 texpack_pcsx2_done(void) { return p2_done; }
