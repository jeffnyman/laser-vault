# LaserVault

A small, readable laserdisc arcade game emulator, written from scratch in C11
with SDL3 and FFmpeg. It plays *Dragon's Lair*.

The original game code runs on an emulated Z80 CPU and drives an emulated
Pioneer LD-V1000 laserdisc player, just as on the arcade board: the attract
mode plays, coins and start work, and you can play the game, with the
laserdisc's soundtrack and the board's own sound chip. The code is meant to
be read: each part explains how the real hardware works.

<p align="center">
  <a href="assets/laser-vault-dl-title.png"><img src="assets/laser-vault-dl-title.png" width="32%" alt="Dragon's Lair title screen, frame 414"></a>
  <a href="assets/laser-vault-dl-dirk.png"><img src="assets/laser-vault-dl-dirk.png" width="32%" alt="Dirk the Daring, frame 2334"></a>
  <a href="assets/laser-vault-ram-test.png"><img src="assets/laser-vault-ram-test.png" width="32%" alt="Diagnostics slate, frame 150"></a>
</p>

<p align="center">
  <a href="assets/dl-530-715.gif"><img src="assets/dl-530-715.gif" width="480" height="360" alt="Dragon's Lair intro, frames 530 to 715"></a>
</p>

## Setting up a development environment

You need a C compiler, CMake (3.21 or newer), [SDL3](https://www.libsdl.org/)
(3.2 or newer), FFmpeg's development libraries (libavcodec, libavformat,
libavutil) and zlib. How you get them depends on the platform. Each section below goes
from a fresh machine to a built `laser-vault`.

### Windows (native, with Visual Studio)

