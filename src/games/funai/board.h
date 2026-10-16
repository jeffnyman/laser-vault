// board.h - Funai's laserdisc game board, which runs Esh's Aurunmilla
// (Funai/Gakken, 1984).
//
// Another Z80 driving a Pioneer LD-V1000, with tiles drawn over the
// picture like Universal's board, but built differently:
//
//     Z80 at 3.072 MHz (18.432 MHz / 6)
//     0000-3FFF  ROM (16 KB)
//     E000-E7FF  RAM (2 KB; the top part is kept with a battery)
//     F000-F3FF  tile RAM: 32 x 32 tile numbers, by rows
//     F400-F7FF  tile attributes: colour (bits 0-3), second 256 tiles
//                (bit 4), blinking (bits 6-7)
//
// and the Z80's I/O ports:
//
//     in  F0  coins, start buttons, service
//     in  F1  joystick and buttons, player 1   in  F2  the same, player 2
//     in  F4  laserdisc status                 out F4  laserdisc command
//     out F5  beeper (bit 1), picture off (bit 3)
//     out F8-FD  button lamps
//     out FE  pulsed at each vertical-blank interrupt
//     out FF  bit 0: let the player's command strobe interrupt (NMI)
//
// The vertical-blank interrupt runs the game; the laserdisc link runs in
// the NMI, which the player's command strobe raises once a field: the game
// reads the status and leaves the next byte for the player.
//
// Its settings live in the battery-backed RAM rather than on DIP switches.
// Details from MAME's esh.cpp, checked against the game's code.

#ifndef LV_FUNAI_BOARD_H
#define LV_FUNAI_BOARD_H

#include "cpu/z80.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

#define FUNAI_CPU_HZ 3072000
#define FUNAI_SAMPLE_RATE 44100

// The tile grid, all of which the overlay shows.
#define FUNAI_OVERLAY_W 256
#define FUNAI_OVERLAY_H 256

// Inputs are active low on the real board; here, true means pressed.
typedef struct {
    bool up, down, left, right, button1, button2;
    bool start1, start2, coin1, coin2;
} FunaiInputs;

#define FUNAI_MAX_SAMPLES 4096

typedef struct {
    Z80 cpu;
    uint8_t rom[0x4000];
    uint8_t ram[0x800];
    uint8_t tile_ram[0x400];
    uint8_t attr_ram[0x400];
    uint8_t tiles[0x3000];      // tile graphics: three bit planes of 512 tiles
    uint32_t palette[0x200];    // colour PROM decoded, as 0xAARRGGBB

    FunaiInputs inputs;
    Ldv1000 *player;
    uint8_t ld_out;             // the byte waiting for the player
    uint8_t misc;               // last byte written to port F5
    uint8_t lamps[6];           // ports F8-FD: start 1, start 2, player 1's
                                // buttons 1 and 2, player 2's buttons 1 and 2
    bool nmi_enabled;           // port FF bit 0

    // The beeper: a square wave while port F5's bit 1 is set.
    bool rendering;
    double next_sample;         // CPU cycle the next sample is due
    double beep_phase;
    float samples[FUNAI_MAX_SAMPLES];
    int sample_count;

    uint64_t cycles;            // CPU clock cycles since power-on
    uint64_t fields;            // video fields since power-on
    uint64_t next_field;        // cycle the next field starts on
    uint64_t next_strobe;       // cycle of the player's next command strobe
    unsigned overlay_serial;    // changes whenever the overlay's look may have changed
} FunaiBoard;

// Does path hold a ROM set for this board? Quiet: for choosing a board.
bool funai_recognizes(const char *path);

// Load the ROMs from a zip file or a folder, checking each chip's checksum.
// Returns false (after printing why) if the set can't be run.
bool funai_load_roms(FunaiBoard *board, const char *path);

const char *funai_game_name(const FunaiBoard *board);

// Power on, connected to an LD-V1000.
void funai_reset(FunaiBoard *board, Ldv1000 *player);

// Run the board for (at least) the given number of CPU cycles.
void funai_run(FunaiBoard *board, uint64_t cycles);

// The beeper: switch making samples on, and take what's been made.
void funai_start_sound(FunaiBoard *board);
int funai_take_samples(FunaiBoard *board, const float **samples);

// Draw the tiles as they look now: FUNAI_OVERLAY_W x FUNAI_OVERLAY_H
// pixels, as 0xAARRGGBB, alpha 0 where the picture shows through.
void funai_draw_overlay(const FunaiBoard *board, uint32_t *pixels);

#endif
