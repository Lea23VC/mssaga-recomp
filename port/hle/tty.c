/* From SaintSeiyaRecomp port/hle/tty.c (MIT, Copyright (c) 2026 Matheus Nunes Pereira;
 * licenses/SaintSeiyaRecomp-MIT.txt). Unchanged. */
/* Debug console output of the SDK, sent to stderr. */
#include <stdio.h>

#include "r5900.h"
#include "runtime.h"

/* (const char *buf, int len) -> len */
void hle_tty_write(Ctx *c) {
    u32 buf = RW(4);
    s32 len = RSW(5);
    for (s32 i = 0; i < len; i++) fputc(rd8(c, buf + i), stderr);
    SET64(2, len);
}
