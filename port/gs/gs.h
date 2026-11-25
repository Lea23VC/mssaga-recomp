/* The GS / DMAC side the runtime and kernel call into. Milestone 2: port/stubs.c answers with an idle DMAC
 * and no picture; SaintSeiyaRecomp's port/gs/ (DMAC -> VIF -> VU1 -> GS -> Vulkan) replaces it at milestone 4. */
#pragma once
#include "r5900.h"

u32 dmac_read(u32 a);
int dmac_write(Ctx *c, u32 a, u32 v);   /* 1 = handled (not stored as plain memory) */
int dmac_gs_busy(void);
void dmac_gs_flip(void);
void gs_set_crt(u32 interlace, u32 mode, u32 field);