On Windows, the libraries come from [vcpkg](https://github.com/microsoft/vcpkg),
a package manager that builds them from source. The project's `vcpkg.json`
lists what it needs, and CMake asks vcpkg for them automatically.

1. Install [Git for Windows](https://git-scm.com/download/win).
2. Install [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/)
   (the free Community edition or the *Build Tools for Visual Studio 2022*) with
   the **Desktop development with C++** workload. It includes the MSVC compiler,
   CMake and Ninja, so you don't need to install those separately.

   **Or, from the command line with winget**, instead of steps 1 and 2:
   ```
   winget install -e --id Git.Git --source winget
   winget install -e --id Microsoft.VisualStudio.2022.BuildTools --source winget --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.CMake.Project --includeRecommended"
   ```
   The `--override` part matters: without it, winget installs Build Tools
   with no C++ compiler. `VCTools` is the C++ workload, and the
   `VC.CMake.Project` component brings CMake and Ninja. Then open a new
   terminal so `git` is on your PATH. (Visual Studio 2026 Build Tools,
   `Microsoft.VisualStudio.BuildTools`, will probably work too but hasn't been
   tested; its Start menu entry is "Developer PowerShell for VS 2026".)
3. Install vcpkg somewhere outside this project, for example `C:\vcpkg`:
   ```
   git clone https://github.com/microsoft/vcpkg C:\vcpkg
   C:\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   ```
4. Open **Developer PowerShell for VS 2022** from the Start menu. (It sets up
   the compiler; a plain PowerShell window won't find it.) Then:
   ```
   $env:VCPKG_ROOT = "C:\vcpkg"
   cd path\to\laser-vault
   cmake --preset windows
   cmake --build --preset windows
   ```
   To avoid setting `VCPKG_ROOT` every time, set it once as a user
   environment variable, either in Windows settings or with
   `setx VCPKG_ROOT C:\vcpkg` (this applies to terminals opened afterwards).

The first `cmake --preset windows` builds SDL3 and FFmpeg, which takes a
while (around 15 to 30 minutes). vcpkg caches the results, so later builds,
and other checkouts on the same machine, are fast.

The program ends up at `build\windows\laser-vault.exe`, with the DLLs it
needs copied next to it.

### Windows (WSL)

You can also build the Linux version inside WSL (Windows Subsystem for Linux).
On Windows 11, Linux GUI programs open as normal windows.

1. In PowerShell: `wsl --install -d Ubuntu-24.04`, then restart if asked and
   create your Linux user.
2. Open the Ubuntu terminal and follow the **Linux (Ubuntu / Debian)** steps
   below. Your Windows drives are under `/mnt`, so `F:\code\laser-vault` is
   `/mnt/f/code/laser-vault`.

Building on `/mnt/...` works but is slower than inside the Linux file system,
and `make` may print harmless "clock skew" warnings.

### Linux (Ubuntu / Debian)

1. Install the compiler, build tools and FFmpeg:
   ```
   sudo apt install build-essential git cmake ninja-build pkg-config \
       libavcodec-dev libavformat-dev libavutil-dev zlib1g-dev
   ```
2. Install SDL3. **Ubuntu 25.04 or newer:** `sudo apt install libsdl3-dev`,
   and you're done with this step. **Ubuntu 24.04** has no SDL3 package, so
   build it from source. First install the libraries SDL3 uses to talk to
   the display, sound and input devices:
   ```
   sudo apt install libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
       libxi-dev libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev \
       libwayland-dev wayland-protocols libegl-dev libgl-dev libdrm-dev \
       libgbm-dev libpulse-dev libasound2-dev libpipewire-0.3-dev \
       libdecor-0-dev libudev-dev libdbus-1-dev
   ```
   Then build SDL3 and install it into `~/.local` (no `sudo` needed):
   ```
   git clone --depth 1 --branch release-3.4.18 https://github.com/libsdl-org/SDL.git ~/src/SDL
   cmake -S ~/src/SDL -B ~/src/SDL/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_INSTALL_PREFIX=$HOME/.local -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
   cmake --build ~/src/SDL/build
   cmake --install ~/src/SDL/build
   ```
3. Build laser-vault. Leave out `-DCMAKE_PREFIX_PATH=...` if SDL3 came from apt:
   ```
   cd path/to/laser-vault
   cmake --preset linux -DCMAKE_PREFIX_PATH=$HOME/.local
   cmake --build --preset linux
   ```

The program ends up at `build/linux/laser-vault`. On other distributions,
install the same things with your package manager: a C compiler, CMake,
pkg-config, FFmpeg and zlib development packages, and SDL3 if your distribution
packages it (otherwise build it from source as above).

### macOS

Using [Homebrew](https://brew.sh/):

1. Install Apple's command-line developer tools: `xcode-select --install`.
2. Install the tools and libraries: `brew install cmake pkg-config ffmpeg sdl3`.
3. Build:
   ```
   cd path/to/laser-vault
   cmake --preset macos -DCMAKE_PREFIX_PATH=$(brew --prefix)
   cmake --build --preset macos
   ```

The program ends up at `build/macos/laser-vault`.

## Running

You need your own copy of the game:

- **The video:** Daphne-style `.m2v` clips with their `.ogg` soundtracks,
  plus a framefile (for Dragon's Lair, `dragonslair.txt`) that maps disc
  frame numbers to clips. If the video came as an archive, extract it with
  [7-Zip](https://www.7-zip.org/) or `unrar`; for split archives, point the
  tool at the first part. There's no need to run a self-extracting `.exe`.
- **The ROMs:** Dragon's Lair rev F2, as `lair.zip` or a folder holding
  `dl_f2_u1.bin` to `dl_f2_u4.bin`. Some other sets work too; see
  [Other ROM sets](#other-rom-sets).

Then:

```
laser-vault path/to/dragonslair.txt --video-dir path/to/mpeg/dragonslair --roms path/to/lair.zip --fast-boot
```

Press 5 twice to insert two coins (the factory setting is two coins per
credit), then 1 to start (or add `--free-play` and skip the coins). Watch the animation for danger or for something
flashing, and move or use the sword at the right moment.

| Key                   | Action                                    |
|-----------------------|-------------------------------------------|
| Arrow keys            | joystick                                  |
| Space / Ctrl          | sword                                     |
| 5 / 6                 | insert a coin (slot 1 / 2)                |
| 1 / 2                 | start (1 / 2 players)                     |
| P / Pause             | pause / resume                            |
| A                     | autoplay on / off (see below)             |
| S                     | show / hide the score displays            |
| D                     | turn combing repair off / on              |
| F11 / Alt+Enter       | fullscreen on / off                       |
| Escape                | quit                                      |

The number keys work on the top row or the numeric keypad. The console
prints a line for each coin and start press.

Game controllers work too, plugged in before or during play:

| Controller            | Action                                    |
|-----------------------|-------------------------------------------|
| D-pad or left stick   | joystick                                  |
| any face button       | sword                                     |
| Back (Select)         | insert a coin                             |
| Start                 | start (1 player)                          |

That covers Xbox, PlayStation and Switch pads, and arcade sticks that
present themselves as one; the console names each controller as it
connects. Arcade-stick encoders that act as a keyboard (such as the I-PAC)
work out of the box, since the keys above follow MAME's defaults. If SDL
doesn't recognize your stick as a controller, you can describe it to SDL
with the `SDL_GAMECONTROLLERCONFIG` environment variable (see SDL's
[gamecontrollerdb](https://github.com/mdqinc/SDL_GameControllerDB)).

### Options

| Option                | Meaning                                                        |
|-----------------------|----------------------------------------------------------------|
| `--video-dir DIR`     | where the video clips are (default: next to the framefile)     |
| `--roms ROMS`         | run the game, from `lair.zip` or a folder of ROM files          |
| `--fast-boot`         | skip the ~35 s start-up wait (see below)                        |
| `--scores`            | show the score displays beside the picture (also the S key)    |
| `--attract-sound`     | sound in every attract-mode cycle, not just every few          |
| `--unlimited-lives`   | Dirk never loses a life, so you can see the whole game         |
| `--free-play`         | no coins needed: press 1 to start                              |
| `--autoplay`          | the program plays the game for you (also the A key)            |
| `--dsw1 HEX`, `--dsw2 HEX` | set the game's DIP switches directly (see [DIP switches](#dip-switches)) |
| `--fullscreen`        | start fullscreen (F11 or Alt+Enter switches back)              |
| `--icon FILE.ico`     | use a different window icon                                    |
| `--frame N`           | without `--roms`: the frame to start on                        |
| `--disc-fps F`        | rate the disc's frame numbers count at (default 23.976, right for Dragon's Lair; see `src/video/disc_video.h`) |

### What to expect

**Start-up.** Like the real machine, the game waits about 20 seconds for
the laserdisc to spin up, and the attract mode starts after about 35.
`--fast-boot` runs that start-up at full speed before the window appears, so
you land straight in the attract mode.

**Scores.** On the arcade cabinet the scores, lives and credits aren't on
the screen but on LED displays in the bezel around it. `--scores` (or S)
shows them in a panel beside the picture. The window title always shows the
raw display digits and what the laserdisc player is doing.

**Between scenes** the screen goes black for a moment while the laserdisc
player searches, as on the real machine. Longer jumps take a little longer,
from about 0.1 s up to 0.3 s.

**Sound.** The soundtrack comes from the `.ogg` file next to each video clip,
played in step with the picture. The beeps (the credit chord, and a short
tone for each joystick move or sword press) come from the board's
AY-3-8910 sound chip, emulated and mixed in. With the factory settings the
attract mode is mostly silent: a DIP switch ("Sound Every 8 Attracts") has
the game turn the disc's sound off and let it through only every few cycles,
about every five minutes. `--attract-sound` flips that switch.

**Unlimited lives.** `--unlimited-lives` turns on the game's own "Unlimited
Dirks" DIP switch, meant for operators: Dirk still dies when you get a move
wrong, but never loses a life.

**Autoplay.** With `--autoplay`, or after pressing A, the program plays the
joystick and sword itself; you still insert coins and press start. It
plays the whole game, from the first room to rescuing Daphne, without losing
a life, with the original ROMs and both DLE sets. Press A at any time to
take over, and again to hand back. The console prints each move it makes.

It works by reading the game's mind. While a scene plays, the game keeps a
list in RAM of the inputs it's watching for, each with its time windows and
what happens if you press it in each one. Nothing in the list says which
outcome is good, so whenever a new set of moves opens, the program tries
each on a copy of the whole machine, runs the copy a few seconds ahead
(hundreds of times faster than real time), and makes the move after which
Dirk doesn't lose a life. `src/games/dlair/dlair_autoplay.c` describes the
list. Judging a move takes up to about 50 ms, so the picture can hitch for a
frame as Dirk makes it.

**Window icon.** Each game has its own icon, built into the program from
`icons/`.

### Other ROM sets

Give `--roms` a different set and the program works out which one it is,
says so on the console, and sets the DIP switches the way that set expects.

- **`lair.zip`, rev F2:** the arcade original, Cinematronics' last US
  revision. It's what laser-vault was built and tested against. Like the
  arcade machine, it leaves some of the disc unused: for instance, a game
  starts with the castle and then cuts to Dirk running past the portcullis,
  skipping the drawbridge.
- **`dle11.zip`, Dragon's Lair Enhancement 1.1** (Dave Hallock, 2000): the
  same game, built from rev F, with the cuts undone. Every scene and death
  scene on the disc (with a few small exceptions) is now used, scenes play
  to their end, there are some new choices, and errors in the original game
  data are fixed. The set whose files end in `l` is the one tuned for the
  LD-V1000.
- **`dle20.zip`, Dragon's Lair Enhancement 2.0** (Dave Hallock and Jeff
  Kulczycki, 2002): a bigger rework, meant to play the way the animators
  designed the game. It starts at the drawbridge, groups the scenes into
  sections (castle, caverns, the lair), brings back a scene you died on at
  the end of its section, and scores differently. Its prototype mode needs
  the scenes on a Limited Edition disc pressed in 2002, so laser-vault sets
  it to the standard game.
- **Revs D, E and F:** earlier US revisions for the LD-V1000. They should
  work, but haven't been tested.

Each DLE zip includes its authors' readme, which describes every change and
what the DIP switches do in that set. The program tells you if a chip
doesn't match the known copy, and explains why it can't run the sets it
doesn't support: the early revisions made for the Pioneer PR-7820 player,
the European board, and Space Ace.

### DIP switches

The game board has two banks of eight DIP switches, DSW1 and DSW2, that
the operator sets inside the cabinet: pricing, lives, difficulty and so on.
Each ROM set starts with its usual settings (rev F2: DSW1 `20`, DSW2 `79`;
DLE 1.1: `20`, `F9`; DLE 2.0: `20`, `E9`), and the console shows the
settings in use when the game starts.

`--dsw1 HEX` and `--dsw2 HEX` replace a whole bank. `--free-play`,
`--attract-sound` and `--unlimited-lives` then change their own switches on
top, so you can combine them. In rev F2, the bits mean (from MAME's
`dlair.cpp`):

| Bit    | DSW1                                   | DSW2                                          |
|--------|----------------------------------------|-----------------------------------------------|
| `01`   | coins per credit: clear 2, set 3       | sound every 8 attracts (`--attract-sound` clears it) |
| `02`   | unused                                 | demo sounds: set turns them off               |
| `04`   | difficulty mode                        | unlimited Dirks (`--unlimited-lives`)         |
| `08`   | engineering mode                       | laserdisc player: set = LD-V1000 (leave it set) |
| `10`   | two credits always (`--free-play`)     | difficulty (with `80`)                        |
| `20`   | lives: clear 3, set 5                  | pay-as-you-go options (with `40`)             |
| `40`   | pay as you go                          | pay-as-you-go options (with `20`)             |
| `80`   | diagnostics                            | difficulty (with `10`)                        |

So `--dsw1 00` gives 3 lives. The DLE sets change what several switches do;
their readmes have the charts. They number the switches 0 to 7 and give
physical positions, where OFF means the bit is set. The three named options
mean the same in every set laser-vault runs.

### Driving the laserdisc player yourself

Leave out `--roms` and you take the game's place: the keyboard controls the
laserdisc player directly, with picture and sound. It's a good way to explore
the disc. Add `--frame N` to start somewhere other than the first frame.

| Key                   | Action                                    |
|-----------------------|-------------------------------------------|
| Space                 | play / pause                              |
| S                     | stop                                      |
| Left / Right          | step one frame back / forward (pauses)    |
| Down / Up             | search one second back / forward          |
| Page Down / Page Up   | search ten seconds back / forward         |
| Home / End            | search to the first / last clip           |
| D                     | turn combing repair off / on              |
| digits, then Enter    | search to that frame number               |
| F11 / Alt+Enter       | fullscreen on / off                       |
| Escape                | cancel typing, or quit                    |

The player starts paused on the first frame (or `--frame`). Like the
Pioneer LD-V1000 in the Dragon's Lair cabinet, a search ends paused on the
target frame; press Space to play from there.

## Tools

These are built alongside `laser-vault`.

`disc-gif` turns a range of disc frames into an animated GIF, using the same
video code as the emulator. The GIF at the top of this page was made with
it, from frames 530 to 715 with `--noise 32`.

```
disc-gif path/to/dragonslair.txt --video-dir path/to/mpeg/dragonslair --from 530 --to 715 -o intro.gif
```

| Option        | Default | Meaning                                                    |
|---------------|---------|------------------------------------------------------------|
| `--width W`   | 480     | output width; the height follows from 4:3                  |
| `--fps F`     | 12      | frames per second in the GIF                               |
| `--noise N`   | 24      | ignore colour changes smaller than this (smaller files)    |
| `--disc-fps F`| 23.976  | rate the disc frame numbers count at                       |

The GIF encoder (`src/tools/gif.c`) is written from scratch and commented,
if you're curious how GIF palettes and LZW compression work.

`dlair-trace` runs Dragon's Lair without a window or video, hundreds of
times faster than real time, and prints what happens: every command the game
sends to the laserdisc player, every change in what the player is doing, and
every change to the score display. It's how you can see what the game is
doing without watching it.

```
dlair-trace path/to/lair.zip --seconds 60 --press coin1@40 --press coin1@41 --press start1@43
```

After about 35 seconds of game time you'll see it search to frame 323, where
the attract mode's video starts. With the presses above, a game starts at
frame 1424.

| Option                     | Meaning                                                   |
|----------------------------|-----------------------------------------------------------|
| `--seconds N`              | how much game time to run (default 60)                    |
| `--press BUTTON@T[:LEN]`   | hold a button at T seconds, for LEN seconds (default 0.2) |
| `--dsw1 HEX`, `--dsw2 HEX` | set the DIP switches (defaults `20` and `79`)             |
| `--autoplay`               | let the program play, printing each move it makes         |

Buttons: `up down left right sword start1 start2 coin1 coin2`.

With `--autoplay`, a whole game takes seconds. This plays through to the
rescue (about 11 minutes of game time) in under 10 seconds:

```
dlair-trace path/to/lair.zip --seconds 700 --press coin1@40 --press coin1@41 --press start1@43 --autoplay
```

## Testing the Z80

The Z80 CPU emulator (`src/cpu/z80.c`) is checked with Frank D. Cringle's
ZEXDOC and ZEXALL instruction exercisers. They run every Z80 instruction over
a huge range of inputs and compare the results against a real Z80; ZEXALL
also checks the undocumented flag bits. They're GPL-licensed CP/M programs,
so they aren't included here. Get `zexdoc.com` and `zexall.com` from
[agn453/ZEXALL](https://github.com/agn453/ZEXALL), then run:

```
z80-zex path/to/zexall.com
```

All 67 tests should print `OK`. A full run is about 47 billion clock cycles
and takes a minute or two.

## Source map

- `src/main.c`: window, keyboard, main loop, drawing.
- `src/ldp/ldp.c`: the laserdisc player model (play, pause, search, step).
- `src/ldp/ldv1000.c`: the Pioneer LD-V1000's command protocol, on top of that model.
- `src/cpu/z80.c`: the Z80 CPU emulator.
- `src/games/dlair/dlair.c`: the Dragon's Lair board (memory map, inputs, outputs, interrupts).
- `src/games/dlair/dlair_autoplay.c`: autoplay (the game's list of moves, and looking ahead).
- `src/util/zip.c`: reading ROMs out of zip files.
- `src/video/framefile.c`: framefile parsing (disc frame → clip + offset).
- `src/video/disc_video.c`: MPEG-2 decoding and seeking with FFmpeg.
- `src/video/decomb.c`: detecting and repairing combed (mixed-field) frames.
- `src/audio/disc_audio.c`: the laserdisc's soundtrack, kept in step with the picture.
- `src/audio/ay8910.c`: the AY-3-8910 sound chip on the game board.
- `src/ui/bezel.c`: the score displays, drawn as seven-segment LED digits.
- `src/ui/icon.c`: reading Windows `.ico` files for the window icon.
- `src/tools/disc_gif.c`: the `disc-gif` tool (decode, scale, sample).
- `src/tools/gif.c`: GIF writer (median-cut palette, LZW compression).
- `src/tools/dlair_trace.c`: the `dlair-trace` tool.
- `tests/z80_zex.c`: runs ZEXDOC/ZEXALL on the Z80 (a tiny stand-in for CP/M).
- `icons/`: each game's window icon. CMake builds them into the program
  (`cmake/embed_file.cmake` turns a file into a C array); to add one, put
  `name.ico` there and add `name` to `EMBEDDED_ICONS` in `CMakeLists.txt`.

## License

**LaserVault**, A Laser Disc Player\
Copyright (C) 2026 [Jeff Nyman][owner]

[![GNU General Public License version 3](assets/gplv3-127x51.png)][GNU General Public License]

> This program is free software: you can redistribute it and/or modify
> it under the terms of the [GNU General Public License] as published by
> the Free Software Foundation, either version 3 of the License, or
> (at your option) any later version.
>
> This program is distributed in the hope that it will be useful,
> but WITHOUT ANY WARRANTY; without even the implied warranty of
> MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
> [GNU General Public License] for more details.

[owner]: https://github.com/jeffnyman
[GNU General Public License]: https://www.gnu.org/licenses/gpl-3.0.en.html
