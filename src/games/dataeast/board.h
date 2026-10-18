// board.h - Data East's laserdisc game board, which runs Bega's Battle
// (1983), Data East's own version of Cobra Command (1984) and Road Blaster
// (1985).
//
// Two 6502s, one for the game and one for its sound, driving Sony's
// LDP-1000 over a serial line (see ldp/ldp1000.h), with two layers of
// tiles and sprites drawn over the picture and two AY-3-8910s for sound:
//
//     main 6502 at 2.5 MHz
//     0000-0FFF  RAM
//     1000  read: joystick and buttons        write: coin lockout
//     1001  read: coins, start buttons,       write: (acknowledges a coin)
//           DIP switches 1 (coinage)
//     1002  read: DIP switches 2
//     1003  read: vertical blank (bit 7)
//     1004  write: a command for the sound CPU (raising its IRQ)
//     1006, 1007  the serial chip (a 6850): control and status, data
//     1800-1FFF  colours: one byte each, 3 bits red, 3 green, 2 blue,
//                inverted
//     2000-23FF  tile layer 0: 32 x 32 tile numbers   2400-27FF  their
//                attributes (bits 0-1: the tile number's top bits)
//     3000-33FF, 3400-37FF  tile layer 1, the same way
//     4000-FFFF  ROM
//
//     sound 6502 at 2.5 MHz
//     0000-07FF  RAM
//     2000, 4000  first AY-3-8910: data, register
//     6000, 8000  second AY-3-8910: data, register
//     A000  read: the main CPU's command
//     E000-FFFF  ROM
//
// The serial chip's interrupt, when a byte arrives from the player, is the
// main CPU's IRQ; a coin raises its NMI. The sound CPU's NMI comes 640
// times a second and runs its music and effects.
//
// The tile layers' top and bottom rows aren't shown: they hold each
// layer's sprites, eight of them in each row, four bytes apiece (bit 0
// shown, bit 1 flipped vertically, bit 2 horizontally; the sprite's number;
// y; x). Each layer has its own three graphics chips, for its tiles (8x8)
// and its sprites (16x16), three bits a pixel.
//
// Details from MAME's deco_ld.cpp (which doesn't get the game going) and
// Daphne's bega.cpp, checked against the game's code; the coins, for
// instance, read as 1 when inserted, though MAME has them the other way.

#ifndef LV_DATAEAST_BOARD_H
#define LV_DATAEAST_BOARD_H

#include "audio/ay8910.h"
#include "cpu/m6502.h"
#include "ldp/ldp1000.h"

#include <stdbool.h>
#include <stdint.h>

#define DECO_CPU_HZ 2500000
#define DECO_SAMPLE_RATE 44100

// The overlay: the tile layers' 32 x 30 shown rows.
#define DECO_OVERLAY_W 256
#define DECO_OVERLAY_H 240

// Inputs are active low on the real board (but for the coins); here, true
// means pressed.
typedef struct {
    bool up, down, left, right;
    bool button1, button2, button3;  // bits 6, 5 and 4 of 1000; the first fires
    bool start1, start2, coin1, coin2;
} DecoInputs;

typedef enum {
    DECO_BEGAS,
    DECO_COBRA,
    DECO_RBLASTER,
} DecoGame;

typedef enum {
    DECO_FREE_PLAY,
    DECO_QUIET_ATTRACT,
} DecoSetting;

typedef struct {
    DecoGame game;
    M6502 cpu, sound_cpu;
    uint8_t rom[0xC000];        // 4000-FFFF
    uint8_t sound_rom[0x2000];  // E000-FFFF
    uint8_t ram[0x1000];
    uint8_t sound_ram[0x800];
    uint8_t colours[0x800];     // 1800-1FFF
    uint8_t video[0x2000];      // 2000-3FFF, layer 0 at 0000, layer 1 at 1000
    uint8_t gfx[0xC000];        // graphics chips, three planes of 0x4000

    DecoInputs inputs;
    bool autoplay;              // the board plays Cobra Command itself (see board.c)
    uint64_t auto_window;       // cycle the open target window opened, or 0
    uint64_t auto_missile;      // cycle of autoplay's last missile press
    uint8_t dsw1, dsw2;
    bool coin_was_in;           // for raising the NMI as a coin goes in
    uint8_t sound_command;      // the latch from the main CPU
    bool sound_irq;             // ... waiting to be read
    Ay8910 ay[2];
    uint8_t ay_reg[2];          // each chip's selected register
    float *samples;             // the two chips mixed
    int sample_capacity;

    // The serial chip, and the player at the other end.
    Ldp1000 *player;
    uint8_t acia_control;
    bool rx_full, tx_busy;
    uint8_t rx_data, tx_data;
    uint64_t next_serial;       // when the line next moves a byte

    uint64_t cycles;            // main CPU clock cycles since power-on
    uint64_t sound_cycles;      // the sound CPU's (same clock)
    uint64_t sound_nmis;        // its NMIs since power-on
    unsigned overlay_serial;    // changes whenever the overlay's look may have changed
} DecoBoard;

// Does path hold a ROM set for this board? Quiet: for choosing a board.
bool deco_recognizes(const char *path);

// Load the ROMs from a zip file or a folder, checking each chip's checksum.
// Returns false (after printing why) if the set can't be run.
bool deco_load_roms(DecoBoard *board, const char *path);

const char *deco_game_name(const DecoBoard *board);

void deco_set(DecoBoard *board, DecoSetting setting, bool on);
bool deco_get(const DecoBoard *board, DecoSetting setting);

// Can autoplay play this game? (Cobra Command.) With it on, the board flies
// and shoots as each scene asks, as well as the player's controls.
bool deco_can_autoplay(const DecoBoard *board);
void deco_set_autoplay(DecoBoard *board, bool on);

// Power on, connected to an LDP-1000.
void deco_reset(DecoBoard *board, Ldp1000 *player);

// Run the board for (at least) the given number of CPU cycles.
void deco_run(DecoBoard *board, uint64_t cycles);

void deco_start_sound(DecoBoard *board);
int deco_take_samples(DecoBoard *board, const float **samples);

// Draw the tiles and sprites as they look now: DECO_OVERLAY_W x
// DECO_OVERLAY_H pixels, as 0xAARRGGBB, alpha 0 where the picture shows
// through.
void deco_draw_overlay(const DecoBoard *board, uint32_t *pixels);

#endif
