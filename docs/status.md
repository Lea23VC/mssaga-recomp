# Status

## The Windows build (2026-11-25)

`./build-win.sh` (after `./build.sh`; needs `sudo apt install mingw-w64` and `tools/win_deps.sh`, which fetches SDL3
3.2.30 for MinGW into `tools/win/`) cross-compiles the same generated C and the port into `build-win/mssaga.exe`,
with `SDL3.dll`, a copy of `SLUS_212.70` and `mssaga.bat` (it passes the ISO's Windows path). It is a console program
for now, so the log shows. From WSL it also runs through interop (`cd build-win && ./mssaga.exe --iso 'G:\...' --shot N`).

Measured on the NEW GAME script (frames after vblank 3626, `MSSAGA_FRAME_LOG`):

| | ms/frame | VU1 | GS | screenshots |
|---|---|---|---|---|
| WSL, Vulkan = llvmpipe | 33.4 | 5.7 (JIT) | 3.3 | late by 700+ vblanks: llvmpipe can't keep up |
| WSL, `MSSAGA_GS=sw` | 33.4 | | | on time |
| Windows, Vulkan on the GPU | 41.9 | 35.7 (interpreted) | 3.2 | on time |

The GPU keeps up on Windows. The game is slower there because VU1 programs are only interpreted: the run-time JIT
needs gcc on the machine. **Next: build the VU1 programs into the executable** (the reference's `vu_jit_generate`,
adapted: our ~90 programs are spread over `.data`, see recon.md). Changes for Windows: memcard.c without fnmatch and
localtime_r; the reference's weak, undefined `vu_builtin*` table (PE/COFF can't link it) is an empty table for now.

The first dialogue now shows too ("Fritz: So this is a Mobile Suit? Wow, that's cool!", ~vblank 11,300 of the script).

## The logo freeze, and the first 3D scene (2026-11-25)

- **Freeze fixed** (about 1 run in 5 stopped on the Sofdec/ADX logos): RenderWare's DMA handler masks GIF path 3
  through the VIF1 FIFO, starts the GIF upload (channel 2), then waits for GIF_STAT.FQC to show it waiting before
  starting the channel-1 chain that unmasks it. The port ran the upload at once; when the renderer thread was quick,
  FQC read 0 forever, inside an interrupt handler. Now a channel-2 transfer started while path 3 is masked is held
  (FQC reports it) and runs when VIF1 executes MSKPATH3 0, where the PS2 would run it (`port/gs/dmac.c`). 12/12 runs ok.
