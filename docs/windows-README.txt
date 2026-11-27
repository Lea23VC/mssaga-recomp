MS Saga: A New Dawn - native PC port (unofficial)
==================================================

This is a fan-made native port of the PS2 game MS Saga: A New Dawn (USA, SLUS-21270),
made by static recompilation. It contains no game data: you need your own copy of
the game as a disc image (.iso).

Running it
----------
Double-click mssaga.exe. The first time, it asks for your disc image (.iso) and
remembers it.

Controls
--------
A gamepad (Xbox, PlayStation, ...) works as a DualShock 2. Keyboard:

  Arrow keys   D-pad              Z   Cross         X   Circle
  Enter        Start              A   Square        S   Triangle
  Backspace    Select             Q/W L1 / R1       E/R L2 / R2

  F1   settings menu (also Start+Select on a gamepad): widescreen, window
       size and full screen, V-Sync, HUD layout, turbo, Discord
  F2   widescreen 16:9 on / off (remembered)
  F3   performance panel
  F4   vsync on / off
  Tab  turbo: gameplay at double speed on maps and in battles (remembered)
  F9   records the next few seconds of graphics for a bug report
       (gsrec_*.gsrec in the folder below; large, delete them when done)

Cutscenes can be skipped with Start or Triangle, as on the PS2.

Command line (optional): mssaga.exe --game <your.iso> --data <folder>
  --widescreen on|off  --window full|WxH  --vsync on|off  --discord on|off
--data keeps everything below in that folder instead of %APPDATA%\mssaga.
RecompLauncher uses recomplauncher.json (next to mssaga.exe) to set it up.

Discord
-------
With the Discord app running, your status shows that you're playing (title
screen, exploring, in battle). To turn it off, start with --discord off, or
put a file named "discord" containing 0 in the settings folder below.

Saves and settings
------------------
Everything is kept in %APPDATA%\mssaga:
  memcard1\     the memory card in slot 1 (your saves)
  iso.txt       where your disc image is
  SLUS_212.70   the game's executable, copied from your disc image
  mssaga.log    the log of the last run (useful when reporting a problem)
  sides.png     (optional, yours) a picture shown at the sides of screens
                drawn only in 4:3 (menus, title) in widescreen; black without it

Third-party software
--------------------
See the "licenses" folder: SDL3 (zlib licence), FFmpeg (LGPL 2.1, used as
unmodified DLLs; its source is at https://ffmpeg.org and the build used is
BtbN's FFmpeg-Builds, https://github.com/BtbN/FFmpeg-Builds), and code from
SaintSeiyaRecomp (MIT).
