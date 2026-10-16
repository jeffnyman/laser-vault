// board.h - Sega's laserdisc game board, which runs Astron Belt (Sega,
// 1983).
//
// A Z80 driving a Pioneer LD-V1000, with sprites and characters drawn over
// the picture:
//
//     Z80 at 5 MHz (20 MHz / 4)
//     0000-7FFF  ROM (32 KB)
//     8000-BFFF  ROM, one of two 16 KB banks (chosen by writing to port 0
//                or 1: bit 0)
//     C000-C1FF  sprites: 32 of 16 bytes
//     C800       laserdisc: read the latched status, write the next command
//     D000, D001 DIP switches 1 and 2
//     D002       coins, start buttons, test and service
//     D003       joystick and fire
//     D800       sound board (sound effects)
//     D801       bit 6: NMI on; bit 5: second half of the character colours
//     E000-E1FF  colour RAM: 256 colours of 2 bytes, 4 bits each of red,
//                green and blue
//     F000-F3FF  characters: 32 x 32 tile numbers, by rows (mirrored at
//                F400-F7FF)
//     F800-FFFF  RAM
//
// The IRQ, at vertical blank, runs the game. The NMI, once a field, is the
// laserdisc link: the board latches the player's status and passes on the
// command byte the game left, and the NMI lets the game read the one and
// leave the next.
//
// Sprites are drawn row by row from two graphics ROMs, which give each
// pixel 4 bits apiece: the 8-bit result is a colour number. Characters are
// 8x8 pixels of 2 bits; a colour PROM turns each one, with bits from its
// tile number, into a colour number. Colour 0 is see-through.
//
// The sound effects come from a separate sound board, which isn't
// emulated: recordings of its sounds (WAV files, as made for the Daphne
// emulator) stand in for it, if they're there. The game writes a sound's
// number to D800 between two writes of "nothing" (low four bits 0, bit 5
// set), once per sound.
//
// Details from MAME's segald.cpp and the Daphne emulator's astron.cpp, and
// the game's code.

#ifndef LV_SEGA_BOARD_H
#define LV_SEGA_BOARD_H

#include "audio/samples.h"
#include "cpu/z80.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

#define SEGA_CPU_HZ 5000000
#define SEGA_SAMPLE_RATE 44100

// The part of the 256 x 256 pixel grid on screen: all of it across, and
// from line 16 down (as the Daphne emulator places it).
#define SEGA_OVERLAY_W 256
#define SEGA_OVERLAY_H 240
#define SEGA_FIRST_LINE 16

// Inputs are active low on the real board; here, true means pressed.
typedef struct {
    bool up, down, left, right, fire;
    bool start1, start2, coin1, coin2;
} SegaInputs;

typedef enum {
    SEGA_FREE_PLAY,
    SEGA_QUIET_ATTRACT,  // on: no sound in the attract mode ("demo sounds" off)
} SegaSetting;

typedef struct {
    Z80 cpu;
    uint8_t rom[0x8000];
    uint8_t banks[2][0x4000];
    int bank;
    uint8_t sprite_ram[0x800];
    uint8_t colour_ram[0x200];
    uint8_t char_ram[0x400];
    uint8_t ram[0x800];
    uint8_t out[4];               // D800-D803
    uint8_t chars[0x1000];        // character graphics: two bit planes of 256 tiles
    uint8_t sprites[0x10000];     // sprite graphics: two ROMs of 4 bits a pixel each
    uint8_t char_prom[0x200];     // character colours

    SegaInputs inputs;
    uint8_t dsw1, dsw2;
    Ldv1000 *player;
    uint8_t ld_in, ld_out;        // the latches between the game and the player
    Samples sound;                // the sound board's recorded sounds

    uint64_t cycles;              // CPU clock cycles since power-on
    uint64_t fields;              // video fields since power-on
    uint64_t next_field;          // cycle the next field starts on
    uint64_t next_strobe;         // cycle of the player's next command strobe
    unsigned overlay_serial;      // changes whenever the overlay's look may have changed
} SegaBoard;

// Does path hold a ROM set for this board? Quiet: for choosing a board.
bool sega_recognizes(const char *path);

// Load the ROMs from a zip file or a folder, checking each chip's checksum.
// Chips the set shares with its parent may be in the parent's zip, next
// to it (as MAME keeps them). Returns false (after printing why) if the
// set can't be run.
bool sega_load_roms(SegaBoard *board, const char *path);

const char *sega_game_name(const SegaBoard *board);

// Load the recordings of the sound board's sounds from dir (ab_fire.wav and
// so on). Returns how many were found; the game plays without the others.
int sega_load_samples(SegaBoard *board, const char *dir);

void sega_set(SegaBoard *board, SegaSetting setting, bool on);
bool sega_get(const SegaBoard *board, SegaSetting setting);

// Power on, connected to an LD-V1000.
void sega_reset(SegaBoard *board, Ldv1000 *player);

// Run the board for (at least) the given number of CPU cycles.
void sega_run(SegaBoard *board, uint64_t cycles);

// Draw the sprites and characters as they look now: SEGA_OVERLAY_W x
// SEGA_OVERLAY_H pixels, as 0xAARRGGBB, alpha 0 where the picture shows
// through.
void sega_draw_overlay(const SegaBoard *board, uint32_t *pixels);

#endif
