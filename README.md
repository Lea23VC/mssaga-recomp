# MS Saga: A New Dawn — native port (work in progress)

<a href="https://recomplauncher.com/game/mssaga-recomp"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recomplauncher.com/assets/press/launcher/badge-light.svg"><img alt="Get it on RecompLauncher" src="https://recomplauncher.com/assets/press/launcher/badge-dark.svg" height="48"></picture></a>
<a href="https://recompstore.com/dev/lea23vc"><picture><source media="(prefers-color-scheme: dark)" srcset="https://recompstore.com/assets/press/verified-light.svg"><img alt="Verified developer on RecompStore" src="https://recompstore.com/assets/press/verified-dark.svg" height="48"></picture></a>

A native PC port of **MS Saga: A New Dawn** (PS2, USA, SLUS-21270) by static recompilation, modelled on
[SaintSeiyaRecomp](https://github.com/matpdev/SaintSeiyaRecomp).

Nothing from the game is in this repository or in its releases. You need your own copy of the disc.

## How to play

- **What you need:** a disc image (`.iso`) of the USA release, serial SLUS-21270. The verified Redump dump is
  1,632,796,672 bytes, CRC32 `e7c78655`.
- **Windows:** unzip the release and start `mssaga.exe`. The first time it asks for your `.iso` and remembers it.
  Saves, settings and the log are in `%APPDATA%\mssaga` (see `README.txt` in the release).
- **Linux:** unpack the release's `.tar.gz` and run `./mssaga` (keep the `lib` folder next to it). It runs on
  distros from 2022 on (Ubuntu 22.04, Debian 12, Fedora 36, Arch, the Steam Deck) with a Vulkan driver. Saves,
  settings and the log are in `~/.local/share/mssaga`.
- **RecompLauncher:** the release carries a `recomplauncher.json`, so the launcher sets it up from your `.iso`
  and shows the picture, window and V-Sync settings on the game's page.
- **Command line:** `mssaga --game <your.iso> [--data <folder>] [--widescreen on|off] [--window full|WxH]
  [--vsync on|off] [--discord on|off]`. `--data` keeps everything the port writes in one folder.
- **Discord:** with the Discord app running, your status shows that you're playing (title screen, exploring, in
  battle). It's on by default; `--discord off`, or a file named `discord` containing `0` in the settings folder,
  turns it off.
- **Keys:** arrows, Z/X/A/S = Cross/Circle/Square/Triangle, Q/W/E/R = L1/R1/L2/R2, Enter = Start,
  Backspace = Select. F1 (or Start+Select on a gamepad) opens the settings menu: widescreen, window
  size, resolution (anti-aliasing), V-Sync, HUD layout, turbo, controls (rebind keys and controller buttons),
  cheats (236 codes) and more. F2 widescreen, F3 performance panel, F4 V-Sync, F9 records the next few
  seconds of graphics for a bug report (`gsrec_*.gsrec` in the settings folder; started with
  `MSSAGA_GSREC_BACK=5`, it keeps the last 5 seconds in memory and F9 saves those too, to catch a glitch after
  seeing it). Tab: turbo, gameplay at double speed on maps and in battles (music and voices stay at normal
  speed; cutscenes play at normal speed). A gamepad works as a DualShock 2.
- **Widescreen sides:** screens drawn only in 4:3 (menus, title) have black sides; a picture of your own named
  `sides.png` in the settings folder is shown there instead (dimmed, its top part).

Status: title, movies, story, field, battles, sound and music, memory card saves and widescreen work; a full
playthrough hasn't been done yet. See [docs/status.md](docs/status.md), [docs/recon.md](docs/recon.md) and
[CLAUDE.md](CLAUDE.md).

## Known limitations

- **Not played to the end.** Tested from the start through the first chapter (story, field, battles, saving);
  later areas may still break. Problems found there are fixed as they come up.
- **Windows** builds are cross-compiled and have not been run on a real Windows machine yet.
- **Linux** release: built on Ubuntu 22.04 in a container; tested on CachyOS so far.
- **USA release only** (SLUS-21270). The Japanese release (*Gundam True Odyssey*) isn't supported.
- **30 fps**, as on the PS2. A 60 fps mode was tried and parked: the game's logic and animations are tied to
  its frame rate.
- **Widescreen (16:9)** shows more of the 3D at the sides, but some screens stay 4:3 with black sides (or your
  `sides.png`): menus, the title and story pictures. If something looks stretched, cut or out of place in 16:9, press F9 right
  there and attach the `gsrec_*.gsrec` file to your report; F2 switches back to the PS2's 4:3 picture.

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

Linux release (runs on older distros too): `tools/linux_deps.sh`, then `./build-linux.sh` (in an Ubuntu 22.04
container, needs docker) and `tools/package-linux.sh` → `build/mssaga-recomp-<version>-linux-x64.tar.gz`.

Windows release, cross-compiled: `pacman -S mingw-w64-gcc`, then `tools/win_deps.sh` (SDL3 and FFmpeg for
MinGW), `./build-win.sh` and `tools/package-win.sh` → `build/mssaga-recomp-<version>-windows-x64.zip`.
