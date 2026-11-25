/*
 * RPGSND.IRX, the game's own sound driver on the IOP (RPC server 0x7073), stubbed (milestone 2).
 *
 * The EE side is SND_* in the executable (SND_Init 0x111998, SND_ExecRPC 0x111498): a call whose function
 * number has bit 31 set waits for the answer, whose first word is the result. SND_Init calls functions 0 and
 * 0xE in a loop until the result is non-zero and keeps it (D_00443088), so it is taken to be an address in IOP
 * memory (a deduction: what the driver does with it is still to read in RPGSND.IRX). Every call is logged once
 * per function number, so the log shows what the driver must do when it is written for real (milestone 6).
 */
#include <stdio.h>
#include <string.h>

#include "r5900.h"
#include "runtime.h"
#include "iop.h"

#define RPGSND_BUFFER 0x001C0000u   /* IOP memory handed out as the driver's work area (free in the HLE:
                                       cri_adxi.c hands out 0x170000-0x19FFFF) */

static u32 srv_rpgsnd(Ctx *c, u32 fn, u8 *in, u32 in_size, u8 *out, u32 out_size) {
    (void)c;
    static u8 told[64];
    u32 f = fn & 0x7FFFFFFF, arg = 0;
    if (in_size >= 4) memcpy(&arg, in, 4);
    if (f < sizeof told && !told[f]++)
        rt_log("rpgsnd", "fn %s%X (arg %08X, %u bytes in, %u out)", fn >> 31 ? "wait " : "", f, arg, in_size, out_size);
    if (out_size < 4) return 0;
    u32 result = (f == 0x0 || f == 0xE) ? RPGSND_BUFFER : 0;
    memset(out, 0, out_size < 16 ? out_size : 16);
    memcpy(out, &result, 4);
    return out_size < 16 ? out_size : 16;
}

void rpgsnd_init(void) {
    iop_register_server(0x00007073, "rpgsnd", srv_rpgsnd);
}
