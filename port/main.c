/*
 * MS Saga: A New Dawn, native: entry point.
 *
 *     mssaga [SLUS_212.70] [--iso game.iso] [-q | -v | -vv] [--trace]
 *            [--shot 300,600,...] [--press "1100:CROSS,..."]
 *     mssaga --gen-vu dir       (the build) the game's VU1 programs as C into dir, then exit
 *
 * Loads the executable into guest RAM (its code is the recompiled C linked into this program; the image is still
 * needed for its data, and because the SN DLL loader patches instructions in it), and runs the main thread from
 * the ELF entry point on a thread of its own; this thread shows the picture (port/gs/display.c), or with --shot
 * saves the pictures at those vertical blanks (build/shot_N.ppm) without a window. MSSAGA_GS=sw draws with the
 * software GS instead of Vulkan.
 *
 * Finding the files (so a double-click works): the executable from the command line, else disc/SLUS_212.70 in the
 * current folder, next to the program, or one folder up (build/mssaga in the repository). The disc image from --iso,
 * else $MSSAGA_ISO, else the one chosen last time (iso.txt in the settings folder), else disc/game.iso in those
 * same places, else a file picker asks for it (and the choice is kept in iso.txt). A start-up error is also shown
 * in a message box: started from Windows' Explorer, the console window closes at once.
 *
 * Structure from SaintSeiyaRecomp's port/main.c (MIT, Copyright (c) 2026 Matheus Nunes Pereira;
 * licenses/SaintSeiyaRecomp-MIT.txt).
 */
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

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
int vu_jit_generate(const char *dir);     /* gs/vu_jit.c */

static u32 entry;

static void *game_main(void *arg) {
    (void)arg;
    kernel_init();
    kernel_run(entry);
    return NULL;
}

/* A start-up error: on stderr, and in a message box (a double-clicked program's console closes at once). */
static int fail(const char *fmt, ...) {
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "MS Saga: A New Dawn", msg, NULL);
    return 1;
}

static int readable(const char *path) {
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* name (e.g. "disc/SLUS_212.70") in the current folder, next to the program, or one folder up; NULL if none */
static const char *find_file(const char *name) {
    static char paths[2][1100];
    static int k;
    char *out = paths[k++ & 1], exe[1024];
    if (readable(name)) return name;
    if (platform_exe_dir(exe, sizeof exe)) return NULL;
    snprintf(out, 1100, "%s/%s", exe, name);
    if (readable(out)) return out;
    snprintf(out, 1100, "%s/../%s", exe, name);
    if (readable(out)) return out;
    return NULL;
}

/* the settings folder's iso.txt: the disc image chosen last time */
static void iso_memory(char *out, size_t n) {
    char dir[1024];
    platform_data_dir(dir, sizeof dir);
    snprintf(out, n, "%s/iso.txt", dir);
}

static char picked[1024];
static int picking;

static void SDLCALL on_pick(void *ud, const char *const *files, int filter) {
    (void)ud;
    (void)filter;
    if (files && files[0]) snprintf(picked, sizeof picked, "%s", files[0]);
    picking = 0;
}

/* asks for the disc image with a file picker; NULL if none was chosen */
static const char *ask_iso(void) {
    if (!SDL_Init(SDL_INIT_VIDEO)) return NULL;
    static const SDL_DialogFileFilter filters[] = {{"Disc image (MS Saga: A New Dawn, USA)", "iso"}, {"All files", "*"}};
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "MS Saga: A New Dawn",
                             "Choose your disc image of MS Saga: A New Dawn (USA, SLUS-21270).\n"
                             "It will be remembered for next time.", NULL);
    picking = 1;
    SDL_ShowOpenFileDialog(on_pick, NULL, NULL, filters, 2, NULL, false);
    while (picking) {
        SDL_Event e;
        SDL_WaitEventTimeout(&e, 50);
    }
    return picked[0] ? picked : NULL;
}

/* the disc image, as described at the top; remembers a picked one */
static const char *find_iso(const char *given) {
    static char kept[1024];
    if (given) return given;
    const char *env = getenv("MSSAGA_ISO");
    if (env && *env) return env;
    char mem[1100];
    iso_memory(mem, sizeof mem);
    FILE *f = fopen(mem, "r");
    if (f) {
        if (fgets(kept, sizeof kept, f)) {
            kept[strcspn(kept, "\r\n")] = 0;
            if (readable(kept)) { fclose(f); return kept; }
        }
        fclose(f);
    }
    const char *local = find_file("disc/game.iso");
    if (local) return local;
    const char *p = ask_iso();
    if (p && (f = fopen(mem, "w"))) {
        fprintf(f, "%s\n", p);
        fclose(f);
    }
    return p;
}

int main(int argc, char **argv) {
    /* SDL's SIGINT/SIGTERM handlers only queue a quit event, which --shot never reads: `timeout` and Ctrl+C
     * could not stop a test run. Keep the default signals (the window still closes with its close button). */
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    const char *elf = NULL, *iso = NULL, *gen_vu = NULL;
    int shots[64], nshots = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-q")) rt_verbose = 0;
        else if (!strcmp(argv[i], "-v")) rt_verbose = 2;
        else if (!strcmp(argv[i], "-vv")) rt_verbose = 3;
        else if (!strcmp(argv[i], "--trace")) rt_trace_on = 1;
        else if (!strcmp(argv[i], "--iso") && i + 1 < argc) iso = argv[++i];
        else if (!strcmp(argv[i], "--gen-vu") && i + 1 < argc) gen_vu = argv[++i];
        else if (!strcmp(argv[i], "--press") && i + 1 < argc) display_set_presses(argv[++i]);
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) {
            for (char *p = argv[++i]; *p && nshots < 64; p++) {     /* --shot 300,600: vertical blanks */
                shots[nshots++] = (int)strtol(p, &p, 10);
                if (!*p) break;
            }
        }
        else elf = argv[i];
    }
    if (!elf && !(elf = find_file("disc/SLUS_212.70")))
        return fail("The game's executable (disc/SLUS_212.70) was not found next to the program.\n"
                    "Build the port again (it copies it there), or give its path on the command line.");
    if (gen_vu) {                                 /* the build: only the executable is needed */
        memory_init();
        if (memory_load_elf(elf, &entry)) { fprintf(stderr, "cannot load %s\n", elf); return 1; }
        return vu_jit_generate(gen_vu);
    }
    if (!(iso = find_iso(iso)))
        return fail("No disc image was chosen. Start the program again, or give it with --iso <file>.");
    if (cdvd_open(iso)) {
        char mem[1100];
        iso_memory(mem, sizeof mem);
        remove(mem);                              /* ask again next time */
        return fail("Cannot read the disc image:\n%s\n\nStart the program again to choose another one.", iso);
    }
    rt_functab = game_functab;
    rt_functab_count = game_functab_count;
    memory_init();
    if (memory_load_elf(elf, &entry)) return fail("Cannot load the game's executable:\n%s", elf);
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
