// board.h - Gottlieb's laserdisc game board (Mylstar's, by then), which runs
// Us vs. Them (1984).
//
// Gottlieb's arcade board of the time (Q*bert's), with a laserdisc
// interface added: an 8088 driving a Pioneer PR-8210 (see ldp/pr8210.h),
// with tiles and sprites drawn over the picture, and a sound board of two
// 6502s.
//
//     8088 at 5 MHz (the address space repeats every 64 KB)
//     0000-0FFF  RAM, kept with a battery
//     1000-2FFF  RAM
//     3000-30FF  sprites: 64 of four bytes (y, x, the tile number inverted, unused)
//     3800-3BFF  tile map: 32 x 32 tile numbers
//     5000-501F  colours: 16, two bytes each (green and blue, then red)
//     5800  read: DIP switches                 write: watchdog
//     5801  read: coins, start buttons, service, tilt
//     5802  read: (trackball, unused)          write: a command for the sound board
//     5803                                     write: video control
//     5804  read: joystick and buttons
//     5805  read: the frame number's low two BCD digits   write: a command for the player
//     5806  read: its middle two digits                   write: what 5807 reads
//     5807  read: its top digit and the player's status, or the disc's data channel
//     6000-FFFF  ROM
//
// The NMI comes at each vertical blank, the board's picture being locked to
// the disc's (59.94 fields a second). A byte written to 5805 is sent to the
// player as remote-control pulses: the board clocks it out, top bit first,
// then four zeros (the PR-8210's 10-bit code with two of its own after),
// and 5807 says when it has finished. The frame number comes from the
// Philips codes on lines 17 and 18 of each field, which the board's decoder
// keeps the last valid one of. (The interface can also read data recorded
// on the disc's second audio channel, which M.A.C.H. 3 uses for its
// targets; Us vs. Them doesn't.)
//
// Video control (5803): bit 0, the tiles in front of the sprites; bit 1,
// the second bank of sprites; bit 2, the display on; bit 3, the disc's
// picture on, behind colour 0. The tiles are 8x8 and the sprites 16x16,
// four bits a pixel, from ROM.
//
// The sound board ("rev. 2"): two 6502s at 1 MHz, each latching the main
// board's commands with an interrupt. The first drives an 8-bit DAC, whose
// volume a second DAC sets. The second drives two AY-3-8913s and an SP0250
// speech chip, and has a timer for its NMI; it can raise an NMI on the
// first. The speech chip isn't emulated yet.
//
//     first 6502                           second 6502
//     0000-03FF  RAM (repeats to 3FFF)     0000-03FF  RAM (repeats to 1FFF)
//     4000  write: DAC volume              2000  write: speech data
//     4001  write: DAC                     4000  write: control (see board.c)
//     8000  read: the command              6000  read: switches, speech ready
//     C000-FFFF  ROM                       8000  write: data for an AY-3-8913
//                                          A000  write: NMI rate
//                                          A800  read: the command
//                                          B000  NMI to the first 6502
//                                          C000-FFFF  ROM
//
// Details from MAME's gottlieb.cpp (Fabrice Frances, Nicola Salmoria, Aaron
// Giles) and Hypseus's mach3.cpp (Matt Ownby and others).

#ifndef LV_GOTTLIEB_BOARD_H
#define LV_GOTTLIEB_BOARD_H

#include "audio/ay8910.h"
#include "cpu/i8088.h"
#include "cpu/m6502.h"
#include "ldp/pr8210.h"

#include <stdbool.h>
#include <stdint.h>

#define GOTTLIEB_CPU_HZ 5000000
#define GOTTLIEB_SOUND_HZ 1000000
#define GOTTLIEB_SAMPLE_RATE 44100

// The overlay: the tile map's 32 x 30 shown rows.
#define GOTTLIEB_OVERLAY_W 256
#define GOTTLIEB_OVERLAY_H 240

typedef enum {
    GOTTLIEB_FREE_PLAY,
} GottliebSetting;

