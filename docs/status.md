# Status

## The DLLs are translated and run (2026-11-24)

`./build.sh` now also extracts the 11 DLLs from the ISO (`tools/dlls.py`: AFS + SD0 → `build/dll/bin/`) and
translates each one with `tools/recomp/recomp.py --dll` → `build/dll/c/`: **7,345 functions, 3.6 MB of code**,
with no instruction left untranslated and no branch to an unknown place. A full build takes ~3 min on 4 cores.

At run time `title_dll` loads at 0x516A80 and its code runs: `__do_global_ctors`, the global data from the disc
(`GLB_InitGlbData` … `GLB_StartGameData`), `TITLE_StartTitle`, the memory card check (`MCK_StartMcCheck`,
`MCW_OpenMcWindow`), drawing through `B3D_*` and RenderWare. Nothing is shown yet (the GS is a stub). The game
is presumably waiting at the memory card window (the `mcserv` server answers zeros); that is the next thing to look at.

How the DLLs are translated (position-independent, as planned below):
- **Code range:** 0x100 to the last `jr $ra` + 8. Function starts are the exports, local `jal` targets, and the
  instruction after a `jr $ra` when no earlier branch of the function reaches past it. (An earlier rule, "always
  after a `jr $ra`", cut functions at early returns: 70 false tail calls in `title` and 7 branches into nothing.)
- **Relocations are RELA:** the instruction fields are 0 and each entry carries its full addend. HI16/LO16 sites read
  the immediate the loader wrote back from guest memory (30,750 sites; measure before making them `base + const`).
- **Calls:** local R26 → a direct C call. An import whose name is the ELF's → a direct call to `F_…`
  (18,873). Another DLL's export → the target the loader wrote into the instruction (2,119). Imports nobody
  defines (`RwMatrixGetPos`, `FIFO_PrintInfo`, … debug helpers missing from the release ELF) also go that way and
  would stop with "not a known function" if ever called.
- **Run time:** `port/hle/dll.c` hooks `snDllLoaded` (before): size + CRC32 of the image as it arrived say which
  DLL it is, and `rt_dll_register` (cpu.c) puts its function table at that base (it replaces older DLLs it
  overlaps; `snDllUnload` needs the table for the destructors, so nothing is removed there). `rt_find` /
  `rt_name_of` look in the loaded DLLs after the ELF.

## Milestone 2 — boots to the first block (2026-11-24)

`./build.sh` now also links `build/mssaga` (runtime, kernel and IOP services from SaintSeiyaRecomp, MIT).
`./build/mssaga [--iso disc/game.iso] [-v|-vv] [--trace]` runs, with no window, sound or picture yet:

1. crt0, `SetupThread`/`SetupHeap`, libkernl's `SetSyscall` patches, the IOP reboot (`IOPRP280.IMG`) and all 12
   modules "loaded" (`LoadModule` is answered; nothing runs on the IOP).
2. Pads opened (none connected), memory card server, libsd, the sound driver `RPGSND` (server 0x7073,
   `port/iop/rpgsnd.c`, stub), CRI ADX's threads and its IOP half (`port/iop/cri_adxi.c`, no audio device).
3. The disc: `sceCdSearchFile`/`sceCdLayerSearchFile` find `\0FLISTDV.DIR` and `\AFSLIST0.AFS` in the ISO
   (`port/hle/cdvd.c` + `port/hle.txt`, 13 replaced functions).
4. ~200 vertical blanks (4 s) later RenderWare starts: `SetGsCrt(interlaced, NTSC)`, GS registers, DMA
   handlers, VIF1/GIF transfers (not carried out: `port/stubs.c`). Why the wait is that long is unknown.
5. **First block: the DLLs.** `SYS_DLLMain` (called from `main`'s loop) loads `title_dll` at **0x516A80**, inside
   `dll_buf` (0x516A00 in `.bss`), and `snDllLoaded` calls its `__do_global_ctors` (base + 0x16768). No translated
   code exists there, so `rt_call` stops: `call to 0052D1E8, which is not a known function (from snDllLoaded)`.

What was needed for MS Saga besides the reference's code: the `RPGSND` stub (`SND_Init` waits until functions
0 and 0xE return non-zero), `sceCdLayerSearchFile` (CRI's file system uses it, not `sceCdSearchFile`), 8 MB of
IOP memory in the HLE (the game allocates 256 KB sound buffers), and `cri_adxi.c` built without SDL3
(`MSSAGA_SDL3` turns sound on; SDL3 is not installed in this WSL yet).

### Where the DLLs go (measured)

`dll_buf` is the last symbol in `.bss`, and `_end` is 0x115D000, so it is a 12.5 MB static pool (0x516A00 to
0x115D000) that the game's `FIFO_*` allocator divides at run time. `title_dll` is the first block of it
(+0x80). Other DLLs will land at addresses that depend on what was allocated before them. So option (a) from
recon.md, fixed addresses, would need us to control the allocator. **Option (b) now looks better**: translate
each DLL position-independently.

- `snDllLoaded` runs as it is and applies the relocations in guest memory, so data (jump tables, vtables) is
  right without help.
- The translated code adds the base where the relocation table says an address is built: HI16/LO16 pairs. R_MIPS_26
  calls inside the DLL become direct C calls. Imports (symbol value 0) are resolved by name against the ELF's table.
- When a DLL is loaded, its function table is registered at its base. Then `rt_call` maps a guest address back
  to the right DLL function, and the 22 `jal 0` imports in the ELF (already read from memory) work.

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

## The DLLs: the first plan (superseded)

Written before measuring, kept for the record (see above for what was done):

1. A tool that extracts and decompresses them from the ISO at build time (`afs.py` + `sd0.py`, into `build/`).
2. Measure where the game puts them (log `snDllLoaded`'s arguments once the core boots, or in PCSX2) and which
   are resident together.
3. Recommended: give each DLL a fixed address, relocate it there with our own SNR1 linker (types 2/4/5/6),
   and have the loader HLE (`SYS_DLLMain`/`snDllLoaded`) put it at that address. Then `recomp.py` translates each
   DLL like the ELF, with its own function table, and `rt_call` finds DLL functions by address.
   Function boundaries: the DLLs' own export symbols + the same `jal`/`jr ra` scan.
