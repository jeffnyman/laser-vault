# Playing LaserVault

How to run the games, the keys, the options, and what each game needs.
(To build the program first, see [DEVELOPING.md](DEVELOPING.md).)

- [Running](#running): what you need, the command, the keys and controllers
- [Options](#options) and [what to expect](#what-to-expect)
- The games: [Dragon's Lair ROM sets](#dragons-lair-rom-sets) and
  [DIP switches](#dip-switches), [Space Ace](#space-ace),
  [Super Don Quix-ote](#super-don-quix-ote), [Esh's Aurunmilla](#eshs-aurunmilla),
  [Cliff Hanger](#cliff-hanger), [Badlands](#badlands)
- [Screenshots](#screenshots)
- [Driving the laserdisc player yourself](#driving-the-laserdisc-player-yourself)

## Running

You need your own copy of the game:

- **The video:** Daphne-style `.m2v` clips with their `.ogg` soundtracks,
  plus a framefile (for Dragon's Lair, `dragonslair.txt`) that maps disc
  frame numbers to clips. If the video came as an archive, extract it with
  [7-Zip](https://www.7-zip.org/) or `unrar`; for split archives, point the
  tool at the first part. There's no need to run a self-extracting `.exe`.
- **The ROMs:** Dragon's Lair rev F2, as `lair.zip` or a folder holding
  `dl_f2_u1.bin` to `dl_f2_u4.bin`. Some other sets work too; see
  [Dragon's Lair ROM sets](#dragons-lair-rom-sets). For the other games, see
  [Space Ace](#space-ace), [Super Don Quix-ote](#super-don-quix-ote),
  [Esh's Aurunmilla](#eshs-aurunmilla), [Cliff Hanger](#cliff-hanger) and
  [Badlands](#badlands).

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
| Space / Ctrl          | sword (the action button)                 |
| Shift                 | Esh's Aurunmilla: button 2; Cliff Hanger: feet (other games: the action button) |
| 5 / 6                 | insert a coin (slot 1 / 2)                |
| 1 / 2                 | start (1 / 2 players)                     |
| F1 / F2 / F3          | Space Ace: skill level Cadet / Captain / Ace |
| P / Pause             | pause / resume                            |
| A                     | autoplay on / off (see below)             |
| S                     | show / hide the score displays (Dragon's Lair, Space Ace) |
| D                     | turn combing repair off / on              |
| F11 / Alt+Enter       | fullscreen on / off                       |
| F12                   | save a screenshot (see [Screenshots](#screenshots)) |
| Escape                | quit                                      |

The number keys work on the top row or the numeric keypad. The console
prints a line for each coin, start, button and skill-level press, and each
move of the stick.

Game controllers work too, plugged in before or during play:

| Controller            | Action                                    |
|-----------------------|-------------------------------------------|
| D-pad or left stick   | joystick                                  |
| any face button       | sword (the action button); in Esh's Aurunmilla, bottom and left are button 1, right and top button 2 |
| Back (Select)         | insert a coin                             |
| Start                 | start (1 player)                          |
| left / right shoulder, right trigger | Space Ace: Cadet / Captain / Ace |

That covers Xbox, PlayStation and Switch pads, and arcade sticks that
present themselves as one; the console names each controller as it
connects. Arcade-stick encoders that act as a keyboard (such as the I-PAC)
work out of the box, since the keys above follow MAME's defaults. If SDL
doesn't recognize your stick as a controller, you can describe it to SDL
with the `SDL_GAMECONTROLLERCONFIG` environment variable (see SDL's
[gamecontrollerdb](https://github.com/mdqinc/SDL_GameControllerDB)).

## Options

| Option                | Meaning                                                        |
|-----------------------|----------------------------------------------------------------|
| `--video-dir DIR`     | where the video clips are (default: next to the framefile)     |
| `--roms ROMS`         | run the game, from its ROM set (`lair.zip`, `ace.zip`, `superdq.zip`, `esh.zip`, `cliff.zip`, `badlands.zip`, ...) or a folder of ROM files |
| `--fast-boot`         | skip the ~35 s start-up wait (see below)                        |
| `--scores`            | Dragon's Lair, Space Ace: show the score displays beside the picture (also the S key) |
| `--attract-sound`     | sound in every attract-mode cycle, not just every few          |
| `--unlimited-lives`   | the hero never loses a life, so you can see the whole game     |
| `--free-play`         | no coins needed: press 1 to start                              |
| `--autoplay`          | Dragon's Lair, Space Ace: the program plays the game for you (also the A key) |
| `--dsw1 HEX`, `--dsw2 HEX` | set the game's DIP switches directly (see [DIP switches](#dip-switches); Cliff Hanger also has `--dsw3`, `--dsw4`) |
| `--fullscreen`        | start fullscreen (F11 or Alt+Enter switches back)              |
| `--icon FILE.ico`     | use a different window icon                                    |
| `--frame N`           | without `--roms`: the frame to start on                        |
| `--disc-fps F`        | rate the disc's frame numbers count at (default: the game's own, 23.976 for Dragon's Lair and Space Ace, 29.97 for the others; 23.976 without `--roms`; see `src/video/disc_video.h`) |

## What to expect

**Start-up.** Like the real machine, the game waits about 20 seconds for
the laserdisc to spin up, and the attract mode starts after about 35.
`--fast-boot` runs that start-up at full speed before the window appears, so
you land straight in the attract mode.

**Scores.** On the arcade cabinet the scores, lives and credits aren't on
the screen but on LED displays in the bezel around it. `--scores` (or S)
shows them in a panel beside the picture. The window title always shows the
raw display digits and what the laserdisc player is doing.

<p align="center">
  <a href="assets/dl-scores-1.png"><img src="assets/dl-scores-1.png" width="49%" alt="Playing Dragon's Lair, with the score displays beside the picture"></a>
  <a href="assets/sa-scores-1.png"><img src="assets/sa-scores-1.png" width="49%" alt="Playing Space Ace at Captain level, with the score displays and skill lamps"></a>
</p>

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
a life, with the original ROMs and both DLE sets; Space Ace too, original
and SAE, at every skill level, to Borf's defeat. Press A at any time to take over, and again
to hand back. The console prints each move it makes.

It works by reading the game's mind. While a scene plays, the game keeps a
list in RAM of the inputs it's watching for, each with its time windows and
what happens if you press it in each one. Nothing in the list says which
outcome is good, so the program tries each move on a copy of the whole
machine, runs the copy a few seconds ahead (hundreds of times faster than
real time), and makes the move after which the hero doesn't lose a life,
and can survive the move after that. `src/games/cinematronics/autoplay.c`
describes the list.

The planning runs on its own thread, a few moves ahead of the game: the
copy carries on from each planned move, so every move is decided long
before it's due, and the picture never waits. Each move records what the
game should look like at the moment it's made; if the game has strayed
from the plan (you pressed something too, say), the plans are thrown away
and planning starts again from where the game is.

**Window icon.** Dragon's Lair has its own icon; the other games share one
(most laserdisc game packs come with the same icon). Both are built into the
program from `icons/`.

## Dragon's Lair ROM sets

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
and the European board.

## DIP switches

The game board has two banks of eight DIP switches, DSW1 and DSW2, that
the operator sets inside the cabinet: pricing, lives, difficulty and so on.
Each ROM set starts with its usual settings (rev F2: DSW1 `20`, DSW2 `79`;
DLE 1.1: `20`, `F9`; DLE 2.0: `20`, `E9`; Space Ace rev A3: `DD`, `FE`; SAE:
`20`, `89`; Super Don Quix-ote: `FF`, `FF`), and the console shows the
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

So `--dsw1 00` gives 3 lives. The fan sets (DLE and SAE) change what several
switches do; their readmes have the charts. They number the switches 0 to 7
and give physical positions, where OFF means the bit is set. The original
Space Ace and Super Don Quix-ote have layouts of their own (see
[Space Ace](#space-ace) and [Super Don Quix-ote](#super-don-quix-ote)). In
every set, the three named options flip whichever switches that set uses.
Cliff Hanger has four banks (see [Cliff Hanger](#cliff-hanger)), Badlands
two of its own (see [Badlands](#badlands)), and Esh's Aurunmilla has no DIP
switches at all.

## Space Ace

Space Ace (1983) came from the same team and runs on the same Cinematronics
board, so laser-vault plays it the same way. You need:

- **The video:** Daphne-style clips and their framefile, `spaceace.txt`. Its
  first line is often a placeholder instead of a folder, so give the folder
  with `--video-dir`.
- **The ROMs:** either of these.
  - **`ace.zip`, rev A3:** the arcade original, chips `sa_a3_u1.bin` to
    `sa_a3_u5.bin`. (Revs A and A2 aren't supported; A3 replaced them.)
  - **`sae.zip`, Space Ace Enhancement 1.0:** a fan set by Dave Hallock
    (2002), chips `sae10_u1.bin` to `sae10_u5.bin`. It uses all the
    animation on the disc, smooths the transitions and fixes problems in
    the original program; its readme describes the changes.

```
laser-vault path/to/spaceace.txt --video-dir path/to/mpeg/spaceace --roms path/to/ace.zip --fast-boot
```

Insert two coins and press 1, then choose a skill level within a few seconds
with F1, F2 or F3 (on a controller: left shoulder, right shoulder, right
trigger). Cadet plays about half the animation, Captain more, and Ace all
of it. On the cabinet these were three buttons on the control panel.

The original's DIP switches are its own, and mostly work the opposite way
to MAME's labels for them (a bit set is the switch off), which is probably
why MAME lists Space Ace as not working. laser-vault starts it with DSW1
`DD` and DSW2 `FE`: every switch off except the LD-V1000 player, which is
how the game expects to be set up. `src/games/cinematronics/board.c` lists
what each switch does; `--free-play`, `--unlimited-lives` and
`--attract-sound` flip the right ones.

SAE uses Dragon's Lair's DIP switches, apart from skill (B:6 and B:7; with
both on, as laser-vault sets them, you pick with the buttons) and a "hard"
mode on B:8 that its authors advise against.

On the cabinet, a lamp beside each skill button flashes while the game
waits for you to choose, and the chosen one stays lit. The score panel
shows them under the credits. The game drives the lamps through the score
display's wires, with a code a separate lamp board picked off before it
reached the digits; `src/games/cinematronics/board.c` describes it. Space
Ace also uses a letter code for a blank digit, so the panel shows only
digits; the window title shows the raw codes.

## Super Don Quix-ote

Super Don Quix-ote (Universal, 1984) runs on Universal's own board, the
System 1. Like the Cinematronics board, it has a Z80 that drives an LD-V1000
player, but it can also draw over the picture: the scores, lives and
credits, the messages, and the arrows that tell you which way to move. You
need:

- **The video:** the Daphne-style clip `sdq.m2v` with its soundtrack, and
  the framefile `superdonquixote.txt`. Its first line is often a placeholder
  instead of a folder, so give the folder with `--video-dir`.
- **The ROMs:** `superdq.zip`, the "long scenes" version: `sdq-prog.bin`
  (the program), `sdq-char.bin` (the letters and arrows drawn over the
  picture) and `sdq-cprm.bin` (their colours). The two "short scenes" sets
  (`sdq_c45.rom` or `sdq_c45a.rom`, with `sdq_a8.rom`) should work too, but
  haven't been tested.

```
laser-vault path/to/superdonquixote.txt --video-dir path/to/mpeg/superdonquixote --roms path/to/superdq.zip --fast-boot
```

Press 5 to insert a coin (one coin per credit at the factory setting), then
1 to start. When an arrow appears over the picture, push the joystick that
way before it goes; each one you miss costs a life. Everything the game
shows is drawn on the screen by the board itself, so there's no score panel.

The game keeps time with the disc: every few fields it asks the player
which frame it's on, and times the arrows from the answer. The overlay's
place on the picture is worked out from how the game sets up its video
chip (a real cabinet to compare with would settle it).

Its DIP switches, from MAME's `superdq.cpp` (the game's own code agrees): a
switch on reads as a clear bit, and the factory setting is every switch off.

| Bit    | DSW1                                        | DSW2                    |
|--------|---------------------------------------------|-------------------------|
| `03`   | difficulty: both set easy, both clear hardest | coin slot 2: both set 1 coin 1 credit |
| `04`   | clear: no sound in the attract mode         | coin slot 1 (with `08`) |
| `08`   | clear: no bonus life (set: at 100,000)      | coin slot 1 (with `04`): both set 1 coin 1 credit |
| `30`   | lives: both set 3, `20` only 4, `10` only 5, neither 2 | unused       |
| `40`   | clear: unlimited lives (`--unlimited-lives`) | unused                 |
| `80`   | clear: free play (`--free-play`)            | unused                  |

With demo sounds off, the game plays the attract mode's video in a way the
player keeps silent; `src/ldp/ldv1000.c` explains.

## Esh's Aurunmilla

Esh's Aurunmilla (Funai/Gakken, 1984) runs on Funai's own board: another Z80
driving an LD-V1000, with tiles drawn over the picture for the score and
the menus. You need:

- **The video:** the Daphne-style clip `esh.m2v` with its soundtrack, and
  the framefile `eshsaurunmilla.txt`. Its first line is often a placeholder
  instead of a folder, so give the folder with `--video-dir`.
- **The ROMs:** `esh.zip`, MAME's set 1, under MAME's chip names (`is1.h8`,
  `is2.f8`, `a.m3`, `b.l3`, `c.k3`, `rgb.j1`, ...) or Daphne's (`h8_is1.bin`,
  `f8_is2.bin`, `m3_a.bin`, ...). Sets 2 and 3, with other program chips,
  should work too, but haven't been tested.

```
laser-vault path/to/eshsaurunmilla.txt --video-dir path/to/mpeg/eshsaurunmilla --roms path/to/esh.zip --fast-boot
```

Press 5 to insert a coin, then 1 to start. Choose a level (LOW, MIDDLE or
HIGH: push the stick left or right, and the chosen one flickers), and press
Space (button 1) to begin. The video itself shows what to do: at each
danger, a lamp on the control panel lights and the animation shows the way
out (a dial that points left or right, for one), and you push the stick
that way before the scene moves on.

MAME lists the game as not working. laser-vault's own reading of the game
code gives the pieces MAME was missing: the laserdisc link runs in the
interrupt that the player's command strobe raises once a field, and the
colour PROM has see-through pixels (where the disc's picture shows) and a
blue background for the menus, chosen by whether the game has the picture
switched on. The board's settings live in battery-backed memory instead of
on DIP switches, so `--free-play` and the other switch options do nothing.
The beeper's pitch isn't known; laser-vault uses MAME's guess of 2 kHz.

## Cliff Hanger

Cliff Hanger (Stern, 1983), made from the Lupin III films *The Castle of
Cagliostro* and *Mystery of Mamo*, runs on Stern's own board, with a
different player: the Pioneer PR-8210, a home player the board drives
through its remote-control socket. The player never answers; the game
knows where the disc is by reading the frame numbers recorded in the
picture itself. A TMS9128 video chip draws the score, lives, hints and
messages over the picture. You need:

- **The video:** the Daphne-style clip `ch.m2v` with its soundtrack, and
  the framefile `cliffhanger.txt`. Its first line is often a placeholder
  instead of a folder, so give the folder with `--video-dir`.
- **The ROMs:** `cliff.zip`, MAME's set 1 (`cliff_u1.bin` to `cliff_u5.bin`).
  Sets 2 (`cliffalt.zip`, `cliff_alt_0.bin` to `cliff_alt_3.bin`) and 3
  should work too, but haven't been tested; MAME says set 3 fails a
  start-up check.

```
laser-vault path/to/cliffhanger.txt --video-dir path/to/mpeg/cliffhanger --roms path/to/cliff.zip --fast-boot
```

Press 5 to insert a coin, then 1 to start. Watch for danger: the game shows
a hint at the bottom of the picture, "STICK" (push the joystick the way
Cliff should go) or "ACTION" (a button). There are two buttons, Cliff's
hands (Space) and his feet (Shift), and after a mistake the game tells you
which you should have used. The board's two tones beep for a right move
and boop for a wrong one.

The control panel's feet buttons double as the start buttons, which is how
the game reads them (MAME calls them player 1's and 2's second buttons).
The PR-8210's remote codes, and the way the game sends each one three
times, are described in `src/ldp/pr8210.h`.

Its DIP switches, from MAME's `cliffhgr.cpp` (set 1; a switch on reads as a
clear bit):

| Bit    | DSW1                          | DSW2               | DSW3                         | DSW4            |
|--------|-------------------------------|--------------------|------------------------------|-----------------|
| `01`, `02` | lives: both set 3, then 4, 5, 6 | coin slot 1      | service index, switch test   | move difficulty |
| `04`   | clear: no hanging scene       | unknown            | clear: free play (`--free-play`) | (with `01`-`08`) |
| `08`   | clear: regular-length scenes  | unknown            | clear: immortal (`--unlimited-lives`) | (with `01`-`04`) |
| `10`   | clear: score and lives shown during the animation | coin slot 2 (with `20`) | clear: disc test | unknown |
| `20`   | clear: hints for the stick and buttons | (with `10`) | set: demo sounds (`--attract-sound` sets it) | unknown |
| `40`   | hints after mistakes (with `80`: both set never, `80` only after the first, `40` only after the second, neither after the third) | unknown | set: short scenes | unknown |
| `80`   | (with `40`)                   | unknown            | clear: continues allowed     | unknown         |

laser-vault starts set 1 with DSW1 `C7`, DSW2 `FF`, DSW3 `3F` and DSW4
`FF`, MAME's defaults.

## Badlands

Badlands (Konami/Centuri, 1984) runs on Konami's own board: a Motorola 6809
driving an LD-V1000, with tiles drawn over the picture for the score and
the hero's name, and an SN76496 sound chip for the gunshots. You need:

- **The video:** the Daphne-style clip `badlands-pc.m2v` with its
  soundtrack, and the framefile `badlands.txt`. Its first line is often a
  placeholder instead of a folder, so give the folder with `--video-dir`.
- **The ROMs:** `badlands.zip`, MAME's set 1 (`badlands.a13`,
  `badlands.a14`, `badlands.c8`, `badlands.c4`). MAME's set 2 has its
  devices at other addresses and isn't supported.

```
laser-vault path/to/badlands.txt --video-dir path/to/mpeg/badlands --roms path/to/badlands.zip
```

The start-up checks take a few seconds; then the attract mode plays. Press 5
to insert a coin, then 1 to start: the game begins with the story (the
title, then Buck's family and the wanted posters, about half a minute; it
looks much like the attract mode). Press 2 instead to skip the story and go
straight to the first gunfight.

There's one button, the fire button (Space), and the timing is everything.
Each outlaw squares up, then draws. The moment to fire opens as his hand
goes for the gun and closes just after his shot, about half a second (16
frames) in all. His muzzle flash comes a third of the way in, so if you
wait to see it you'll be too late: fire as he draws. Fire while he's still
squaring up and it's too early, and Buck falls. After each shot the gun
takes about half a second to reload, so an early shot also leaves you
unable to fire when it counts. (For the first gunman, the window is disc
frames 3236 to 3251, as the game's own scene table has it.)

MAME lists the game as not working. Reading the game's code gave the
missing pieces: the interrupts (the NMI at vertical blank sends the
player's next command, and the player's strobe starts the game's work for
the field), the DIP switch banks (MAME has them the other way round), and
how the sound chip is wired (its data lines are reversed, so the game
reverses each byte's bits before sending it). Badlands' player also ran
Konami's own version of the player's software: a digit leaves it ready
for the next byte, so the game sends a frame number one digit a field, and
it has a command to skip back 16 frames. The sound effects run from a timer
interrupt whose rate isn't known for sure; laser-vault uses eight a field,
which gives notes of sensible lengths.

Its DIP switches (a switch on reads as a clear bit):

| Bank | Bits | Meaning |
|------|------|---------|
| DSW1 | all  | coinage: `FF` 1 coin 1 credit, `EE` 1 coin 2 credits, `88` 2 coins 1 credit, `00` free play (`--free-play`); MAME's `konblands.cpp` lists the rest |
| DSW2 | `01`, `02` | lives: both set 2, `02` alone 3, `01` alone 5, neither 7 |
| DSW2 | `80` | set: no sound in the attract mode (`--attract-sound` clears it) |

The other switches in DSW2 are unused (the manual says to leave them off,
that is, set). laser-vault starts with DSW1 `FF` and DSW2 `7E`: 1 coin 1
credit, 3 lives, sound in the attract mode. Hold both start buttons as the
game powers on for its service mode.

## Screenshots

Press F12 to save a screenshot: the picture, with whatever the board draws
over it and the score panel if it's shown, as a PNG named for the date and
time, in the folder you ran it from, at the size it fills the window:
960x720 at the window's starting size. Pause first (P) to line
up a moment; the PAUSED banner stays out of the shot.

## Driving the laserdisc player yourself

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
| F12                   | save a screenshot (see [Screenshots](#screenshots)) |
| Escape                | cancel typing, or quit                    |

The player starts paused on the first frame (or `--frame`). Like the
Pioneer LD-V1000 in the Dragon's Lair cabinet, a search ends paused on the
target frame; press Space to play from there.

Near the start of the Dragon's Lair and Space Ace discs are the slates
their makers used for testing:

<p align="center">
  <a href="assets/dl-diagnostics-150.png"><img src="assets/dl-diagnostics-150.png" width="49%" alt="Dragon's Lair diagnostics slate, frame 150"></a>
  <a href="assets/sa-diagnostics-1.png"><img src="assets/sa-diagnostics-1.png" width="49%" alt="Space Ace diagnostics slate, frame 1"></a>
</p>
