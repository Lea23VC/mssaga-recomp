/*
 * MS Saga: turbo, a speed multiplier for gameplay.
 *
 * The game advances once per vertical blank and counts everything in them (walking, battle animations, menus), so
 * it runs faster when the vertical blanks come faster: the kernel's timer (kernel.c) fires `turbo_speed()` times as
 * often. Music and voices (CRI ADX, cri_adxi.c) and the sound driver (port/iop/lle) keep their own real-time clocks:
 * they don't speed up; sound effects only come sooner. Movies play at their own speed.
 *
 * Tab switches it (kept in the settings folder, file `turbo`); it applies only on a map or in a battle (the DLL
 * loaded), never while a movie plays, and only while the music plays alone (no voice playing: the game is changing
 * tracks, entering a battle; two or more: a voice or a jingle over the music, a voiced scene), and never in a
 * cutscene (its letterbox bars on screen: gpu_letterboxed). MSSAGA_TURBO=<n>: the multiplier (2 if not given, 2..4).
 */
#include <stdio.h>
#include <stdlib.h>

#include "r5900.h"
#include "runtime.h"

void display_config_path(char *p, size_t n, const char *name);     /* display.c */
int game_in_battle(void);                                           /* discord.c */
int game_on_map(void);                                              /* discord.c */
int movie_active(void);                                             /* hle/movie.c */
int cri_voices_playing(void);                                       /* iop/cri_adxi.c */
int gpu_letterboxed(void);                                          /* gs/gpu_vk.c */

static int on = -1, factor;

static void load(void) {
    const char *e = getenv("MSSAGA_TURBO");
    factor = e ? atoi(e) : 2;
    if (factor < 2) factor = 2;
    if (factor > 4) factor = 4;
    char path[1100], buf[8] = "0";
    display_config_path(path, sizeof path, "turbo");
    FILE *f = fopen(path, "r");
    if (f) { if (!fgets(buf, sizeof buf, f)) buf[0] = '0'; fclose(f); }
    on = atoi(buf) != 0;
    if (getenv("MSSAGA_TURBO_ON")) on = atoi(getenv("MSSAGA_TURBO_ON")) != 0;   /* scripted tests */
}

/* Tab (display.c) */
void turbo_toggle(void) {
    if (on < 0) load();
    on = !on;
    char path[1100];
    display_config_path(path, sizeof path, "turbo");
    FILE *f = fopen(path, "w");
    if (f) { fprintf(f, "%d\n", on); fclose(f); }
    rt_log("turbo", "turbo %s (x%d on maps and in battles)", on ? "on" : "off", factor);
}

int turbo_enabled(void) {
    if (on < 0) load();
    return on;
}

/* the kernel's timer, each vertical blank: how many times faster they come now */
int turbo_speed(void) {
    if (on < 0) load();
    if (!on || movie_active() || !(game_in_battle() || game_on_map())) return 1;
    /* only while the music plays alone: none playing is the game changing tracks (entering a battle: at double speed
       its wait for the battle theme ran out and the theme never started), two or more is a voice or a jingle on top
       (a voiced scene): normal speed for those */
    if (cri_voices_playing() != 1) return 1;
    if (gpu_letterboxed()) return 1;                    /* a cutscene (letterbox bars): its own pace */
    return factor;
}
