/*
 * Stand-ins for the parts of the port that don't exist yet (milestone 2: boot up to the first block).
 * Each logs what the game asks of it once, so the log shows what to build next.
 *
 *   DMAC/GS (port/gs later): a transfer started by writing CHCR.STR finishes at once, without being
 *     carried out, and raises the channel's interrupt; nothing is drawn.
 *   Pads: none connected.
 */
#include <stdio.h>
#include <string.h>

#include "r5900.h"
#include "runtime.h"
#include "gs.h"
#include "pad.h"

void kernel_raise_dmac(int ch);           /* kernel.c */

/* ---- DMAC: 0x10008000-0x1000EFFF. Channel registers at 0x1000{8,9,A,B,B4,C,C4,C8,D,D4}000; CHCR first,
 * STR (bit 8) starts a transfer. D_STAT (0x1000E010): bits 0-9 channel done, writing 1 clears. */
static u32 regs[0x7000 / 4];
static const u32 chan_base[10] = {0x8000, 0x9000, 0xA000, 0xB000, 0xB400, 0xC000, 0xC400, 0xC800, 0xD000, 0xD400};

static int channel_of(u32 off) {
    for (int i = 0; i < 10; i++) if (off == chan_base[i]) return i;
    return -1;
}

u32 dmac_read(u32 a) { return regs[((a & 0xFFFF) - 0x8000) / 4]; }

int dmac_write(Ctx *c, u32 a, u32 v) {
    (void)c;
    u32 off = a & 0xFFFF, *r = &regs[(off - 0x8000) / 4];
    if (off == 0xE010) {                    /* D_STAT: clear done bits (0-15), toggle mask bits (16-31) */
        *r = (*r & ~(v & 0xFFFF)) ^ (v & 0xFFFF0000);
        return 1;
    }
    int ch = channel_of(off);
    *r = v;
    if (ch >= 0 && (v & 0x100)) {
        static u16 told;
        if (!(told & (1 << ch))) { told |= 1 << ch; rt_log("dmac", "first transfer on channel %d (CHCR %08X): not carried out", ch, v); }
        *r &= ~0x100u;                      /* done at once */
        regs[(0xE010 - 0x8000) / 4] |= 1u << ch;
        kernel_raise_dmac(ch);
    }
    return 1;
}

int dmac_gs_busy(void) { return 0; }
void dmac_gs_flip(void) {}
void gs_set_crt(u32 interlace, u32 mode, u32 field) { (void)interlace; (void)mode; (void)field; }

/* COP0 condition (bc0f/bc0t loops): every DMA transfer is already done. */
int rt_cpcond0(Ctx *c) { (void)c; return 1; }

/* ---- pads: none */
volatile u16 pad_buttons[2];
volatile u8 pad_sticks[2][4];
volatile int pad_second;
void pad_open(u32 port, u32 slot, u32 ee_area) { rt_log("pad", "open port %u slot %u area %08X", port, slot, ee_area); }
void pad_vblank(Ctx *c) { (void)c; }

/* ---- crash reports: no heap bookkeeping yet */
void heap_explain(const char *what, u32 addr) { (void)what; (void)addr; }
