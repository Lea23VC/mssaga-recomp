# Status

## Milestone 1 — the executable translates and compiles (2026-11-24)

`./build.sh` (needs `disc/SLUS_212.70`) runs four steps, all output in `build/` and `asm/` (ignored by git):

1. `tools/symbols.py` → `build/symbol_addrs.txt`: 5,734 names from the executable's SN `SNR1` table.
2. splat 0.50.1 with `config/slus_212.70.yaml` → `asm/text.s`: **6,882 functions**, 1,958 of them without a
   name (statics). Compiler setting `EEGCC`, `$gp = 0x3D0870`, sections from the ELF's section headers.
3. `tools/recomp/recomp.py` → `build/recomp/rc_000.c … rc_017.c`, `functab.c/.h`, `report.txt`.
4. gcc `-O1` → `build/obj/*.o`: **all 19 files compile** (~40 s on 4 cores). 7,736 `F_` functions are defined
   (functions + 854 extra entry points), none missing.

Every instruction translates. Report: 1,361 tail calls, 854 extra entry points, 22 DLL imports, 2 functions that
fall through into the next one. `rt_fell_off` remains at the end of a few functions that never return (crt0's
`0x100000` nops, the `ExitThread` root at `0x100250`, three kernel-call wrappers around `0x284100–0x284360`).

### Changes to the reference's recompiler (`tools/recomp/recomp.py`)

- File offset, load address and the jump-table data range (`.data` … end of `.sdata`) are read from the ELF.
- **`jal 0` = SN DLL import**: the 22 sites in `0x1006E0–0x1020E8` (`TITLE_StartTitle`, `FMMF_FMapMainBoot`…)
  are patched in memory by `snDllLoaded`, so the translation reads the instruction word back at run time:
  `rt_call(c, (rd32(c, pc) & 0x3FFFFFF) << 2)`.
- **`bc0t`/`bc0f`** (256 sites, mostly RenderWare waiting for DMA) → `rt_cpcond0(c)`. The runtime must answer
  from the DMAC (CPCOND0 = the channels selected in `D_PCR` are done); with instant DMA it can return 1.
- New MMI: `paddsh` (192 sites), `pexew` (46); `teq` (1, gcc's divide-by-zero trap) → `rt_break`.
- The HLE list (`port/hle.txt`) may be missing: there is none yet.

### What the runtime must provide (milestone 2)

The objects need: `rt_ram`, `rt_spr`, `rt_mmio_read/write(128)`, `rt_call`, `rt_jump`, `rt_syscall`, `rt_break`,
`rt_mfc0/mtc0`, `rt_ei/di`, `rt_poll`, `rt_irq_pending`, `rt_enter`, `rt_trace_on`, `rt_fell_off`,
`rt_cpcond0`, `vu0_exec`, `vu0_cfc2`, `vu0_ctc2`. All exist in the reference's `port/runtime` + `port/kernel`
except `rt_cpcond0`. `port/runtime/r5900.h` is the reference's, with the additions above.

## Not done yet: the DLLs

Most of the game is in the 11 SN DLLs inside the AFS (docs/recon.md), and they are not translated yet. Plan:

1. A tool that extracts and decompresses them from the ISO at build time (`afs.py` + `sd0.py`, into `build/`).
2. Measure where the game puts them (log `snDllLoaded`'s arguments once the core boots, or in PCSX2) and which
   are resident together.
3. Recommended: give each DLL a fixed address, relocate it there with our own SNR1 linker (types 2/4/5/6),
   and have the loader HLE (`SYS_DLLMain`/`snDllLoaded`) put it at that address. Then `recomp.py` translates each
   DLL like the ELF, with its own function table, and `rt_call` finds DLL functions by address.
   Function boundaries: the DLLs' own export symbols + the same `jal`/`jr ra` scan.
