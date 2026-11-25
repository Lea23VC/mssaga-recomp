# Reconnaissance

Everything here was measured on the USA disc (`SLUS-21270`, `SYSTEM.CNF`: `BOOT2 = cdrom0:\SLUS_212.70;1`,
`VER = 1.01`, `VMODE = NTSC`). Deductions say so. Tools in `tools/recon/` (run them with `.venv/bin/python`;
`.venv` has splat, spimdisasm, rabbitizer, pyelftools — the same set as the reference's `tools/setup.sh`).

## Summary (milestone 0, 2026-11-24)

- `SLUS_212.70` (SHA-1 `b23d39e5520047ca2a14c43d8f8ce36f1394764b`) is a normal ELF **with section headers**,
  built with the **SN Systems ProDG** toolchain (GNU binutils + SN's ee-gcc; see *Compiler*).
- **It carries its own symbol table**: `.sndata` is an SN `SNR1` DLL export table with **5,764 named symbols**,
  4,925 of them in `.text` (`main`, `SYS_*`, `B3D_*`, all of RenderWare, CRI, Sony). Static functions are not in it.
- **Most of the game's code is not in the executable.** It lives in 11 compressed SN DLLs inside `AFSLIST0.AFS`
  (`title_dll.cmp`, `fmap_dll.cmp`, `sento_dll.cmp`…), about **11.5 MB decompressed (code + data)** against 1.5 MB of `.text`
  in the ELF, and they are loaded at an address chosen at run time. **This is the biggest difference from Saint
  Seiya** and changes milestone 1: the recompiler must also translate these DLLs (see *Code loaded at run time*).
- Engine: **RenderWare 3.6** (PS2 "sky2" driver) wrapped by the game's `B3D_*` layer. VU1 microcode comes from
  RenderWare: ~90 programs in `.data`.
- CRI middleware (ADX, Sofdec/MWSFD) built Apr 19 2005, Sony SDK 2.8 libraries — same families as Saint Seiya.

## Disc layout

| File | Size | Notes |
|---|---|---|
| `SLUS_212.70` | 2,944,224 | The executable (ELF). Only one: no `BIN/` folder of code files |
| `AFSLIST0.AFS` | 1,606,543,360 | All game data, CRI AFS archive, 25,831 entries **with names** (see *AFS*) |
| `DMY.BIN` | 1,048,576 | Dated 2001, not zero-filled (1,044,419 non-zero bytes). Disc padding (deduction) |
| `0FLISTDV.DIR` | 14 | The text `AFSLIST0.AFS\r\n`: the list of AFS files, read by CRI's DVCI file system (deduction) |
| `IOP/IOPRP280.IMG` | 269,889 | IOP replacement image, Sony SDK 2.8 (same as Saint Seiya) |
| `IOP/CRI_ADXI.IRX` | 75,189 | CRI ADX audio on the IOP (same as Saint Seiya) |
| `IOP/RPGSND.IRX` | 4,844 | Game's own sound module (BEC?). Small: probably a thin RPC layer over the Sony modules below |
| `IOP/MODHSYN.IRX`, `MODMIDI.IRX`, `MODMSIN.IRX`, `SDRDRV.IRX` | 61,069 / 21,965 / 1,929 / 9,161 | Sony's standard sound libraries: hardware synth, MIDI sequencer, MIDI input, SD RPC driver |
| `IOP/LIBSD.IRX` | 28,605 | Sony low-level sound |
| `IOP/CDVDSTM.IRX` | 34,705 | CD/DVD streaming |
| `IOP/PADMAN.IRX`, `SIO2MAN.IRX`, `MCMAN.IRX`, `MCSERV.IRX` | | Pad and memory card (same family as Saint Seiya) |

- The executable loads exactly these 12 IRX plus `IOPRP280.IMG`, by `cdrom0:\IOP\<NAME>.IRX;1` strings. No
  `host0:` use except one leftover debug string (`HST:host0:..\..\PcGame.bin`).
- No Metrowerks `MWo3` overlay files, no ELF files anywhere (disc files and AFS entries checked).

## ELF

- 32-bit little-endian, `e_flags 0x20924001` (noreorder, 5900, eabi64, mips3). Entry `0x100008` (`_start`).
- One `PT_LOAD`: file offset `0x1000` → `0x100000`, 0x2C962C bytes of file, 0x626A00 of memory (ends `0x726A00`).
  File offset = address − `0xFF000`.
- `$gp` = **`0x3D0870`** (`.reginfo`, and symbol `_gp`). No `.comment`, no `.symtab`.

| Section | Address | Size | Contents |
|---|---|---|---|
| `.text` | `0x100000` | 0x185290 | all code (ends `0x285290`) |
| `.data` | `0x285300` | 0x0FFD30 | data; **VU1 microcode** `0x28D2CC–0x385000`; libkernl kernel-mode code `0x37FD70–0x381360` (`SysExecPS2Entry`, `_kTLBRefillHandler`…) — flags `WAX` |
| `.sndata` | `0x385080` | 0x02E294 | SN `SNR1` table: imports, symbols, names (symbol `sn_dll_header_root`) |
| `.rodata` | `0x3B3380` | 0x0154C8 | |
| `.lit4`, `.sdata` | `0x3C8880`, `0x3C8E80` | 0x588, 0x7AC | `$gp`-relative |
| `.sbss`, `.bss` | `0x3C9680`, `0x3C9980` | 0x284, 0x35D080 | `_fbss = 0x3C9680` |
| `.DVP.overlay…` ×17, `.DVP.ovlytab`, `.DVP.ovlystrtab` | not loaded | | VU code sections from Sony's `dvp-as`. **Zero-filled in the file**; the real code is in `.data` at the LMA given by `.DVP.ovlytab` (0x381370…0x384F50, each preceded by its VIF `MPG` tag) |

### crt0 (`0x100008`) and start-up

1. Clears all GPRs (`padduw r, zero, zero`: 128-bit), HI/LO/HI1/LO1, SA, FPRs, FCSR.
2. Clears `0x3C9680` (`_fbss`) up to **`0x115D000` (`_end`)**, not just to the end of `.bss` (`0x726A00`): the
   linker's `_end` sits 10 MB higher. Deduction: room reserved after `.bss`, the heap starts there.
3. `SetupThread` (syscall 0x3C): `$gp=0x3D0870`, stack `_stack = 0x1EDD000`, size `_stack_size = 0x100000`, args
   at `0x3C9980`, root `0x100250` (syscall 0x23 `ExitThread`).
4. `SetupHeap` (0x3D): `0x115D000`, size `_heap_size = 0xD80000` → the heap ends at `0x1EDD000`, right below the stack.
5. `_InitSys` (`0x281928`), `FlushCache(0)`, C++ static constructors via `0x1315E0`, `atexit`-like `0x270148`, `ei`,
   `main(argc, argv)` at **`0x10E1C8`**, then `exit` (`0x12EFE0`).
6. `main` calls `0x12EFC0`, `0x10E1F8` (init: interrupts, `SYS_*`), `0x10E618` (the main loop, deduction).

## Compiler

- **SN Systems ProDG** (SN's build of ee-gcc 2.9x): the DLL system is SN's (`snInitDllSystem`, `snDllLoaded`,
  `snDllMove`, `snDllGetFunctionAddress`, `sn_dll_header_root`, `SNR1` files), and the sections are GNU (`.reginfo`,
  `.mdebug.eabi64`, `.DVP.*`). A build-config string `PS2_EE_Release_S…` sits in every DLL.
- Code looks like gcc: moves are `daddu rd, rs, $zero`, `$ra` saved with `sd`. Callee-saved `s0`–`s7` are saved
  with **`sq`** in the game code *and* in RenderWare, with `sd` in Sony and CRI libraries (`tools/recon/funcs.py`).
  The reference's "sq = Metrowerks" rule does **not** apply here: SN's compiler also saves 128-bit registers.
- Function starts: about half aligned to 16, half to 8 (3,397 / 3,368 of 6,919).
- C++ is present (`__do_global_ctors`/`__do_global_dtors` in every DLL, `__sce_eh_sema_id`), the rest looks like C.

## Memory map of `.text`

From the symbol names (`tools/recon/snr_syms.py`) and the save style. "≈" = rounded to the nearest run.

| Address | Contents | Save style |
|---|---|---|
| `0x100000–0x1002F0` | crt0 | — |
| `0x1002F0–0x1328A8` | **game core in the ELF**: `SYS_*` (tasks, DLL manager), `main`, `B3D_*` (RenderWare wrapper, ~`0x110000–0x122000`), `FIFO_*` (memory manager, `d:/projects/GRPG/SYS/FIFO/lib_FIFO.c`), `MOVIE_*`, `PS_Dec*` (decompressors), libgcc soft-float, `snDll*` | sq |
| `0x1328A8–0x13BFC0` | Sony: libpad, libm (`atan2`, `sqrtf`), libcdvd | sd |
| `0x13BFC0–0x1EF1B0` | **RenderWare 3.6**: plugins (prtstd, ptank, matfx, hanim, skin2, dmorph, collis, pvs, pds…), world, core, sky2 driver | sq |
| `≈0x1EF1B0–0x240300` | CRI: ADX (ADXT 9.44s281, ADXF 7.30, ADXPS2 2.60), CVFS 2.39, DVCI 2.71, SJ 6.31, SVM, SRD, LSC… | sd |
| `≈0x240300–0x25F170` | CRI Sofdec (MWSFD 3.50s281, SFD/MPV/M2V 1.958, MPS) + Sony libmpeg/libipu | sd |
| `≈0x25F170–0x285290` | Sony libmc, libgraph, libdma, libvu0, libkernl (syscall stubs `0x278D04–0x2795A0`), newlib (`vfprintf`, malloc), `_InitSys` | mixed |

- Sony SDK: libcdvd, dma, graph, ipu, kernl, mpeg, msin, pad, sdr, vu0 `2800`; libmc `2810`. CRI string
  `GCC2096 SCE2810` (CRI's own build).
- Syscall stubs exist for most kernel calls (147 numbers). The game uses `SetupThread/SetupHeap`, threads
  (`CreateThread` from `InitThread`, `SYS_ThreadCreate`, ADXM's idle thread, libcdvd and libsdr callbacks),
  semaphores (33 callers), INTC handlers (`main`, `B3D_InitSystem`, `DISC_SysInit`, `InitTimer` via
  `AddIntcHandler2`, RenderWare's `_rwDMAHook`), DMAC handlers (`_rwDMAHook`, SIF, Sofdec's CSC). No `SetAlarm`.
  `sceSifLoadElf*` and `LoadExecPS2` are never called. (Caller names are the nearest exported symbol, so a static
  function shows as its exported neighbour.)

## Symbols: the `SNR1` table in `.sndata`

Header at `0x385080`: `"SNR1"`, imports `0x39EB34` ×22, symbols `0x39EC3C` ×0x1684 (5,764), then the string table.
Symbol = 12 bytes `{name address, value, info}`; import = `{address, symbol index << 8 | MIPS reloc type, 0}`.

- **22 imports**: `jal 0` placeholders in the ELF's `.text` (`0x1006E0–0x1020E8`) patched when a DLL loads —
  `GLB_InitGlbData`, `TITLE_StartTitle`, `FMMF_FMapMainBoot`, `BMMF_BMapMainBoot`, `SentoMain_Init`,
  `ENDMOVIE_Start`, `MSF_Setup2DSprite`… This is how the core calls into the DLLs.
- 5,764 symbols: 4,925 in `.text`, 619 `.data`, 76 `.bss`, 63 `.sdata`, 53 `.rodata`, plus `_stack`, `_heap_size`,
  `_end`. The detector in `funcs.py` finds 6,919 function starts, so ~2,000 functions (statics) still need names.
- These names (and the `B3D_*` debug strings) mean splat can start from a real `symbol_addrs.txt`. Generate it
  from the disc at build time; don't commit it until we decide names count as "data from the game".

## Code loaded at run time: the DLLs

The game core registers DLLs (`SYS_RgstDLL`, called from `GAME_Main`) and `SYS_DLLMain` loads one: it reads the
`.cmp` from the AFS, decompresses it, gets memory from the game's `FIFO_*` allocator and calls SN's
`snDllLoaded` to relocate and link it (from the call graph; details still to read). `dllfsize.cmp` holds the
11 decompressed sizes.

| DLL | Decompressed | Exports | Imports | Relocs | What (from names) |
|---|---|---|---|---|---|
| `title_dll` | 0x3CB9C | 171 | 90 | 3,982 | title screen |
| `staff_dll` | 0x4744 | 36 | 25 | 300 | staff roll |
| `bmap_dll` | 0x17DB38 | 637 | 405 | 17,502 | B map (base? `BMMF_*`) |
| `fmap_dll` | 0x161678 | 506 | 458 | 14,496 | field map (`FMMF_*`) |
| `tmap_dll` | 0x17A9E4 | 676 | 394 | 17,888 | T map (town? `TMMF_*`) |
| `sento_dll` | 0x16899C | 582 | 177 | 21,236 | battle (*sentō*, `SentoMain_Init`) |
| `stest_dll` | 0x1FBF28 | 591 | 214 | 29,342 | battle test? shares names with `sento` |
| `sub_dll` | 0x68F34 | 419 | 95 | 7,492 | sub-game / menus (`SUBGAMEN_*`) |
| `haichi_dll` | 0x253D34 | 674 | 228 | 35,926 | placement (*haichi*): unit setup? |
| `emov_dll` | 0x2B034 | 86 | 76 | 2,732 | ending movie |
| `sound_dll` | 0x9544 | 38 | 36 | 745 | sound tables (`sndFileTbl`) |

- **Compression `SD0`**: `"SD0\0"`, u32 file size, u32 decompressed size, then LZ + run-length, decoded by
  `PS_DecSlide` (`0x10D1D0`). `tools/recon/sd0.py` decodes all 11 DLLs to the exact size. The table at `0x3B38A8`
  names two more formats, `CMP0` and `RLN0` (`PS_DecZero*`, `PS_DecRunlen*`); not seen in the AFS so far.
- **DLL format `SNR1`** (decompressed): header `{"SNR1", relocs, n, symbols, n, end of image, __do_global_ctors,
  __do_global_dtors, total size, 0, 0x80, total size}`; image from `0x80`, code from `0x100`; symbols as in the
  ELF (value 0 = import from the core). Relocations: MIPS types 2 (`R_MIPS_32`), 4 (`R_MIPS_26`), 5 (`HI16`),
  6 (`LO16`). So the DLLs are position-independent only through relocations: their code has absolute addresses
  once loaded.
- **Consequence for the port** (decision for milestone 1, not taken yet): either (a) make the loader HLE place each
  DLL at a fixed address of our choosing, relocate it there with our own linker, and recompile it at that base;
  or (b) recompile each DLL position-independently, keeping its relocation sites as "base + offset". (a) is the
  smaller change to `recomp.py` — it is how the MWo3-overlay ports (rogue-galaxy, DQ8) work — but needs the
  loader to always get the same address. Several DLLs may be resident at once (core imports from title, fmap,
  tmap, bmap, sento, endmovie): which ones overlap is still to measure.

## VU1 microcode

All in `.data`, uploaded with VIF `MPG` packets (`tools/recon/vu_scan.py`):
- **~66 large programs** `0x28D2CC–0x351E00`, 4–8 MPG packets each, up to 0x3F20 bytes (almost all of VU1's
  16 KB). Not in `.DVP.ovlytab`: prebuilt binaries, RenderWare's sky2 pipelines (deduction).
- 11 small programs `0x35187C–0x37C09C` (9 of one packet, 2 of two).
- 13 programs from the 17 `.DVP.overlay` sections (`0x381370–0x384F50`; four are two-part, `0x0` + `0x800`),
  matching symbols like `vu1nfim2dll`, `vu1im2dll` in the `SNR1` table (RenderWare Im2D/ptank, deduction).
- Code checked with the reference's `tools/vudis.py`: real VU code (loads matrices from VU memory 1008–1011, `loi 0.5`…).
- 90 programs, 452 packets in all — Saint Seiya had 62 packets. The microcode is shared RenderWare code, so other
  RenderWare PS2 games' notes may help.

## AFS (`AFSLIST0.AFS`)

`tools/recon/afs.py`: 25,831 entries, 1.58 GB, name table at offset 0x5FAEF800 (48-byte records).
- 25,577 `.cmp`, 250 `.adx`, 2 `.sfd` (Sofdec movies), 1 `.hd` + 1 `.bd` (Sony sound bank header/body for
  MODHSYN).
- 25,558 entries start with `SD0\0` (compressed). 179 `.adx` start with `80 00 00 24` (ADX header); 19 start with
  `a \r\n` (text, unknown).
- First entries: the 11 DLLs, `dllfsize.cmp`, `font_g16.cmp`, `font_g22.cmp` (`FIG` header after decompression),
  `glbdata.cmp` (`MRG`)…
- Data formats inside `.cmp` are still to look at; RenderWare binary streams (`.dff`/`.txd`-style chunks) are
  likely since `B3D_*` calls `RwStreamOpen`/`RpClumpStreamRead`.

## Still to measure

- `snDllLoaded`/`SYS_DLLMain` in detail: where the DLL memory comes from, whether the address is stable, which
  DLLs are loaded together (log it in PCSX2 or in our port at milestone 2).
- Function boundaries inside the DLLs (they have their own export symbols).
- `RPGSND.IRX` RPC interface and how `.hd/.bd` + MIDI sequences reach MODHSYN/MODMIDI.
- The 19 text-like AFS entries; `CMP0`/`RLN0` usage.
- Which RenderWare VU programs the game actually uses.
