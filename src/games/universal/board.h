// board.h - Universal's System 1 laserdisc game board, which runs Super Don
// Quix-ote (1984).
//
// Like the Cinematronics board it has a Z80 driving a Pioneer LD-V1000, but
// it can also draw on the picture: a character generator lays a grid of 8x8
// tiles over the video, for the score, the high-score table and messages.
// Tiles can be partly see-through, letting the disc's picture show between
// the letters.
//
//     Z80 at 2.5 MHz (20 MHz / 8)
//     0000-3FFF  ROM (16 KB)
//     4000-47FF  RAM (2 KB)
//     5C00-5FFF  video RAM: 32 x 32 tile numbers, one byte each, by rows
//
// It uses the Z80's I/O ports for everything else:
//
//     in  00  joystick           out 00  byte for the laserdisc player
//     in  01  buttons and coins
//     in  02  DIP switches 1     in  03  DIP switches 2
//     in  04  laserdisc status   out 04  SN76496 sound chip
//                                out 08  interrupt acknowledge, colours,
//                                        see-through, coin counters
//     0C, 0D  HD46505 video timing chip (programmed once at start-up)
//
// The interrupt comes once per video field (59.94 times a second), and the
// player link is pulsed at the same time: the board latches the player's
// status byte and sends it the byte the game left in the output latch.
//
// The tile graphics (8 KB, 256 tiles of 8x8 at 4 bits a pixel) and a 32-byte
// colour PROM are separate chips. Details from MAME's superdq.cpp.

#ifndef LV_UNIVERSAL_BOARD_H
#define LV_UNIVERSAL_BOARD_H

#include "audio/sn76496.h"
#include "cpu/z80.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

#define UNI_CPU_HZ 2500000
#define UNI_SAMPLE_RATE 44100

// The tiles the video timing chip shows: 32 across, 28 down (of the 32
// rows in video RAM), 8x8 pixels each.
#define UNI_OVERLAY_W 256
#define UNI_OVERLAY_H 224

// Where they sit on the picture, as fractions of its width and height. The
// timing chip (as the game sets it up) shows 32 character times of 1.6 us
// on each line, 51.2 us of the 52.6 us a TV line shows, and 224 of a
// field's 240 visible lines; both centred, as far as can be told without a
// real cabinet to compare.
#define UNI_OVERLAY_AREA_W (51.2f / 52.6f)
#define UNI_OVERLAY_AREA_H (224.0f / 240.0f)

// Settings the DIP switches hold, which the main program offers by name.
typedef enum {
    UNI_FREE_PLAY,
    UNI_UNLIMITED_LIVES,
    UNI_QUIET_ATTRACT,  // on: no sound in the attract mode ("demo sounds" off)
} UniSetting;

// Inputs are active low on the real board; here, true means pressed.
typedef struct {
    bool up, down, left, right, button;
    bool start1, start2, coin1, coin2;
} UniInputs;

typedef struct {
    Z80 cpu;
    uint8_t rom[0x4000];
    uint8_t ram[0x800];
    uint8_t video_ram[0x400];
    uint8_t tiles[0x2000];     // tile graphics
    uint32_t palette[32];      // colour PROM decoded, as 0xAARRGGBB

    uint8_t dsw1, dsw2;
    UniInputs inputs;
    Ldv1000 *player;
    uint8_t ld_in, ld_out;     // the latches between the board and the player
    uint8_t misc;              // last byte written to port 08

    Sn76496 sound;
    uint64_t cycles;           // CPU clock cycles since power-on
    uint64_t fields;           // video fields since power-on
    uint64_t next_field;       // cycle the next field starts on
    unsigned overlay_serial;   // changes whenever the overlay's look may have changed
} UniBoard;

// Does path hold a ROM set for this board? Quiet: for choosing a board.
bool uni_recognizes(const char *path);

// Load the ROMs from a zip file or a folder, checking each chip's checksum,
// and set the DIP switches as the factory did. Returns false (after
// printing why) if the set can't be run.
bool uni_load_roms(UniBoard *board, const char *path);

void uni_set(UniBoard *board, UniSetting setting, bool on);
bool uni_get(const UniBoard *board, UniSetting setting);

const char *uni_game_name(const UniBoard *board);

// Power on, connected to an LD-V1000. (The DIP switches stay as they are.)
void uni_reset(UniBoard *board, Ldv1000 *player);

// Run the board for (at least) the given number of CPU cycles.
void uni_run(UniBoard *board, uint64_t cycles);

// Draw the tiles as they look now: UNI_OVERLAY_W x UNI_OVERLAY_H pixels,
// as 0xAARRGGBB, alpha 0 where the picture shows through.
void uni_draw_overlay(const UniBoard *board, uint32_t *pixels);

#endif
