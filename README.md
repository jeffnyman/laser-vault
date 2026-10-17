# LaserVault

A small, readable laserdisc arcade game emulator, written from scratch in C11
with SDL3 and FFmpeg.

The original game code runs on emulated CPUs (a Z80, Badlands' 6809, Bega's
Battle's two 6502s, Dragon's Lair II's 8088, and Thayer's Quest's COP421
microcontroller) and drives an emulated laserdisc player, just as on the
arcade board: the attract mode
plays, coins and start work, and you can play the game, with the
laserdisc's soundtrack, the board's own sounds, and whatever the board draws
over the picture. The code is meant to be read: each part explains how the
real hardware works.

- **To play:** see [PLAYING.md](PLAYING.md). You need your own copy of each
  game: its ROMs and the disc's video, as made for the Daphne emulator.
- **To build it, or to read the code:** see [DEVELOPING.md](DEVELOPING.md).
  It builds on Windows, Linux and macOS.

## The games

| Game | Year | Maker | Board | Player |
|------|------|-------|-------|--------|
| [Dragon's Lair](#dragons-lair) | 1983 | Cinematronics | Cinematronics | LD-V1000 |
| [Space Ace](#space-ace) | 1983 | Cinematronics | Cinematronics | LD-V1000 |
| [Cliff Hanger](#cliff-hanger) | 1983 | Stern | Stern | PR-8210 |
| [Super Don Quix-ote](#super-don-quix-ote) | 1984 | Universal | Universal System 1 | LD-V1000 |
| [Esh's Aurunmilla](#eshs-aurunmilla) | 1984 | Funai / Gakken | Funai | LD-V1000 |
| [Badlands](#badlands) | 1984 | Konami / Centuri | Konami | LD-V1000 |
| [Astron Belt](#astron-belt) | 1983 | Sega / Bally Midway | Sega | LD-V1000 or Sega's own |
| [Galaxy Ranger](#galaxy-ranger) (Star Blazer) | 1984 | Sega / Bally Midway | Sega | LD-V1000 or Sega's own |
| [Cobra Command](#cobra-command) | 1984 | Data East | Data East or Sega | Sony LDP-1000 or Sega's own |
| [Thayer's Quest](#thayers-quest) | 1984 | RDI Video Systems | RDI | LD-V1000 |
| [Bega's Battle](#begas-battle) | 1983 | Data East | Data East | Sony LDP-1000 |
| [Dragon's Lair II: Time Warp](#dragons-lair-ii-time-warp) | 1991 | Leland / Sullivan Bluth | Leland | Sony LDP-1450 |

### Dragon's Lair

Dirk the Daring goes into the dragon's castle to rescue Princess Daphne.
On the cabinet the scores and lives are on LED displays in the bezel;
laser-vault can show them beside the picture. It can also play the whole
game for you, by reading the game's own list of moves. It runs the arcade
rev F2 and the fan-made Dragon's Lair Enhancement 1.1 and 2.0.

<p align="center">
  <a href="assets/dl-title-1320.png"><img src="assets/dl-title-1320.png" width="32%" alt="Dragon's Lair title screen, frame 1320"></a>
  <a href="assets/dl-1560-1820.gif"><img src="assets/dl-1560-1820.gif" width="32%" alt="Dragon's Lair: Dirk on the drawbridge, frames 1560 to 1820"></a>
  <a href="assets/dl-play-1.png"><img src="assets/dl-play-1.png" width="32%" alt="Playing Dragon's Lair: magic fire sweeps across the floor toward Dirk"></a>
</p>

### Space Ace

Dexter, turned back into a boy by Borf's Infanto Ray, has to rescue Kimberly.
Same team, same board; you choose a skill level with three buttons, and the
cabinet's skill lamps show on the score panel. Autoplay works here too. It
runs the arcade rev A3 and the fan-made Space Ace Enhancement.

<p align="center">
  <a href="assets/sa-title-160.png"><img src="assets/sa-title-160.png" width="32%" alt="Space Ace title screen, frame 160"></a>
  <a href="assets/sa-1162-1420.gif"><img src="assets/sa-1162-1420.gif" width="32%" alt="Space Ace: Dexter and Kimmy under Borf's ship, frames 1162 to 1420"></a>
  <a href="assets/sa-play-1.png"><img src="assets/sa-play-1.png" width="32%" alt="Playing Space Ace at Captain level: Borf fires at Dexter from his platform"></a>
</p>

### Cliff Hanger

Cliff has to save Clarissa from the evil Count Draco, in animation from the
Lupin III films *The Castle of Cagliostro* and *Mystery of Mamo*. Its board drives a home player, the Pioneer PR-8210, through the
remote-control socket, and knows where the disc is only by reading the frame
numbers recorded in the picture. A TMS9128 video chip draws the score, the
lives and the hints over it.

<p align="center">
  <a href="assets/ch-title-50.png"><img src="assets/ch-title-50.png" width="32%" alt="Cliff Hanger title screen, frame 50"></a>
  <a href="assets/ch-1860-2120.gif"><img src="assets/ch-1860-2120.gif" width="32%" alt="Cliff Hanger: Cliff runs from the casino with the loot, frames 1860 to 2120"></a>
  <a href="assets/ch-play-1.png"><img src="assets/ch-play-1.png" width="32%" alt="Playing Cliff Hanger: the score, the lives and an ACTION hint over the picture"></a>
</p>

### Super Don Quix-ote

Don Quix-ote and his squire, on horse and donkey, against a sorcerer's
gargoyles and worse. Universal's board draws the scores and messages over
the picture, and the arrows that tell you which way to move.

<p align="center">
  <a href="assets/sdq-title-5455.png"><img src="assets/sdq-title-5455.png" width="32%" alt="Super Don Quix-ote title screen, frame 5455"></a>
  <a href="assets/sdq-5700-5960.gif"><img src="assets/sdq-5700-5960.gif" width="32%" alt="Super Don Quix-ote: the crystal ball, the lightning and the gargoyle, frames 5700 to 5960"></a>
  <a href="assets/sdq-play-1.png"><img src="assets/sdq-play-1.png" width="32%" alt="Playing Super Don Quix-ote: an arrow tells you to push the joystick right"></a>
</p>

### Esh's Aurunmilla

Don Davis flies across the universe to rescue Princess Sindy from Emperor
Esh. MAME lists it as not working; laser-vault's own reading of the game's
code supplied the missing pieces, and it plays.

<p align="center">
  <a href="assets/esh-title-1310.png"><img src="assets/esh-title-1310.png" width="32%" alt="Esh's Aurunmilla title screen, frame 1310"></a>
  <a href="assets/esh-3495-3760.gif"><img src="assets/esh-3495-3760.gif" width="32%" alt="Esh's Aurunmilla: the jump to hyperspace and the asteroid fortress, frames 3495 to 3760"></a>
  <a href="assets/esh-play-1.png"><img src="assets/esh-play-1.png" width="32%" alt="Playing Esh's Aurunmilla, with the score drawn over the picture"></a>
</p>

### Badlands

Buck rides into town after the gang that killed his family, with one button:
wait for the outlaw to draw, then fire. Konami's board runs on a Motorola
6809, and its laserdisc player had Konami's own changes to the player's
software. Like Esh's Aurunmilla, it doesn't run in MAME; reading the game's
code filled in the rest.

<p align="center">
  <a href="assets/bl-title-2066.png"><img src="assets/bl-title-2066.png" width="32%" alt="Badlands title screen, frame 2066"></a>
  <a href="assets/bl-3840-4100.gif"><img src="assets/bl-3840-4100.gif" width="32%" alt="Badlands: through a bullet hole to the gunman, who falls, frames 3840 to 4100"></a>
  <a href="assets/bl-play-1.png"><img src="assets/bl-play-1.png" width="32%" alt="Playing Badlands: the score, the lives and Buck's name over the picture"></a>
</p>

### Astron Belt

A space shooter: your fighter, drawn by the board, flies over film of space
battles and fights the enemy ships it draws over them. Sega's board is the
first here with sprites. It doesn't run in MAME either; laser-vault plays
it, with the disc's soundtrack, and plays recordings of the separate sound
board's effects if you have them.

<p align="center">
  <a href="assets/ab-attract.png"><img src="assets/ab-attract.png" width="32%" alt="Astron Belt's attract screen, drawn by the board over the film"></a>
  <a href="assets/ab-6950-7150.gif"><img src="assets/ab-6950-7150.gif" width="32%" alt="Astron Belt: an enemy ship closes in and explodes, frames 6950 to 7150"></a>
  <a href="assets/ab-play-1.png"><img src="assets/ab-play-1.png" width="32%" alt="Playing Astron Belt: the fighter among enemy ships, with the score and timer"></a>
</p>

### Galaxy Ranger

Astron Belt's sequel, on the same board: your fighter flies through tunnels,
over alien deserts and cities, and into dogfights, to the enemy's battle
cruiser. laser-vault runs both the American version and Star Blazer, its
Japanese release, which plays the same disc.

<p align="center">
  <a href="assets/gr-attract.png"><img src="assets/gr-attract.png" width="32%" alt="Star Blazer's attract screen over the enemy battle cruiser"></a>
  <a href="assets/gr-2880-3140.gif"><img src="assets/gr-2880-3140.gif" width="32%" alt="Galaxy Ranger: enemy craft over the desert, an explosion, then a ground vehicle, frames 2880 to 3140"></a>
  <a href="assets/gr-play-1.png"><img src="assets/gr-play-1.png" width="32%" alt="Playing Star Blazer: the fighter over the alien desert, with the score and timer"></a>
</p>

### Cobra Command

Data East's helicopter game: from the cockpit of an attack helicopter you
fight through New York, the desert and the sea, with the board drawing the
cockpit, the radar and the gun sight over the film. laser-vault runs both
versions: Data East's own, on Bega's Battle's board with Sony's player, and
the one Sega made for its own board, driving Sega's player over a serial
line.

<p align="center">
  <a href="assets/cc-title-700.png"><img src="assets/cc-title-700.png" width="32%" alt="Cobra Command title, frame 700"></a>
  <a href="assets/cc-420-680.gif"><img src="assets/cc-420-680.gif" width="32%" alt="Cobra Command: the helicopter rises out of the sunset as the title appears, frames 420 to 680"></a>
  <a href="assets/cc-play-1.png"><img src="assets/cc-play-1.png" width="32%" alt="Playing Cobra Command: an enemy helicopter in the gun sight, with the cockpit and radar"></a>
</p>

### Thayer's Quest

An adventure from the makers of Dragon's Lair: Thayer travels the five
kingdoms of Quoid, gathering the items he needs, and you play it on a
keyboard of named buttons, typing your name and choosing what to do. A
speech chip narrates. Its voice here is laser-vault's own synthesizer,
speaking the sounds the game sends, with the narrator's words also shown as
subtitles.

<p align="center">
  <a href="assets/tq-title-50430.png"><img src="assets/tq-title-50430.png" width="32%" alt="Thayer's Quest title, frame 50430"></a>
  <a href="assets/tq-50140-50400.gif"><img src="assets/tq-50140-50400.gif" width="32%" alt="Thayer's Quest: ogres seize a girl and warriors battle on the rocks, frames 50140 to 50400"></a>
  <a href="assets/tq-play-1.png"><img src="assets/tq-play-1.png" width="32%" alt="Playing Thayer's Quest: the wizard, with the narrator's words as a subtitle"></a>
</p>

### Bega's Battle

Data East's shooting game over scenes from the anime film *Harmagedon*: Bega
flies through the city and into space, shooting the creatures of the alien
Varga and rescuing the friends who fight beside him. The film is only the
background; the board draws Bega, his enemies and the score over it. Two
6502s run it, one for the game and one for its sound, driving Sony's
LDP-1000 player over a serial line.

<p align="center">
  <a href="assets/bb-title-44100.png"><img src="assets/bb-title-44100.png" width="32%" alt="Bega's Battle title, frame 44100"></a>
  <a href="assets/bb-43901-44161.gif"><img src="assets/bb-43901-44161.gif" width="32%" alt="Bega's Battle: the robot in the dark as the title appears, frames 43901 to 44161"></a>
  <a href="assets/bb-play-1.png"><img src="assets/bb-play-1.png" width="32%" alt="Playing Bega's Battle: Bega fires up past a skyscraper at night, with Luna beside him and an enemy exploding"></a>
</p>

### Dragon's Lair II: Time Warp

Dirk the Daring chases the wizard Mordroc through time, in a time machine,
to rescue Daphne. The sequel came out eight years after the first game, on
a board from Leland built much like an IBM PC: an 8088 runs the game, and
Sony's LDP-1450 player draws the game's words (the level, lives, credits
and, on the first level, hints for the moves) over the picture itself.

<p align="center">
  <a href="assets/dl2-title-2050.png"><img src="assets/dl2-title-2050.png" width="32%" alt="Dragon's Lair II: Time Warp title, frame 2050"></a>
  <a href="assets/dl2-1300-1560.gif"><img src="assets/dl2-1300-1560.gif" width="32%" alt="Dragon's Lair II: scenes from the attract mode, frames 1300 to 1560"></a>
  <a href="assets/dl2-play-1.png"><img src="assets/dl2-play-1.png" width="32%" alt="Playing Dragon's Lair II: the player's LEVEL 1 text over Mordroc's tower at night"></a>
</p>

## What's planned

Ideas rather than promises:

- **More games:** Data East's *Road Blaster*, on Bega's Battle's board;
  Space Ace on Dragon's Lair II's board
  (Leland's 1991 conversion).
  (*Goal to Go*,
  Cliff Hanger's sister game, would be easy, but its disc has never been
  dumped.)
- **Keeping high scores and settings** between runs, for the boards that
  kept them in battery-backed memory or an EEPROM.
- **A list of the ROM sets' checksums**, to check your copies against.

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
