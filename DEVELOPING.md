# Developing LaserVault

How to build the program, the tools that come with it, and where things are
in the source. (To play the games, see [PLAYING.md](PLAYING.md).)

## Setting up a development environment

You need a C compiler, CMake (3.21 or newer), [SDL3](https://www.libsdl.org/)
(3.2 or newer), FFmpeg's development libraries (libavcodec, libavformat,
libavutil, built with libvorbis) and zlib. How you get them depends on the
platform. Each section below goes from a fresh machine to a built
`laser-vault`.

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

## Making a release

The presets above build with debug information, for working on the
program. Each has a `-release` partner (`windows-release`, `linux-release`,
`macos-release`) that builds in its own folder, fully optimized, and can
package the result. On Windows, from the same Developer prompt:

```
cmake --preset windows-release
cmake --build --preset windows-release --target package
```

That leaves `build/windows-release/package/laser-vault-0.1.0-windows-x64.zip`:
`laser-vault.exe` with the DLLs it needs (SDL3, FFmpeg, zlib, Ogg Vorbis and
Microsoft's C runtime), README, PLAYING, LICENSE, and a `licenses` folder
with the libraries' licenses. Unzip it anywhere; it needs nothing else. On
Linux and macOS (add `-DCMAKE_PREFIX_PATH` as above), the package is a
`.tar.gz` with `bin/laser-vault` and the documents in `share/doc/laser-vault`,
and the libraries come from the system's packages, as when building.
`cmake --install build/linux-release --prefix ~/.local` installs the same
files without packaging them. The tools and tests aren't included.

The version number is in `CMakeLists.txt`'s `project()` line (keep
`vcpkg.json`'s the same). Every build also records the git commit it came
from (`cmake/version.cmake` writes it into the program), with `-dirty` when
there were changes not yet committed. The program prints both when it
starts and with `--version`, for example `laser-vault 0.1.0 (041cf10)`, so a
report of a problem says which build it came from.

## Tools

These are built alongside `laser-vault`.

`disc-gif` turns a range of disc frames into an animated GIF, or a single
frame into a PNG, using the same video code as the emulator. The title
screens and GIFs in the [README](README.md) and the diagnostics slates in
[PLAYING.md](PLAYING.md) were made with it (the GIFs with `--noise 32`); the pictures of games being played are screenshots
taken in `laser-vault` (F12; see [PLAYING.md](PLAYING.md#screenshots)).

```
disc-gif path/to/dragonslair.txt --video-dir path/to/mpeg/dragonslair --from 1560 --to 1820 --noise 32 -o drawbridge.gif
disc-gif path/to/dragonslair.txt --video-dir path/to/mpeg/dragonslair --from 1320 -o title.png
```

| Option        | Default | Meaning                                                    |
|---------------|---------|------------------------------------------------------------|
| `--width W`   | 480 (GIF), 640 (PNG) | output width; the height follows from 4:3     |
| `--fps F`     | 12      | frames per second in the GIF                               |
| `--noise N`   | 24      | ignore colour changes smaller than this (smaller files)    |
| `--disc-fps F`| 23.976  | rate the disc frame numbers count at (29.97 for every game but Dragon's Lair and Space Ace) |

A name ending in `.png` writes just the frame given by `--from`, in full
colour. The GIF encoder (`src/tools/gif.c`) and the PNG writer
(`src/util/png.c`) are written from scratch and commented, if you're curious
how GIF palettes, LZW compression or PNG's row filters work.

`cine-trace` runs a game (Dragon's Lair or Space Ace) without a window or
video, hundreds of times faster than real time, and prints what happens:
every command the game sends to the laserdisc player, every change in what
the player is doing, and every change to the score display. It's how you
can see what the game is doing without watching it.

```
cine-trace path/to/lair.zip --seconds 60 --press coin1@40 --press coin1@41 --press start1@43
```

After about 35 seconds of game time you'll see it search to frame 323, where
the attract mode's video starts. With the presses above, a game starts at
frame 1424.

| Option                     | Meaning                                                   |
|----------------------------|-----------------------------------------------------------|
| `--seconds N`              | how much game time to run (default 60)                    |
| `--press BUTTON@T[:LEN]`   | hold a button at T seconds, for LEN seconds (default 0.2) |
| `--dsw1 HEX`, `--dsw2 HEX` | set the DIP switches (default: the ROM set's)             |
| `--autoplay`               | let the program play, printing each move it makes         |

Buttons: `up down left right sword start1 start2 coin1 coin2`, and for
Space Ace `cadet captain ace`.

With `--autoplay`, a whole game takes seconds. This plays through to the
rescue (about 11 minutes of game time) in under 10 seconds:

```
cine-trace path/to/lair.zip --seconds 700 --press coin1@40 --press coin1@41 --press start1@43 --autoplay
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

## Testing the 6502

The 6502 emulator (`src/cpu/m6502.c`) is checked with Klaus Dormann's
functional test, which runs every documented instruction in every
addressing mode, decimal mode included. It's GPL-licensed and not included
here. Get `bin_files/6502_functional_test.bin` from
[Klaus2m5/6502_65C02_functional_tests](https://github.com/Klaus2m5/6502_65C02_functional_tests),
then run:

```
m6502-test path/to/6502_functional_test.bin
```

It should print `passed`, after about 96 million clock cycles (a second or
so). A failure prints the address the test stopped at; the `.lst` file
beside the binary shows which check that is.

## Testing the 8088

The 8088 emulator (`src/cpu/i8088.c`) is checked with Daniel Balsom's
SingleStepTests, recorded from a real 8088: for each opcode, thousands of
single instructions run from random registers and memory, with the
registers and memory they left. They're MIT-licensed and not included here
(about 500 MB). Get the `v2_binary` folder (the `.MOO.gz` files) from
[SingleStepTests/8088](https://github.com/SingleStepTests/8088), then run:

```
i8088-test path/to/v2_binary
```

It should print `passed: all 322 files`, after half a minute or so. Add
opcodes in hex (`i8088-test path/to/v2_binary F6.7 D4`) to run just those;
failures print the test's name and the first register or byte that differs.
The flags Intel leaves undefined for an instruction aren't compared, nor
the cycle counts.

## The other CPUs

The 6809 emulator (`src/cpu/m6809.c`) has no exerciser of its own yet. So
far it's checked by Badlands itself, whose start-up code tests its RAM and
checksums both program ROMs, and which then plays. The COP421
(`src/cpu/cop421.c`) likewise: Thayer's Quest's keyboard works through it.

## Source map

- `src/main.c`: window, keyboard, main loop, drawing.
- `src/version.h`: the version text (written by `cmake/version.cmake`; see [Making a release](#making-a-release)).
- `src/games/machine.c`: what the main program needs from a game board, passed on to the right one.
- `src/games/game_folder.c`: finding the framefile and the ROM set in a game's folder.
- `src/ldp/ldp.c`: the laserdisc player model (play, at normal or another speed; pause, search, step).
- `src/ldp/ldv1000.c`: the Pioneer LD-V1000's command protocol, on top of that model.
- `src/ldp/pr8210.c`: the Pioneer PR-8210's remote-control codes and the frame numbers on the disc.
- `src/ldp/vip9500sg.c`: Sega's laserdisc player (the VIP9500SG) and its serial commands.
- `src/ldp/ldp1000.c`: Sony's LDP-1000 and its serial commands, and the LDP-1450's (with its text display).
- `src/ldp/font8x8.h`: the PC's 8x8 font, for the LDP-1450's text.
- `src/cpu/z80.c`: the Z80 CPU emulator.
- `src/cpu/m6809.c`: the Motorola 6809 CPU emulator, with a disassembler.
- `src/cpu/cop421.c`: the National Semiconductor COP421 microcontroller (Thayer's Quest's keyboard).
- `src/cpu/m6502.c`: the MOS 6502 CPU emulator, with a disassembler.
- `src/cpu/i8088.c`: the Intel 8088 CPU emulator, with a disassembler.
- `src/games/cinematronics/board.c`: the Cinematronics board that runs Dragon's Lair and Space Ace
  (memory map, inputs, outputs, interrupts, ROM sets).
- `src/games/cinematronics/autoplay.c`: Dragon's Lair's and Space Ace's autoplay (the game's list of
  moves, and looking ahead).
- `src/games/universal/board.c`: Universal's System 1 board that runs Super Don Quix-ote
  (memory map, I/O, the tiles drawn over the picture, ROM sets).
- `src/games/stern/board.c`: Stern's board that runs Cliff Hanger and Goal to Go (memory map, I/O,
  the frame-number reader, Goal to Go's side check, the tones, ROM sets, and autoplay from the
  games' move records).
- `src/video/tms9928.c`: the TMS9918/9928 video chip that draws over Cliff Hanger's and Goal to Go's
  picture.
- `src/games/funai/board.c`: Funai's board that runs Esh's Aurunmilla (memory map, I/O,
  the laserdisc interrupt, the tiles drawn over the picture, the beeper, and autoplay from the
  game's scene records).
- `src/games/konami/board.c`: Konami's board that runs Badlands (memory map, the three
  interrupts, the tiles drawn over the picture, the bit-reversed sound chip).
- `src/games/rdi/board.c`: RDI's board that runs Thayer's Quest (I/O ports, the COP421's
  connections and the keyboard's serial link, interrupts, subtitles from the game's text).
- `src/games/sega/board.c`: Sega's board that runs Astron Belt, Galaxy Ranger and Sega's Cobra Command
  (memory map, ROM banks, the serial chip for Sega's player,
  the laserdisc latches, the sprites and characters drawn over the picture).
- `src/games/sega/gpworld.c`: Sega's GP World board (memory map, the steering wheel, the pedals'
  converter and the gear lever, the laserdisc latches, the sprites and characters).
- `src/games/dataeast/board.c`: Data East's board that runs Bega's Battle, Data East's Cobra Command and Road Blaster (two 6502s, the
  serial chip for Sony's player, the two sound chips, the tiles and sprites drawn over the picture).
- `src/games/leland/board.c`: Leland's board that runs Dragon's Lair II (the PC's interrupt
  controller, timer and speaker, the serial port for Sony's player, the EEPROM, the coin counter,
  and autoplay from the game's table of moves).
- `src/games/gottlieb/board.c`: Gottlieb's board that runs Us vs. Them and M.A.C.H. 3 (the 8088,
  the laserdisc interface that clocks commands out to the PR-8210 and reads the frame numbers back,
  the disc's data channel with M.A.C.H. 3's targets, the tiles and sprites, and the sound board's
  two 6502s, DAC and AY-3-8913s).
- `src/util/roms.c`: reading ROM chips from a zip file or a folder.
- `src/util/zip.c`: reading files out of zip archives.
- `src/util/png.c`: writing PNG files, for screenshots.
- `src/video/framefile.c`: framefile parsing (disc frame → clip + offset).
- `src/video/disc_video.c`: MPEG-2 decoding and seeking with FFmpeg.
- `src/video/decomb.c`: finding and repairing combing (mixed fields), pixel by pixel.
- `src/audio/disc_audio.c`: the laserdisc's soundtrack, kept in step with the picture.
- `src/audio/ay8910.c`: the AY-3-8910 sound chip on the Cinematronics and Data East boards (and
  its relative the AY-3-8913, on Gottlieb's sound board).
- `src/audio/sn76496.c`: the SN76496 sound chip on the Universal and Konami boards.
- `src/audio/samples.c`: recorded sound effects (WAV files), for the Sega board's sound board.
- `src/audio/ssi263.c`: the SSI-263 speech chip: its registers and timing, and a formant synthesizer for its voice.
- `src/ui/bezel.c`: the score displays, drawn as seven-segment LED digits.
- `src/ui/icon.c`: reading Windows `.ico` files for the window icon.
- `src/tools/disc_gif.c`: the `disc-gif` tool (decode, scale, sample).
- `src/tools/gif.c`: GIF writer (median-cut palette, LZW compression).
- `src/tools/cine_trace.c`: the `cine-trace` tool.
- `tests/z80_zex.c`: runs ZEXDOC/ZEXALL on the Z80 (a tiny stand-in for CP/M).
- `tests/m6502_test.c`: runs Klaus Dormann's functional test on the 6502.
- `tests/i8088_test.c`: runs the SingleStepTests on the 8088.
- `icons/`: the window icons, one for each game (named in its board's case in
  `machine_icon_name`, `src/games/machine.c`), and `laser.ico` for a game
  without one. CMake builds them into the program (`cmake/embed_file.cmake`
  turns a file into a C array) with a table of their names; to add one, put
  `name.ico` there, add `name` to `EMBEDDED_ICONS` in `CMakeLists.txt`, and
  return it from `machine_icon_name`.

### How a game fits together

Each game runs on its board, in `src/games/<maker>/board.c` (GP World's in
`src/games/sega/gpworld.c`): the CPU (a Z80, Konami's 6809, Data East's
6502s, or Leland's and Gottlieb's 8088s), the memory map, the I/O ports, the
interrupts and whatever the board draws or plays. The board drives a
laserdisc player model (`src/ldp/`), which drives the plain player in
`src/ldp/ldp.c`, whose frame number the main program shows.
`src/games/machine.c` is the common ground: the main program asks it to
load the ROMs, run the board, pass in the controls, and take out the sound
and the overlay, and it passes each call on to the right board. To add a
board, add it there; in the main program, only the lines it prints at
start-up about the game's controls name the games.