// True means pressed (the board reads most of them active high anyway).
typedef struct {
    bool up, down, left, right;
    bool button1, button2, button3;
    bool start1, start2, coin1, coin2;
} GottliebInputs;

typedef struct {
    I8088 cpu;
    uint8_t ram[0x3000];        // 0000-2FFF
    uint8_t sprites[0x100];     // 3000-30FF
    uint8_t tiles[0x400];       // 3800-3BFF
    uint8_t colours[0x20];      // 5000-501F
    uint8_t rom[0xA000];        // 6000-FFFF
    uint8_t tile_gfx[0x2000];   // 256 tiles
    uint8_t sprite_gfx[0x10000];  // 512 sprites, four planes of 0x4000
    uint8_t video_control;      // 5803
    uint8_t dsw;
    GottliebInputs inputs;

    // The laserdisc interface.
    Pr8210 *player;
    bool data_select;           // 5806 bit 0: 5807 reads the status (else the data channel)
    uint32_t philips_code;      // the last valid code read off the disc
    bool disc_ready;            // a picture at the last vertical blank
    int search_fields;          // fields left of the picture lost to a search (see board.c)
    uint8_t command;            // the byte being clocked out to the player
    int pulses_left;            // 0 when idle
    uint64_t next_pulse;        // when the next pulse starts
    uint64_t pulse_end;         // when the current one ends (0 for none)
    uint64_t next_field;        // when the next vertical blank comes

    // The sound board.
    M6502 dcpu, ycpu;
    uint8_t d_ram[0x400], y_ram[0x400];
    uint8_t d_rom[0x4000];      // C000-FFFF
    uint8_t y_rom[0x4000];
    uint8_t d_latch, y_latch;   // the command, for each CPU
    uint8_t last_command;
    bool d_irq, y_irq;
    uint8_t speech_control;     // the second CPU's control register
    uint8_t psg_latch;
    uint8_t ay_reg[2];
    uint8_t nmi_rate;
    uint64_t next_nmi;          // in sound CPU cycles
    Ay8910 ay[2];
    uint8_t dac, dac_volume;
    float dac_in, dac_out;      // the output's DC-blocking filter: its last input and output
    uint64_t d_cycles, y_cycles;  // each sound CPU's clock cycles since power-on
    double next_sample;         // in sound CPU cycles
    bool sound_on;
    float *dac_samples;
    int dac_count, dac_capacity;
    float *samples;             // the mix
    int sample_capacity;

    uint64_t cycles;            // main CPU clock cycles since power-on
    unsigned overlay_serial;    // changes whenever the overlay may look different
} GottliebBoard;

// Does path hold a ROM set for this board? Quiet: for choosing a board.
bool gottlieb_recognizes(const char *path);

// Load the ROMs from a zip file or a folder, checking each chip's checksum.
// Returns false (after printing why) if the set can't be run.
bool gottlieb_load_roms(GottliebBoard *board, const char *path);
void gottlieb_free(GottliebBoard *board);

const char *gottlieb_game_name(const GottliebBoard *board);

void gottlieb_set(GottliebBoard *board, GottliebSetting setting, bool on);
bool gottlieb_get(const GottliebBoard *board, GottliebSetting setting);

// Power on, connected to a PR-8210.
void gottlieb_reset(GottliebBoard *board, Pr8210 *player);

// Run the board for (at least) the given number of CPU cycles.
void gottlieb_run(GottliebBoard *board, uint64_t cycles);

void gottlieb_start_sound(GottliebBoard *board);
int gottlieb_take_samples(GottliebBoard *board, const float **samples);

// Draw the tiles and sprites as they look now: GOTTLIEB_OVERLAY_W x
// GOTTLIEB_OVERLAY_H pixels, as 0xAARRGGBB, alpha 0 where the picture shows
// through.
void gottlieb_draw_overlay(const GottliebBoard *board, uint32_t *pixels);

#endif
