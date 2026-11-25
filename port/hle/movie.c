/*
 * The game's movies (CRI Sofdec, .sfd in the AFS), skipped for now.
 *
 * Sofdec decodes MPEG-2 with the PS2's IPU, which the port doesn't have, so a movie never shows a frame nor ends:
 * after NEW GAME the opening movie left the screen black forever. MOVIE_PlayStart (0x1283F8) is replaced: like
 * the game's, it takes the first of the 4 movie slots (D_004EF680, 0x28 bytes each, state in byte 0) that is free
 * (0) or ended (3), but marks it ended at once and returns its number. Callers poll MOVIE_GetStat(slot) until
 * it says 3 or 4 (title_dll func_014970), so they go on as if the movie had played; MOVIE_Main only works on
 * slots playing a picture (byte 1 = 1), which this one isn't.
 * Later: decode the .sfd with FFmpeg and show it, as SaintSeiyaRecomp's port/hle/movie.c does.
 */
#include "r5900.h"
#include "runtime.h"

#define MOVIE_SLOTS 0x004EF680u    /* D_004EF680 */

void hle_MOVIE_PlayStart(Ctx *c) {
    int slot = 0;
    while (slot < 4) {
        u8 st = rd8(c, MOVIE_SLOTS + 0x28 * slot);
        if (st == 0 || st == 3) break;
        slot++;
    }
    rt_log("movie", "MOVIE_PlayStart(%08X, %08X, %08X, %08X): skipped (no IPU / movie player yet), slot %d", RW(4), RW(5),
           RW(6), RW(7), slot);
    if (slot == 4) slot = 0;
    wr8(c, MOVIE_SLOTS + 0x28 * slot, 3);          /* ended */
    wr8(c, MOVIE_SLOTS + 0x28 * slot + 1, 0);      /* no picture */
    SET64(2, slot);
}

/* the movie frame to show instead of the game's picture (port/gs/display.c): none yet */
int movie_take(u32 *out, int max, int *w, int *h, u32 *gen) { (void)out; (void)max; (void)w; (void)h; (void)gen; return 0; }
