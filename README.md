# MS Saga: A New Dawn — native port (work in progress)

A native PC port of **MS Saga: A New Dawn** (PS2, USA, SLUS-21270) by static recompilation, modelled on
[SaintSeiyaRecomp](https://github.com/matpdev/SaintSeiyaRecomp).

Nothing from the game is in this repository. You need your own copy of the disc.

Status: reconnaissance. See [docs/recon.md](docs/recon.md) and [CLAUDE.md](CLAUDE.md).

## Setup (WSL2 Ubuntu)

```sh
git clone <this repo> mssaga-recomp && cd mssaga-recomp
git clone https://github.com/matpdev/SaintSeiyaRecomp reference/SaintSeiyaRecomp
ln -s "/mnt/g/Sistema/Usuarios/Leandro/Descargas/MS Saga - A New Dawn (USA)/MS Saga - A New Dawn (USA).iso" disc/game.iso
bsdtar -xf disc/game.iso -C disc SLUS_212.70 SYSTEM.CNF
sha1sum disc/SLUS_212.70 > disc/SLUS_212.70.sha1   # keep locally; add the hash (only) to config/ later
```