- **After the prologue the first 3D scene renders** (a hangar with a mobile suit, `bmap` + `sub`). It seemed to run
  at ~1 fps with ~1 million primitives per frame and memory growing to 5 GB (in WSL's 5.9 GB this took VS Code down).
  **Cause: the GPU renderer's frame queue, not the game.** Under WSL Vulkan is llvmpipe (Mesa on the CPU); it fell
  behind, the queue of finished frames filled, and a frame is only sealed when there is room, so every later draw
  went into one frame that grew without end (the "1 fps" counts sealed frames; `MSSAGA_FRAME_LOG` now also has
  prims, VU1 runs and XGKICKs per frame: the scene starts at a normal ~1,300 primitives and ~500 VU1 runs per frame).
  Fix (`gs.c` seal_locked): when the queue is full the oldest frame's draws are dropped and its texture frees and
  uploads go to the front of the next frame. The game itself runs the scene at 30 fps (33.4 ms per frame in
  `MSSAGA_FRAME_LOG`, ~30,000-40,000 primitives per frame on average, up to ~72,000); the software GS
  (`MSSAGA_GS=sw`) keeps up with it (screenshots on time), llvmpipe doesn't. **Under WSL use `MSSAGA_GS=sw`**; real GPU speed needs a native Windows build.
- Further: the game goes on to the first in-game scene (a walkway, dialogue "Tristan: Yeah... The G System's really
  something..."). A suspicion that skinned models (Tristan's close-up: "big pink shapes") were wrong was not
  reproduced with `MSSAGA_GS=sw`: the cutscene, the bridge walk, the close-up and the mobile suit in the hangar all
  render correctly (vblanks 5000-8900 of the NEW GAME script). Likely a partial frame while llvmpipe dropped frames.
- Tools: a watchdog in the vblank timer (no vblank for 5 s: the stuck thread's registers and stack;
  `MSSAGA_WATCHDOG=0` off), `MSSAGA_FIFO_LOG=1` (VIF1 FIFO writes, MSKPATH3), chain sizes in `MSSAGA_DMA_LOG`,
  `tools/sheet.py` (screenshots side by side). SDL no longer swallows SIGTERM/SIGINT, so `timeout` stops a run.
- **Test runs: cap memory**, e.g. `(ulimit -v 2500000; timeout 200 ./build/mssaga ...)`, and check for leftover
  `build/mssaga` processes afterwards.

## NEW GAME reaches the prologue (2026-11-25)

After the name is confirmed the game plays its opening movie (CRI Sofdec, `MOVIE_PlayStart(0x6499, …)`). Sofdec
decodes MPEG with the IPU, which the port lacks, so the movie never ended: a black screen. `port/hle/movie.c`
replaces `MOVIE_PlayStart` with a slot already marked ended (state 3), so the title DLL goes on: `bmap` loads at
0x516A80 and `sub` at 0x694680, and the prologue text ("This is where our fight begins...") shows. Playing the
movie for real (FFmpeg, like Saint Seiya's movie.c) is for later.

- Speed: vblanks at 60 Hz, the game at 30 fps in the menus (its own rate, it seems; to compare with PCSX2).
- The first time 3D code runs, its VU1 programs are interpreted until gcc has compiled them (cached in
  `~/.cache/mssaga/vu`): that first run crawled for minutes. Next: compile them ahead of time in the build.
- About 1 run in 5 froze on the Sofdec/ADX logo screen: found and fixed (path 3, see above).
- Tests: `MSSAGA_MEMCARD=$PWD/build/mc-test ./build/mssaga --press "1600:START,1800:DOWN,1900:CROSS,2200:START,2350:UP,2450:CROSS" --shot 2600`
  (NEW GAME with the default name; the "Begin with this name?" box defaults to No, hence UP).

## Milestone 4 — the title screen, and the menus answer (2026-11-24)

The reference's renderer is in (`port/gs/`: DMAC → VIF → VU1 (interpreter + run-time JIT) → GS, software or
Vulkan through SDL3's GPU API, `port/iop/pad.c` for keyboard/gamepad). With it the game shows the memory card check,
the Sofdec/ADX logos, the **title screen** ("PRESS ANY BUTTON"), the **LOAD GAME / NEW GAME** menu after START, and
the memory card slot screen after CROSS: all at full speed (1,500 vertical blanks in 25.8 s, WSLg, Vulkan scale 2).

    ./build/mssaga                                    a window (keys below; F1 size, F3 hides the panel)
    ./build/mssaga --shot 1500                        build/shot_1500.ppm, no window (tools/ppm2png.py)
    ./build/mssaga --press "1600:START,1900:CROSS" --shot 1750,2100
    MSSAGA_GS=sw ...                                  software GS instead of Vulkan

Keys: arrows, Z = cross, X = circle, A = square, S = triangle, Q/W = L1/R1, E/R = L2/R2, Enter = start,
Backspace = select (or a gamepad).

What MS Saga needed on top of the reference:
- **RenderWare's DMA scheduler** (`func_001C4B18`, the DMA-end handler) writes VIF codes straight into the VIF1 FIFO
  (0x10005000: STCYCL/STMASK/STMOD at start, MSKPATH3 before an upload) and waits for `GIF_STAT.M3P`. Now: FIFO
  writes go to VIF1 after the queued work (`dmac_vif1_fifo`), MSKPATH3 sets the mask, `GIF_STAT` bit 1 reports it.
  Before this the title DLL stopped in `_rwDMAWaitQueue` after its first frame.
- **`bc0f`** (CPCOND0): `D_PCR` is kept now and `rt_cpcond0` answers from `D_PCR` & `D_STAT` (`port/gs/dmac.c`).
- **`sceTtyWrite`** blocked on the IOP every time the game `printf`s (the memory card screen does each frame):
  replaced (`port/hle/tty.c`), so the game's debug output goes to stderr (`SECTOR_SIZE 94`, `FREE = 8000`).
- Link with `-rdynamic` (the VU1 programs compiled at run time call into the executable). Shaders are compiled with
  `glslangValidator -V` (no `glslc` on Ubuntu 22.04). The VU JIT's ahead-of-time pass is off: our VU1 programs are
  spread over `.data`, not one block like Saint Seiya's.
- Saint Seiya's hooks the renderer calls (training mode, widescreen, font, movie) are stubs in `port/stubs.c`.
- Debugging: `MSSAGA_DMA_LOG=1` logs every DMA transfer.

Next: NEW GAME (the field/battle DLLs and 3D), the opening movie (Sofdec), sound (milestone 6: SDL3 is there now;
`cri_adxi.c` is still built without it, and `RPGSND` is a stub).

## Where it stops now: RenderWare waits for its DMA (2026-11-24)

With the DLLs running, the title DLL draws its first frames, then the main thread spins forever in RenderWare's
`_rwDMAWaitQueue` → `_rwDMAForceBufferSwap` (found with `MSSAGA_DUMP_AT=400`). It waits for the queue state
(`D_003C97F0` = `D_003C97F4`, `D_003C97E8` = 0) that RenderWare's DMA-end handler (`func_001C4B18`, DMAC channels 1
and 2) keeps. That handler reads VIF1/GIF status and the channels' MADR/QWC/TADR to chain the next packet, and
`port/stubs.c` only pretends transfers end. **Next: port the reference's DMAC → VIF → VU1 → GS (`port/gs/`,
milestone 4)** instead of faking more registers. That needs SDL3, Vulkan headers and a GLSL compiler (Ubuntu
22.04 has no `libsdl3-dev` and no `glslc`: build SDL3 from source and use `glslangValidator -V`).

Also done:
- **Memory card:** the game uses classic libmc (not libmc2 like Saint Seiya). `port/hle/memcard.c` replaces its 22
  public functions (all exported by name) with native ones over a folder (`~/.local/share/mssaga/memcard1`,
  `MSSAGA_MEMCARD=dir`). Each call finishes at once and leaves its result for `sceMcSync`. Not exercised yet:
  the title stops at the DMA wait before its first `sceMcGetInfo`.
- Debugging: `-vv` names each syscall's caller; `MSSAGA_DUMP_AT=<vblank>` shows where the polling thread is (registers
  and return addresses on its stack) and stops. The reference's `SSTHEH_*` variables are now `MSSAGA_*`.
- On the way, CRI's frame wait (`ADXM_WaitVsync`: the main thread sleeps, the vsync thread wakes it) was checked and
  works with the reference kernel.

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
