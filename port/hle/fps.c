/*
 * Frame rate. The game runs at 30 frames a second because it tells RenderWare, once at start-up
 * (B3D_VsyncCntSet(2) from 0x1006C8), to wait at least 2 vertical blanks per frame:
 * _rwDMAMinVsyncCntSet (0x1C73E8) keeps the count in a byte (D_003C941C). The game measures each frame's
 * length in vertical blanks (B3D_Main: D_0044312C, read through B3D_GetVSyncInterval by the field, town and
 * battle code), so much of it keeps its speed at another rate.
 *
 * The game's own task scheduler also wakes its main loop every N vertical blanks: SYS_VsyncWakeUp(N) (0x10F158,
 * from 0x1006BC) keeps N in D_00442CC8, which the vertical blank handler (func_0010F800) and a wait
 * (func_0010FE60) honour.
 *
 * MSSAGA_FPS=60: both minimums become 1 (an experiment: what still counts frames runs twice as fast).
 */
#include <stdlib.h>

#include "r5900.h"
#include "runtime.h"

#define RW_MIN_VSYNC_CNT 0x003C941Cu   /* D_003C941C */
#define SYS_VSYNC_VAL    0x00442CC8u   /* D_00442CC8 */

static int fps60(void) {
    static int on = -1;
    if (on < 0) { const char *e = getenv("MSSAGA_FPS"); on = e && atoi(e) >= 60; }
    return on;
}

void hle_rwDMAMinVsyncCntSet(Ctx *c) {
    u8 n = (u8)RW(4);
    if (fps60() && n > 1) n = 1;
    rt_log("fps", "RenderWare: at least %u vertical blank(s) per frame (game asked %u)", n, RW(4) & 0xFF);
    wr8(c, RW_MIN_VSYNC_CNT, n);
}

void hle_SYS_VsyncWakeUp(Ctx *c) {
    u32 n = RW(4);
    if (fps60() && n > 1) n = 1;
    rt_log("fps", "main loop: every %u vertical blank(s) (game asked %u)", n, RW(4));
    wr32(c, SYS_VSYNC_VAL, n);
}
