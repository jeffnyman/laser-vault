# LaserVault

A small, readable laserdisc arcade game emulator, written from scratch in C11
with SDL3 and FFmpeg.

The original game code runs on an emulated Z80 CPU and drives an emulated
Pioneer laserdisc player, just as on the arcade board: the attract mode
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

## What's planned

Ideas rather than promises:

- **More games:** *Thayer's Quest* (RDI, 1984), an adventure played on a
  keyboard of named buttons, with a speech chip; *Astron Belt* and *Galaxy
  Ranger* (Sega, 1983), shooters with sprites over the video. (*Goal to Go*,
  Cliff Hanger's sister game, would be easy, but its disc has never been
  dumped.)
- **Keeping high scores and settings** between runs, for the boards that
  kept them in battery-backed memory.
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
