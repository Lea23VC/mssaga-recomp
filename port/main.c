/*
 * MS Saga: A New Dawn, native: entry point.
 *
 *     mssaga [disc/SLUS_212.70] [--iso disc/game.iso] [-q | -v | -vv] [--trace]
 *            [--shot 300,600,...] [--press "1100:CROSS,..."]
 *
 * Loads the executable into guest RAM (its code is the recompiled C linked into this program; the image is still
 * needed for its data, and because the SN DLL loader patches instructions in it), and runs the main thread from
 * the ELF entry point on a thread of its own; this thread shows the picture (port/gs/display.c), or with --shot
 * saves the pictures at those vertical blanks (build/shot_N.ppm) without a window. MSSAGA_GS=sw draws with the
 * software GS instead of Vulkan.
 *
 * Structure from SaintSeiyaRecomp's port/main.c (MIT, Copyright (c) 2026 Matheus Nunes Pereira;
 * licenses/SaintSeiyaRecomp-MIT.txt).
 */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "r5900.h"
#include "runtime.h"
#include "kernel.h"
#include "platform.h"

extern const FuncEntry game_functab[];    /* build/recomp/functab.c */
extern const int game_functab_count;
extern const FuncEntry *rt_functab;       /* runtime/cpu.c */
extern int rt_functab_count;

int cdvd_open(const char *path);          /* hle/cdvd.c */
int display_window(void);                 /* gs/display.c */
int display_shots(const int *at, int n);
void display_set_presses(const char *spec);
void prof_start(void);                    /* runtime/prof.c */
void perf_calibrate(void);

static u32 entry;

static void *game_main(void *arg) {
    (void)arg;
    kernel_init();
    kernel_run(entry);
    return NULL;
}

int main(int argc, char **argv) {
    const char *elf = "disc/SLUS_212.70", *iso = "disc/game.iso";
    int shots[64], nshots = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-q")) rt_verbose = 0;
        else if (!strcmp(argv[i], "-v")) rt_verbose = 2;
        else if (!strcmp(argv[i], "-vv")) rt_verbose = 3;
        else if (!strcmp(argv[i], "--trace")) rt_trace_on = 1;
        else if (!strcmp(argv[i], "--iso") && i + 1 < argc) iso = argv[++i];
        else if (!strcmp(argv[i], "--press") && i + 1 < argc) display_set_presses(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            for (char *p = argv[++i]; *p && nshots < 64; p++) {     /* --shot 300,600: vertical blanks */
                shots[nshots++] = (int)strtol(p, &p, 10);
                if (!*p) break;
            }
        }
        else elf = argv[i];
    }
    if (cdvd_open(iso)) {
        fprintf(stderr, "cannot open the disc image %s (use --iso <file>)\n", iso);
        return 1;
    }
    rt_functab = game_functab;
    rt_functab_count = game_functab_count;
    memory_init();
    if (memory_load_elf(elf, &entry)) {
        fprintf(stderr, "cannot load %s\n", elf);
        return 1;
    }
    rt_log("main", "loaded %s, entry %08X, %d functions", elf, entry, rt_functab_count);
    perf_calibrate();
    prof_start();
    {
        extern int gpu_on;
        const char *r = getenv("MSSAGA_GS");
        gpu_on = !(r && !strcmp(r, "sw"));
    }
    pthread_t game;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setstacksize(&a, 64 << 20);     /* deep guest call chains run on the host stack */
    pthread_create(&game, &a, game_main, NULL);
    platform_thread_name(game, "game");
    if (nshots) return display_shots(shots, nshots);
    return display_window();
}
