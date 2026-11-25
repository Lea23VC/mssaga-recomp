# CLAUDE.md

Native PC port of **MS Saga: A New Dawn** (PS2, USA, `SLUS-21270`, BEC / Bandai, 2006) by **static
recompilation**: the game's MIPS R5900 code is translated to C ahead of time and linked with a layer that plays
the part of the PS2's hardware (kernel, IOP, VU1, GS → Vulkan). The game's data is read from the player's own ISO
at run time. A matching decompilation may come later; it is not the goal now.

Japanese release (later, maybe): *Gundam True Odyssey: Ushinawareshi G no Densetsu*, `SLPS-25520` / `SLPS-73246`.

## Hard rules

- **Nothing from the game enters git**: no ISO, no `SLUS_212.70`, no `AFSLIST0.AFS`, no IRX/IMG modules, no
  extracted textures/sounds/models/text, no RAM or VRAM dumps, no `asm/` from splat. Check `git status` before every
  commit. `disc/` is gitignored except its README.
- Generated C (if committed later) must contain only translated instructions and address tables, never the
  executable's data. Inspect it before committing.
- Never hand-edit generated output: change the tool, regenerate.
- Keep third-party licences: code copied from SaintSeiyaRecomp (MIT) keeps its notice in `licenses/`.
- Don't push without being asked.

## Approach (decided 2026-11-24)

Base the port on **[SaintSeiyaRecomp](https://github.com/matpdev/SaintSeiyaRecomp)** (MIT), cloned read-only at
`reference/SaintSeiyaRecomp`. Its disc is nearly the same shape as ours (see `docs/recon.md`): one executable,
one CRI AFS archive, CRI ADX audio, Sony SDK 2.8 (IOPRP280), standard PADMAN/MCMAN/LIBSD. Reuse its pieces:

| Piece | Source in the reference | Expected change for MS Saga |
|---|---|---|
| Static recompiler | `tools/recomp/recomp.py` | Remove SLES_541.62 constants (`BASE`, `DATA_START/END`, ELF offset) — read them from our ELF |
| Function boundaries | splat (`config/*.yaml`) | New yaml for `SLUS_212.70`, our `$gp` |
| CPU / kernel | `port/runtime/`, `port/kernel/` | Mostly as is |
| IOP: SIF/RPC, disc, pad, memcard, CRI ADXI | `port/iop/`, `port/hle/cdvd.c`, `memcard.c` | Re-find addresses of the SDK functions in our ELF (`port/hle.txt`, `game.h`) |
| Sound effects / music driver | `port/iop/lle/` (MIPS I interpreter + SPU2) | **New risk**: ours is `RPGSND.IRX` + Sony `MODHSYN/MODMIDI/MODMSIN/SDRDRV` (sequenced music), not SSD/RSSD |
| Graphics | `port/gs/` (DMAC→VIF→VU1→GS, Vulkan) | Our VU1 microcode, our GS usage |
| Game-specific HLE | `port/hle/*.c` (widescreen, training, fonts…) | Drop; write our own later |
| Run-time code | — (Saint Seiya has none) | **New**: most game code is in 11 SN `SNR1` DLLs in the AFS, SD0-compressed (`docs/recon.md`). Recompile them too; see the MWo3-overlay ports below |

Read `reference/SaintSeiyaRecomp/docs/architecture.md` and `CLAUDE.md` before touching the port layer.

Other references worth reading when stuck (not cloned by default):
- [rogue-galaxy-recomp](https://github.com/victor-takai/rogue-galaxy-recomp) and
  [DQ8-Recompiled](https://github.com/Sinan-Karakaya/DQ8-Recompiled): PS2Recomp-based, Metrowerks **MWo3
  overlays** handled by wrapping each in an ELF and an overlay manager. Only relevant if we find runtime-loaded code.
- [PS2Recomp](https://github.com/ran-j/PS2Recomp) (GPL-3.0): the generic framework those use. Fallback base.
- [BT3-Recomp](https://github.com/z3xox/BT3-Recomp): overlays, paraLLEl-GS renderer.
- [dw3_decomp](https://github.com/ReGame-Labs/dw3_decomp): Digimon World 3 (PS1), **same studio (BEC)** — may share
  file formats and engine habits.
- PCSX2 has a widescreen patch for SLUS-21270: its addresses point at the camera code.

## Milestones

0. Recon of `SLUS_212.70` written down in `docs/recon.md` (compiler, memory map, $gp, libraries, VU1 packets,
   any code loaded at run time).
1. splat splits the ELF; the recompiler writes C for every function, and it compiles.
2. Boots: crt0 → main run natively; syscalls logged up to the first block.
3. Files: `AFSLIST0.AFS` read through the IOP HLE.
4. First picture (2D, title screen) with the software GS.
5. 3D (VU1), field and battle visible.
6. Sound (CRI ADX first, then RPGSND/MIDI) and controller, memory card.
7. Playable start to end. Then: widescreen, higher resolution, 60 fps where it makes sense.

## Commands

- `./build.sh`: symbols → splat → `tools/recomp/recomp.py` (ELF + the 11 DLLs) → gcc → `build/mssaga`
  (needs `disc/SLUS_212.70`, `disc/game.iso`, SDL3, Vulkan headers, glslangValidator). ~3 min.
- `./build/mssaga [--shot N,...] [--press "N:START,..."] [-v|-vv] [--trace]`; `MSSAGA_GS=sw` software GS;
  `MSSAGA_DUMP_AT=N` shows where a stuck thread is; `MSSAGA_DMA_LOG=1`; `MSSAGA_MEMCARD=dir` (use a copy in tests);
  `SDL_AUDIO_DRIVER=dummy` for silent tests, `MSSAGA_AUDIO_DUMP=f.wav`, `MSSAGA_SPU_DUMP=f.raw`, `MSSAGA_SND_HLE=1`.
- Player options: `--game <iso>`, `--data <dir>`, `--widescreen on|off`, `--window full|WxH`, `--vsync on|off`.
  Windows release: `./build-win.sh` then `tools/package-win.sh [version]`; it carries `recomplauncher.json`.
- Progress and what's next: `docs/status.md`.

## Environment

- Developed on **CachyOS** (Arch-based). Dependencies via `pacman`: `base-devel python sdl3 vulkan-headers
  glslang pkgconf git`. Windows binaries are cross-compiled with `build-win.sh` (MinGW-w64 + SDL3).
- The ISO lives at `/home/lea/Downloads/MS Saga - A New Dawn (USA).iso`.
  `disc/game.iso` is a symlink to it; `disc/SLUS_212.70` is extracted from it (both gitignored).
- Verify a change: the game still reaches the furthest milestone reached; compare `--shot`-style screenshots
  before/after; compare against PCSX2 when behaviour is in doubt.

## Working style

- Measure before optimizing; record suspicions as suspicions until confirmed (the reference's lessons).
- Every game address the native code uses gets a name in one header (like the reference's `port/hle/game.h`).
- Write findings into `docs/recon.md` / `docs/status.md` as they happen, so the next session has them.
