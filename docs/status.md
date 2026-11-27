# Status

## Cheats (F1 > Cheats) (2026-11-27)

`port/runtime/cheats.c` applies cheat codes at each vertical blank (kernel.c, after the pads), as a PS2 cheat device
would. The list: the MS Saga section of the CheatDevicePS2 database (GPL-3.0; `tools/cheats2c.py` writes
`port/runtime/cheats_db.c`), 236 cheats, plus the player's own `cheats.txt` in the settings folder. Its enable code
(`90133094 0C09B3B7`) matches SLUS_212.70 (the word at 0x133094), so they are for this release. None patch the
game's code (they would do nothing: it is compiled); all write its data (mostly the save state at 0x1Fxxxxx). Types
done: 0/1/2 (8/16/32-bit writes), 4 (serial writes), D (16-bit test of the pad buffer: the "press L1+Select" codes);
9 (a device's hook) skipped. Which are on: `cheats_on` in the settings folder. The menu's Cheats tab: groups that fold,
a search box.

**Max EXP**: the database writes 99,999,999; the game's EXP table (0x3B7438, 49 steps to 5,000,000 at level 50, the one
"Gain 1 Exp For Level 50" rewrites) says level 50 is the cap. With more, the party screen lost the character's
portrait for good; now it writes 5,000,000. The levels come at the end of the next battle (until then "Next" is
negative and the party screen draws the row broken; the game's own behaviour); the menu says so. Played: Level 99,
stats, TP, Max EXP (fixed after a battle), Have Item.

## Settings menu (F1 / Start+Select) with Dear ImGui (2026-11-27)

`port/gs/menu.cpp` (C++, behind a few C functions) draws a settings window with Dear ImGui v1.91.9b
(`port/third_party/imgui`, MIT, `licenses/DearImGui-MIT.txt`; SDL3 + SDL_GPU backends) over the picture, after
`present_into` in `gpu_present`. Tabs: Video (16:9, V-Sync, window size, HUD at the edges, portraits at the edges,
sides picture), Gameplay (turbo, x2/x3/x4), Misc (performance panel, Discord at the next start); Close, Exit game
(confirmed). F1 opens it (the window size moved into it); Start+Select on the first gamepad too. While it is open the
game gets no buttons (`poll_pads`) but keeps running; keyboard, mouse and gamepad navigate it. Choices are applied
between pictures (`menu_run_pending`): done at once, V-Sync rebuilt the swapchain the menu was drawing into (a crash in
the NVIDIA driver). The three layout options are kept in the settings folder (`hud_edges`, `portraits`,
`sides_picture`; their environment variables still win), turbo's speed in `turbo_speed`. The window size button
restores a maximized window first (a maximized window ignored the size). Both builds compile the C++ with g++ and link
with it (Windows: `-static-libstdc++`, ~1.5 MB more). Played: works, V-Sync and window sizes included.

## Noisy sky in the village cutscene fixed; turbo; subtitles and portraits in 16:9 (2026-11-27)

**Sky / trees noise** (the Zakus attacking the village, not every run; the software GS too): the cutscene streams a
256x256 texture (the sky) through RenderWare's texture pool each picture. From a 12-second F9 clip: up to a point
the uploads trickle in between the draws (one GIF packet per MSKPATH3 0); then the next picture's chain is started
only after this picture's VIF1 has passed its unmasks, and the end-of-picture flush sent it whole: ~100 uploads in
one burst, a pool slot written twice before its first texture was drawn (the sky's slot under a small texture). Now
at the end of a picture (`dmac_path3_seal`) a chain already opened by this picture's VIF1 sends the rest, and one not
opened yet stays held for the next picture's VIF1 to open packet by packet, as on the PS2. Played: clean three times
(2x and normal speed), no freeze. F10 still switches path 3 to whole chains live, to compare.

**Turbo** (`runtime/turbo.c`): Tab runs gameplay faster (x2, `MSSAGA_TURBO=3/4`), kept in the settings folder. The
game counts in vertical blanks, so the kernel's timer fires n times as often; music and voices (ADX) and the sound
driver keep their real-time clocks. Only on a map or in a battle (`game_on_map` / `game_in_battle`), not during a
movie, and only while exactly one ADX voice plays (none: changing tracks, entering a battle, where at x2 the battle
theme's wait ran out and it never started; two or more: a voice line or jingle over the music). Known: a cutscene's
lines before its first voice ran faster at first; now cutscenes are recognised by their letterbox: the game draws
the bars as two opaque black rectangles across the whole width, top and bottom (merged into one draw, the 3D under
them reaching the edges); with both on screen (`gpu_letterboxed`), turbo stays at normal speed. Played: works.

**16:9 fixes**: a subtitle without a box (cutscenes) had its words spread to both edges by the HUD layout: units of a
line's height side by side within 24 pixels are now one element. A centre portrait overlapping a moved side
portrait lost pieces of its 3D (Marie's hair and glasses) to it: 3D drawn into a box now belongs only to its own box.
`MSSAGA_P3_TINY=0` turns the tiny-first-packet rule off (to compare).

## 16:9: the HUD at the screen's edges, as the remasters do (2026-11-26)

Over 3D reaching the sides, the 2D kept 4:3 goes to the 16:9 screen's edges by element (`gpu_vk.c` hud_shifts). The
units are triangles, not draws: the game draws many pieces of different elements with the same state and they arrive
merged (a battle's menu page, the strip behind a name and the enemy bar's back in one draw). Triangles touching (2
pixels of slack) make an element; a 3D box (the minimap's view) is one unit, its scissor; a layer across the whole
width stays and joins nothing. An element wholly in the left half goes to the left edge, wholly in the right half to
the right edge; one crossing the middle stays. A draw whose triangles moved is not cut at 4:3 (GpuDraw.spread).
- Field: radar top-left, minimap bottom-right; dialogue boxes crossing the middle stay.
- Battle: command menus left, enemy name bars right, windows crossing the middle (item list, weapon list) whole and
  centred; the party row below the scene (y >= 312) goes by its three slots: left slot to the left edge (in line with
  the menu), middle stays, right slot to the right edge; a piece wider than a slot stays. Not seen yet: three members
  and the reserve row under them.
- Menus and other pictures without 3D at the sides stay in the middle 4:3.
`MSSAGA_HUD_EDGES=0`: as before. Battles are known from the DLL loaded (`game_in_battle`); replays load none:
`MSSAGA_REPLAY_BATTLE=1` takes one as a battle.

## 16:9: a portrait's text box goes with it (2026-11-26)

When a dialogue portrait is moved to the screen's edge (portrait_shifts), its text box now goes with it: the largest
2D panel starting just below the portrait (within 48 lines) and overlapping it sideways is taken as its box (the
game lines them up), and every 2D draw on that box (text, frame, the arrow, which pokes out a few pixels) gets the
same shift. Dialogue without a portrait stays in the middle; the radar and minimap don't move. Checked on the hangar
dialogue (Tristan's portrait at the left). A portrait on the right side hasn't been seen yet.

## F9 can save the seconds before it (2026-11-26)

`MSSAGA_GSREC_BACK=<seconds>` (up to 20; off by default): the recorder keeps a ring of one-second segments in memory,
each a snapshot of the GS / VIF1 / VU1 state taken at its first picture and everything the GS was given since. F9 then
writes the oldest kept snapshot and all the segments after it, and records one second more (`gsrec.c`; the state
functions now write through `gsrec_io`, a file or a buffer). 5 seconds: the game peaked at ~224 MB. Saving writes
the clip at once (a short pause). Tested: F9 during the logos replays from the Bandai logo 5 s before; in play, the
name entry's cursor moving in the seconds before each press. `MSSAGA_F9_AT=<vblank>` presses F9 once there, for
scripted tests.

## Battle effects' first picture fixed: a texture chain's tiny first packet passes with the next (2026-11-26)

On a counterattack (Zaku, Iron Axe) the first picture of the hit effect showed a pale rectangle with scattered blocks
around the hit; the next picture was right. Same in 4:3 and with the software GS. A live log of one F9 clip (path 3
unmasks, VU1 program starts, uploads, draws, in order, with the GS run in step: `MSSAGA_GSTHREAD=0`) showed every
texture of a battle's chain arriving one unmask late: RenderWare's GIF chain has one packet per draw that may need a
texture (a lone one-register tag when it is already in memory), but starts with one extra lone tag; the path-3 cursor
let one packet through per MSKPATH3 0, so the first unmask sent only that tag. Steady state hid it (the same textures
each picture); a new effect's first picture read what was there before. `dmac.c` p3_run: the chain's first packet,
when it is 2 quadwords or less, does not count (on the PS2 the GIF is through it at once, and the next packet starts
before the mask closes). The GM-flicker case (the paused hangar cutscene from the player's presses) stays still in
every picture, as before. Played: two battles, no rectangle. Also: the recorder (F9) now works with
`MSSAGA_GSTHREAD=0` too (the flip there skipped it).

## Idea for later: settings inside the game (2026-11-26)

Asked: switching widescreen (and the other F-key settings) from a menu instead of keys. Two ways, not started:
- **The game's own Config menu.** It is in the `sub` DLL (`ConfigWindowOpen` D_sub_010338, `ConfigWindowGetStat`,
  `ConfigWindowGetCursorPos`: thin wrappers; the item list, drawing, cursor and value changes are in unnamed
  functions around them). Adding a line needs its item table, its texts (probably a message file in the AFS, drawn
  with the game's font), cursor bounds and box size; the value kept in the port's settings, not on the memory card.
  Several sessions of reverse-engineering. Easier if an existing entry means nothing on PC and can be taken over:
  look at what Config holds first.
- **The port's own overlay menu** (a key or Select+Start held): widescreen, window, V-Sync, Discord in one list,
  drawn like the F3 panel (`gpu_set_overlay`); about a day, works with a gamepad. Suggested first.

## 16:9: split-screen scenes stay 4:3 (2026-11-26) — to do: make them 16:9

The cutscene where the soldier talks to Tristan ("So you're a tough guy, huh?", ~vblank 5800 from NEW GAME) is two
cameras drawn into two full-height boxes, a left half (scissor x 0-287) and a right half (224-511) cut by a slanted
line. The portrait rule (gpu_vk.c portrait_shifts) took them for two portraits and pushed them to the screen's edges:
a black wedge in the middle and the text box cut. Boxes as tall as the screen (300+ lines) are no longer moved: the
scene now shows as in 4:3, with the sides black or `sides.png`.

**To do (decided to leave for now):** show such scenes in 16:9. Each half's camera draws into a raster 288 wide, so
widescreen.c keeps its 4:3 view (boxed: raster under 400 wide), and gs.c marks its VU1 draws `box` (kept 4:3). For a
full-height box, the camera could get the 4:3 x 4/3 view like a full-screen one and its draws go with the 3D
(stretched): each half would reach its own edge of the 16:9 screen and the split stay where it is. Not done yet
because other scenes may draw full-height boxes for other things; check them (F9 clips) before changing it.

## 16:9: fades between scenes fill the screen (2026-11-26)

After a skipped cutscene the game clears the screen to white (in 32-pixel strips) and fades the next scene in from
it; the frame had no 3D, so the renderer took it for a 4:3-only picture and showed the white in the middle with black
or `sides.png` around it. A clear or fade of a colour other than black now counts as drawn at the sides (gs.c
classify, `colour_clear`). `sides.png` is shown only when the 4:3 picture holds 2D other than a clear or a fade across
the screen (a menu, a title), so the black pause between scenes stays black (gpu_vk.c, Target.shows). F9 recordings
no longer overwrite an earlier one with the same name.

Follow-up: a picture of only clears and fades (the game fading over the white without clearing it again, ~1/6 s)
keeps the previous picture's verdict instead of falling back to a 4:3 box. Whether `sides.png` shows is decided once
per picture the game draws (not per frame buffer, not per window redraw: with vsync off those run far faster): on
after 3 pictures that are 4:3 with 2D, off after 10 without, so a skip's last picture or logos fading in and out
never make it flicker, and a menu gets it as it slides in.

## 16:9: the UI drawn through VU1 keeps its shape; markers follow the 3D; sides black or a picture (2026-11-26)

MS Saga draws most of its UI through VU1 like its 3D: dialogue boxes and their text, the field radar, the menus
(name entry, party, memory card), the battle menus, labels. The widescreen rule "2D = drawn from the EE" missed them,
so they were stretched. `gs.c` classify now also keeps 4:3 a VU1 primitive whose vertices all share one depth with
q = 1 and that is at the very front (z >= 0.999: dialogue, HUD) or drawn over everything (depth test always: menus at
z 0, markers at a depth of their own that they write). Kept with the 3D: effects drawn over everything at the 3D's
depths without writing them (an explosion's billboards), additive untextured shapes whose alpha fades to the rim (the
light haze over some fields, in pieces; placed apart their seams showed as beams), and draws reading a render target
(the soft glow copy of the whole picture at save points). A menu's highlight bar is additive too but even: kept.
Markers on things in the 3D (the target's brackets, a unit's RNG label) keep their shape but go where the widened 3D
puts their centre, with the 2D inside them (`gpu_vk.c` marker_shifts).

The sides of a 4:3-only picture are now black (were the picture blurred; `MSSAGA_WIDE_BLUR=1` brings it back), or
show `sides.png` from the settings folder, dimmed, covering 16:9 with its top kept (the player's own picture; none
ships; `MSSAGA_SIDES=0` black).

**F9** records the next 150 pictures (`gsrec_<vblank>.gsrec` in the settings folder) for `--gs-replay`; with
`MSSAGA_REPLAY_SHOTS` and `MSSAGA_REPLAY_DUMP=n` (`MSSAGA_REPLAY_DUMP_ALL=1`: untextured too) that is how the player's
screens above were found and checked. The recordings hold game data: never in git.

## Dotted squares in the hangar cutscene fixed: sprites take each corner's own Q (2026-11-26)

The glows of the first hangar cutscene (orange lights at the pipe ends, a blue glow) showed as dotted squares with the
GPU renderer. They are additive sprites whose first vertex has Q 1 (S, T 1) and whose second keeps the Q of the 3D
drawn before (~0.01) with S, T 0. The GPU path took the second vertex's Q for the whole sprite: S/Q up to ~100, the
glow repeated a hundred times. Now a sprite's corners are each divided by their own Q and drawn with Q 1 (gs.c,
put_prim); sprites with one Q are unchanged.

Found with the GS recorder, now usable: `MSSAGA_GSREC=file MSSAGA_GSREC_FROM=vb MSSAGA_GSREC_FRAMES=n` records,
`mssaga --gs-replay file [warm]` replays (wired into main.c); with `MSSAGA_REPLAY_SHOTS=dir` the replay draws with the
GPU renderer, adding `MSSAGA_REPLAY_SW=1` with the software GS (sw_NNNN.ppm at each flip): the same frames through both.
`MSSAGA_REPLAY_DUMP=n` lists frame n's textured draws.

## Flicker on the GM fixed: path 3 one packet at a time; texture cache by pixels; 16:9 portraits (2026-11-26)

**Flicker** (the GM's armour, visor, arm flashing in the cutscene where it first appears, more after quick skips):
RenderWare streams its textures through a pool in the GS memory and trickles the uploads in with VIF1's MSKPATH3:
the GIF chain (path 3) starts masked, and the VIF1 chain unmasks and masks again around draws (MSKPATH3 0, MSKPATH3 1),
which on the PS2 lets one GIF packet through (the mask acts at the end of a packet). The port ran the whole held chain
at the first unmask: 39 uploads at once, overwriting pool slots still used by the draws that followed (same TEX0,
another picture, one frame in five). `dmac.c` now walks a held path 3 chain as a cursor: one packet at an unmask, one
more at each VU1 program started while open, the rest at FLUSHA / the end of the VIF1 transfer while open, and before
a new path 3 transfer or the frame's end in any case. `MSSAGA_PATH3_WHOLE=1` restores the old behaviour. Found by
recording the player's pad (their path, Circle to confirm, START to skip, filled the cache; mine never did), replaying
it headless, and comparing each draw's texture across paused frames. Ruled out on the way: widescreen, incomplete
frames, GPU synchronisation, the CLUT buffer (CLD; emulating it changed nothing here), frame skipping.

**Texture cache**: textures were keyed by the GS memory pages they read; the pool put the same picture beside other
data each time, so each copy was a new texture (4,025 made of 217 in under a minute), the 256 MB cache filled and
thrashed (~240 textures dropped and remade per second). A texture not known by its pages is now decoded and looked up
by its pixels (`tex_by_pixels`) before a new one is made: 234 made, none dropped, in the same replay.

**16:9 portraits**: dialogue portraits and the minimap's view are 3D drawn into a box of the screen (a sub-raster).
Their cameras keep their view window (only cameras drawing the 512-wide screen are widened; decided at
RwCameraBeginUpdate, which also makes F2 take effect at once), VU1 draws with a scissor narrower than the frame and
lines (the portrait's frame) are kept 4:3 with the 2D. In 16:9 a portrait in the left part of the screen is moved to
its left edge, one in the right part to its right edge, a centred one stays (gpu_vk.c portrait_shifts: the box's 3D
and the 2D on its box; the text box stays). `MSSAGA_WIDE_PORTRAITS=0`: all in the middle.

## Discord status (2026-11-25)

`port/runtime/discord.c`: the Discord app's local IPC (unix socket `discord-ipc-N`, Windows named pipe), no library.
Application ID 1575286990482575430, art assets `title`, `field`, `battle`, `small` uploaded to it. "At the title
screen" (`title` DLL), "Exploring" (`bmap`/`fmap`/`tmap`), "In battle" (`sento`), with the session's start time.
On in the window; `--discord off`, `MSSAGA_DISCORD=0` or a `discord` file containing 0 turns it off; headless runs
only with `MSSAGA_DISCORD=1`. Tested against a stand-in socket server (handshake, title → exploring, ping, reconnect
after a drop), and seen in the real app on CachyOS ("Exploring" with its pictures). Battle and the Windows pipe
not tested yet. Discord shows one activity on the profile: another program's status (VS Code's Discord Presence
extension) can take its place.

The art for Discord / the launcher / the program icon is official game art: kept in `build/art/` (gitignored), never
committed. `MSSAGA_ICON=file.ico ./build-win.sh` puts another icon in a private build.

## RecompLauncher support (2026-11-25)

`recomplauncher.json` (schema-valid): Easy Setup from the player's `.iso` (checked by `SLUS_212.70` and the serial;
verified against Redump disc 13040: 1,632,796,672 bytes, CRC32 e7c78655, which matches our copy), started as
`mssaga --game {input:game} --data {data}`; saves in `{data}/memcard1`; Picture / Window / V-Sync settings passed as
arguments. New options in main.c (no change without them): `--game` (= `--iso`), `--data <dir>` (`MSSAGA_DATA`:
everything the port writes, caches in its `cache`), `--widescreen on|off`, `--window full|WxH`, `--vsync on|off`.
`build-win.sh` copies the manifest next to `mssaga.exe`; `tools/package-win.sh` makes
`build/mssaga-recomp-<version>-windows-x64.zip`. Not done: CHD, a portable Linux release, Recomp Achievements.

## Windows release tidied (2026-11-25)

`build-win.sh` now makes a window program (`-mwindows`) with an icon (`tools/make_icon.py` draws a generic dawn;
nothing from the game), `README.txt` for players (`docs/windows-README.txt`: running, controls, F-keys, where saves
are) and `licenses/` (SDL3 zlib, FFmpeg LGPL, SaintSeiyaRecomp MIT). The folder carries nothing from the game any
more: with no `disc/SLUS_212.70` next to the program, `main.c` copies the executable out of the ISO into the settings
folder (tested on Linux: byte-identical, boots). The log goes to `%APPDATA%\mssaga\mssaga.log`, or to the command
prompt the program was started from. `mssaga.bat` (one person's ISO path) is gone. Not yet run on Windows.

## 60 fps: an experiment, parked (2026-11-25)

The game holds itself at 30 fps twice: RenderWare's minimum of 2 vertical blanks per frame (`B3D_VsyncCntSet(2)` →
`_rwDMAMinVsyncCntSet`, byte D_003C941C) and its task scheduler, which wakes the main loop every 2 vertical blanks
(`SYS_VsyncWakeUp(2)` from 0x1006BC, D_00442CC8). `port/hle/fps.c` replaces both setters; `MSSAGA_FPS=60` makes them
1 and the game runs at 60 (16.8 ms per frame here). B3D_Main measures each frame's length (D_0044312C, read through
B3D_GetVSyncInterval in ~50 places of bmap/tmap/sento), but played by hand nearly everything ran twice as fast, and
the battle theme (ADX, the same voice as the field music) didn't start. So most of the game counts frames: real
60 fps would mean keeping the logic at 30 and making the frames in between (interpolating cameras and objects), a
big job. Default stays 30; `MSSAGA_FPS=60` is kept as the experiment's switch.

Frames in between (display.c `interpolate`, from the reference: the window shows the newest frame moved part of the
way from the one before) never worked here: it paired draws only with the same target, and the game draws its frames
into two buffers in turn (0% paired). Fixed and measured (`MSSAGA_INTERP_LOG=1`, headless): 99% of draws pair when
still, 71-86% while walking; most of the rest are ground and walls cut at the screen's edge into other triangles each
frame (same texture, other vertex count). Added: pairing with look-ahead (a draw come or gone doesn't shift the rest),
a pair must move as one (no vertex more than 6 px off the draw's mean, no jump over 40 px), and unpaired 3D moves
with the motion of the pairs around it (a 16x14 grid). Still frames came out clean (`MSSAGA_INTERP_SHOT=1` with
`--shot`), but played live it still looked broken. Parked: off by default, `MSSAGA_INTERP=1` turns it on. Next
attempt, if any: record a walk and a battle (`MSSAGA_GSREC`) and study long blended sequences offline; or the
engine route (render twice per game step with blended matrices), much bigger.

## Widescreen 16:9 (2026-11-25)

`port/hle/widescreen.c` replaces RenderWare's `RwCameraSetViewWindow` (0x1B8B58): with 16:9 on, the view window's
width is 4/3 larger (Hor+, the same change as nemesis2000's PCSX2 patch for SLUS-21270, which rewrites that function
in place), so the projection and the frustum RenderWare culls with both see more at the sides. On by default with the
GPU renderer; F2 switches it (kept in `~/.local/share/mssaga/widescreen`), `MSSAGA_WIDESCREEN=0/1` overrides. A
camera picks up F2 when it next sets its window (the field camera does at load).

The renderer's widescreen rules (from the reference: 2D kept 4:3 around the centre, 3D stretched) needed one change
for this game: the field clears its frame (colour, then depth) in 32-pixel strips of untextured sprites, each taken
for 2D and kept 4:3, so the 16:9 sides were never cleared (white, 3D failing its depth test there). An untextured
sprite the whole picture high is now a clear and goes with the 3D (`classify` in gs.c). Checked at 1280x720
(`MSSAGA_SHOT_PRESENT=1280x720`): title (2D kept 4:3, sides filled), the hangar and the forest path after it (3D
across 16:9, HUD and minimap in shape). Battles in 16:9 not checked yet. `MSSAGA_PRIMS_EE=1` (with `MSSAGA_PRIMS`)
lists only what the EE drew.

## Movies with FFmpeg (2026-11-25)

The two Sofdec movies are AFS entries 25753 `gto_op.sfd` (the opening, 122.6 s) and 25754 `conp.sfd`: MPEG-2
512x448 at 29.97 fps, ADX stereo 48 kHz. `port/hle/movie.c` (player from the reference) replaces MOVIE_PlayStart /
GetStat / Stop / StopAll: the AFS entry is decoded by FFmpeg on a thread of its own, its pictures shown instead of the
game's, its sound mixed in; the slot says 1 while playing and 3 at the end, START skips. Tested after NEW GAME: the
movie plays with sound, START skips to the prologue, and left alone it ends at 122.6 s and the prologue follows.
`build.sh` uses FFmpeg when pkg-config finds it. Windows: `tools/win_deps.sh` also fetches BtbN's FFmpeg 8.1 LGPL
shared build; `build-win.sh` links it (and copies its 5 DLLs) when present, and takes Arch's `x86_64-w64-mingw32-gcc`
(POSIX threads, UCRT). Cross-built on CachyOS; not yet run on Windows.

## First battle, first save (2026-11-25)

Played on CachyOS (by hand): NEW GAME → the hangar → down the hallway on the left → the first battle (DLL `sento`,
vblank ~8000) → played to the end → back to the field (`bmap` again) → **saved to the memory card**
(`BASLUS-21270G0000`, `pad.bin`, `icon.sys`, `user.ico` in `~/.local/share/mssaga/memcard1`). No errors, no
unimplemented functions. 8 VU1 programs met there were new; added to `config/vu_programs.txt` (now 27 ids), so
battles run fully native on Windows too.

Scripted tests: `--press` now takes the left stick (`N:LLEFT:len`, `LUP`, `LDOWN`, `LRIGHT`). START / TRIANGLE skip
cutscenes. Holding LLEFT from the GM's first position didn't reach the hallway in a script (it only turned); a copy of
the save (`build/mc-battle`, never in git) is the way to start tests near battles.

## Sound, second part: sound effects, the game's driver run as it is (2026-11-25)

`SDRDRV.IRX` (libsd for the EE, sid 0x80000701), `MODHSYN.IRX` (Sony's synthesizer) and `RPGSND.IRX` (the game's
driver, sid 0x7073, which calls MODHSYN through its exported library) are now loaded from the disc and run by the
reference's IOP low-level emulation (`port/iop/lle`: MIPS I interpreter, IRX loader, IOP system libraries, SPU2),
mixed into the SDL output after the ADX voices. `MSSAGA_SND_HLE=1` keeps the old stubs. MODMIDI / MODMSIN are not
run (RPGSND doesn't import them; nothing has asked for them yet).

Added to `iop_lib.c` for these modules: libsd Note2Pitch / Pitch2Note / ProcBatch / ProcBatchEx (13-16), 30-33;
thbase ChangeThreadPriority, ReferThreadStatus, CancelWakeupThread, GetSystemTime, SysClock2USec; sysclib
look_ctype_table, strncmp, strtol; loadcore 7. No import is left unimplemented at the title. The modules sit at
0x38000-0x4C000 (LLE heap 0x30000-0x100000, below the HLE's buffers).

Title test (`MSSAGA_SPU_DUMP=build/spu.raw`): a burst at START, then one short blip per cursor move (peaks ~5,600).
`MSSAGA_SPU_DUMP` writes the SPU2's output alone (raw s16 stereo 48 kHz). Checked by ear on CachyOS: menu cursor and
the GM's steps sound; the music is loud against the effects, as on the real hardware (left as it is).

## Sound, first part: CRI ADX through SDL (2026-11-25)

The build scripts define `MSSAGA_SDL3`, so `port/iop/cri_adxi.c` plays CRI's PS2RNA voices through SDL (48 kHz). The
game creates one voice at the title (vblank ~692) and plays music through it: a recording of the NEW GAME script
(`SDL_AUDIO_DRIVER=dummy MSSAGA_AUDIO_DUMP=build/audio.wav`) has music for the title (~31 s, peaks ~20,000), silence
around NEW GAME / the name entry, then music again from the prologue. Not checked by ear yet (pitch, which track).
Not done: the game's own driver `RPGSND` (+ Sony's MODHSYN/MODMIDI: sound effects and maybe some music) is still a
stub, and the movies' sound. Tests: use `SDL_AUDIO_DRIVER=dummy` so test runs stay silent.

Moved to CachyOS (16 GB, RTX 3070, native Vulkan): the GPU renderer is fast here too (33.4 ms per frame, the game's
30 fps; GS 2.0 ms). MinGW isn't installed on this machine yet (`pacman -S mingw-w64-gcc` for `build-win.sh`).

## VU1 programs built in: Windows at full speed (2026-11-25)

The build compiles the game's VU1 programs into the executables (two stages in `build.sh`: `build/mssaga-stage1
--gen-vu build/vu`, then the final link; `build-win.sh` compiles `build/vu` too). Only the programs the game was seen
to start are built: their ids are in `config/vu_programs.txt` (hashes of entry + reachable microcode; nothing from
the game). Building every place RenderWare's microcode can be entered gave ~2,500 programs and 815 MB of C, and
compiling that ran WSL out of memory (VS Code crashed again). Of the 19 programs used up to the first dialogue,
12 are found by scanning `.data` for MPG chains; the other 7 (the heavy ones) are made from code images saved while
playing (`MSSAGA_VU_IMAGES=build/vu_images`, the game's microcode: in `build/`, never in git).

Windows, NEW GAME script: 41.9 → **33.4 ms per frame (30 fps)**, VU1 35.7 → 7.3 ms, nothing interpreted.

To add programs met later in the game: play with `MSSAGA_VU_IDS=build/vu_ids.txt MSSAGA_VU_IMAGES=build/vu_images`,
append `sort -u build/vu_ids.txt` to `config/vu_programs.txt`, rebuild. A fresh clone needs such a run before the
image-only programs are built in; until then they are compiled at run time (Linux) or interpreted (Windows).

## GPU renderer: models missing and flashing, fixed (2026-11-25)

On Windows (the real GPU) models were missing (the GM showed only its glowing parts, Tristan only her hair), the
background was white, and frames flashed. Cause: RenderWare draws nearly everything with the alpha test's fail mode
FB_ONLY (TEST 0x5140B: alpha GEQUAL 0x40, failing pixels write colour but not depth, depth GEQUAL). The reference's
GPU renderer approximated it by turning depth writes off for the whole draw, so almost nothing wrote depth and later
draws covered nearer ones. Now FB_ONLY / RGB_ONLY draws go in two passes (`gpu_vk.c`): the pixels that pass with the
draw's masks (depth written), then those that fail, colour only (shader fail mode 4). Measured on 31 shots through the
hangar scene: frame-to-frame brightness changed by up to 32 before, at most 1 now; the GM, the characters and the
dialogue boxes render as with the software GS. Also: `gpu_vblank_tick` no longer seals a frame after 3 vblanks once
the game is seen writing DISPFB (only the newest sealed frame is drawn, so a cut picture lost its first part);
`MSSAGA_PRIMS_BIG=1` lists only primitives 200+ pixels wide.

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
