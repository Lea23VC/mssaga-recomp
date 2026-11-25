# MS Saga: A New Dawn — native port (work in progress)

A native PC port of **MS Saga: A New Dawn** (PS2, USA, SLUS-21270) by static recompilation, modelled on
[SaintSeiyaRecomp](https://github.com/matpdev/SaintSeiyaRecomp).

Nothing from the game is in this repository. You need your own copy of the disc.

Status: the game runs natively to its title screen and menus (milestone 4); no sound yet.
See [docs/status.md](docs/status.md), [docs/recon.md](docs/recon.md) and [CLAUDE.md](CLAUDE.md).

## Setup (WSL2 Ubuntu)

```sh
git clone <this repo> mssaga-recomp && cd mssaga-recomp
git clone https://github.com/matpdev/SaintSeiyaRecomp reference/SaintSeiyaRecomp
ln -s "/mnt/g/Sistema/Usuarios/Leandro/Descargas/MS Saga - A New Dawn (USA)/MS Saga - A New Dawn (USA).iso" disc/game.iso
bsdtar -xf disc/game.iso -C disc SLUS_212.70 SYSTEM.CNF
./build.sh        # symbols -> splat -> recompiler (executable + DLLs) -> gcc -> build/mssaga
./build/mssaga    # a window; --shot 1500 saves build/shot_1500.ppm instead
```

Needs: gcc, python3-venv, SDL3 (Ubuntu 22.04: build it from source), `libvulkan-dev`, `glslang-tools`,
`pkg-config`. Keys: arrows, Z/X/A/S = cross/circle/square/triangle, Q/W/E/R = L1/R1/L2/R2, Enter = start.
