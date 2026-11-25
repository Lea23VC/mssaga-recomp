# MS Saga: A New Dawn — native port (work in progress)

<a href="https://recomplauncher.com"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>

A native PC port of **MS Saga: A New Dawn** (PS2, USA, SLUS-21270) by static recompilation, modelled on
[SaintSeiyaRecomp](https://github.com/matpdev/SaintSeiyaRecomp).

Nothing from the game is in this repository or in its releases. You need your own copy of the disc.

## How to play

- **What you need:** a disc image (`.iso`) of the USA release, serial SLUS-21270. The verified Redump dump is
  1,632,796,672 bytes, CRC32 `e7c78655`.
- **Windows:** unzip the release and start `mssaga.exe`. The first time it asks for your `.iso` and remembers it.
  Saves, settings and the log are in `%APPDATA%\mssaga` (see `README.txt` in the release).
- **RecompLauncher:** the release carries a `recomplauncher.json`, so the launcher sets it up from your `.iso`
  and shows the picture, window and V-Sync settings on the game's page.
- **Command line:** `mssaga --game <your.iso> [--data <folder>] [--widescreen on|off] [--window full|WxH]
  [--vsync on|off] [--discord on|off]`. `--data` keeps everything the port writes in one folder.
- **Discord:** with the Discord app running, your status shows that you're playing (title screen, exploring, in
  battle). It's on by default; `--discord off`, or a file named `discord` containing `0` in the settings folder,
  turns it off.
- **Keys:** arrows, Z/X/A/S = Cross/Circle/Square/Triangle, Q/W/E/R = L1/R1/L2/R2, Enter = Start,
  Backspace = Select. F1 window size, F2 widescreen, F3 performance panel, F4 V-Sync. A gamepad works as a
  DualShock 2.

Status: title, movies, story, field, battles, sound and music, memory card saves and widescreen work; a full
playthrough hasn't been done yet. See [docs/status.md](docs/status.md), [docs/recon.md](docs/recon.md) and
[CLAUDE.md](CLAUDE.md).

## Building (Linux; developed on CachyOS)

```sh
git clone <this repo> mssaga-recomp && cd mssaga-recomp
git clone https://github.com/matpdev/SaintSeiyaRecomp reference/SaintSeiyaRecomp
ln -s /path/to/your.iso disc/game.iso
bsdtar -xf disc/game.iso -C disc SLUS_212.70 SYSTEM.CNF
./build.sh        # symbols -> splat -> recompiler (executable + DLLs) -> gcc -> build/mssaga
./build/mssaga    # a window; --shot 1500 saves build/shot_1500.ppm instead
```

Needs: gcc, python3, SDL3, Vulkan headers, glslang, pkg-config, and FFmpeg for the movies (CachyOS / Arch:
`pacman -S base-devel python sdl3 vulkan-headers glslang pkgconf ffmpeg`).

Windows release, cross-compiled: `pacman -S mingw-w64-gcc`, then `tools/win_deps.sh` (SDL3 and FFmpeg for
MinGW), `./build-win.sh` and `tools/package-win.sh` → `build/mssaga-recomp-<version>-windows-x64.zip`.
