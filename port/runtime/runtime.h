/* From SaintSeiyaRecomp port/runtime/runtime.h (MIT, Copyright (c) 2026 Matheus Nunes Pereira;
 * licenses/SaintSeiyaRecomp-MIT.txt). Changes for MS Saga are noted where they are. */
/* Services shared by the runtime's parts (not used by the recompiled code). */
#pragma once
#include "r5900.h"

_Noreturn void rt_fatal(Ctx *c, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void rt_log(const char *area, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
extern int rt_verbose;

const FuncEntry *rt_find(u32 addr);
/* cpu.c: a translated DLL now lies at base (port/hle/dll.c); replaces any other one overlapping it */
void rt_dll_register(u32 base, const DllModule *m);
const char *rt_name_of(u32 addr);

/* hle/heap.c: what a crash report says about an address in a freed block (SSTHEH_POISON_FREE) */
void heap_explain(const char *what, u32 addr);

/* kernel.c */
void kernel_init(void);
void kernel_run(u32 entry);
u32 kernel_vblank_count(void);

/* memory.c */
void memory_init(void);
int memory_load_elf(const char *path, u32 *entry);
