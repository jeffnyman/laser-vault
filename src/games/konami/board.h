// board.h - Konami's laserdisc game board (GX455), which runs Badlands
// (Konami/Centuri, 1984).
//
// A Motorola 6809 driving a Pioneer LD-V1000, with tiles drawn over the
// picture and an SN76496 for sound effects:
//
//     6809 at 1.536 MHz (18.432 MHz / 12)
//     0000  read: DIP switches 2 (lives,      write: load the sound latch
//           demo sounds)                             into the sound chip
//     0800  read: DIP switches 1 (coinage)    write: laserdisc command
//     1000  read: laserdisc status            write: LED
//     1001, 1002  coin counters
//     1003  picture off (blank tiles opaque)  1005  laserdisc audio on
//     1004  NMI on     1006  IRQ on     1007  FIRQ on (writing 0 also
//           clears an interrupt waiting to be taken)
//     1800  read: coins, start buttons, fire  write: the sound latch
//     4000-47FF  tile RAM: 64 x 32 tile numbers, by rows
//     4800-4BFF  RAM
//     5800  watchdog
//     C000-FFFF  ROM (16 KB)
//
// The interrupts split the work: the NMI, at vertical blank, reads the
// player's status and leaves the next command byte; the IRQ, raised by the
// player's command strobe as it takes that byte, runs the game; the FIRQ,
// eight times a field, runs the sound effects. (The order matters: the
// game queues commands for the NMI to send, and an NMI in the middle of
// queueing one would send half a command.)
//
// The sound chip's data lines are wired in reverse order, so the game
// reverses each byte's bits before writing it.
//
// Details from MAME's konblands.cpp (which doesn't get the game going),
// checked against the game's code, which corrects it in places: MAME has
// the DIP switch banks the other way round.

#ifndef LV_KONAMI_BOARD_H
#define LV_KONAMI_BOARD_H

#include "audio/sn76496.h"
#include "cpu/m6809.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

#define KONAMI_CPU_HZ 1536000
#define KONAMI_SAMPLE_RATE 44100

// The part of the 64 x 32 tile grid on screen: 40 x 30 tiles from column
// 4, row 2. (The Daphne emulator starts at column 6; from 4 the game's
// centred text, such as the hero's name, is centred.)
#define KONAMI_OVERLAY_W 320
#define KONAMI_OVERLAY_H 240

// Inputs are active low on the real board; here, true means pressed.
typedef struct {
    bool fire;
    bool start1, start2, coin1, coin2;
} KonamiInputs;

// Settings the DIP switches hold, which the main program offers by name.
typedef enum {
    KONAMI_FREE_PLAY,
    KONAMI_QUIET_ATTRACT,  // on: no sound in the attract mode ("demo sounds" off)
} KonamiSetting;

typedef struct {
    M6809 cpu;
    uint8_t rom[0x4000];
    uint8_t tile_ram[0x800];
    uint8_t ram[0x400];
    uint8_t tiles[0x2000];      // tile graphics: 256 tiles of 8x8 pixels, 4 bits each
    uint32_t palette[16];       // colour PROM decoded, as 0xAARRGGBB

    KonamiInputs inputs;
    bool autoplay;              // the board makes each shot itself (see board.c)
    uint8_t dsw1, dsw2;
    Ldv1000 *player;
    uint8_t ld_out;             // the byte waiting for the player
    uint8_t sound_latch;        // the byte waiting for the sound chip
    Sn76496 sound;
    uint8_t controls[8];        // last values written to 1000-1007

    // Interrupts: enabled (by 1004, 1006, 1007), and waiting to be taken.
    bool irq_pending, firq_pending;

    uint64_t cycles;            // CPU clock cycles since power-on
    uint64_t fields;            // video fields since power-on
    uint64_t next_field;        // cycle the next field starts on
    uint64_t next_strobe;       // cycle of the player's next command strobe
    uint64_t firq_ticks;        // FIRQs since power-on
    uint64_t next_firq;         // cycle of the next FIRQ
    unsigned overlay_serial;    // changes whenever the overlay's look may have changed
} KonamiBoard;

// Does path hold a ROM set for this board? Quiet: for choosing a board.
bool konami_recognizes(const char *path);

// Load the ROMs from a zip file or a folder, checking each chip's checksum.
// Returns false (after printing why) if the set can't be run.
bool konami_load_roms(KonamiBoard *board, const char *path);

const char *konami_game_name(const KonamiBoard *board);

void konami_set(KonamiBoard *board, KonamiSetting setting, bool on);
bool konami_get(const KonamiBoard *board, KonamiSetting setting);

// Power on, connected to an LD-V1000.
void konami_reset(KonamiBoard *board, Ldv1000 *player);

// With autoplay on, the board fires each shot the game is waiting for, as
// well as the player's controls.
void konami_set_autoplay(KonamiBoard *board, bool on);

// Run the board for (at least) the given number of CPU cycles.
void konami_run(KonamiBoard *board, uint64_t cycles);

// Can the disc's audio be heard? (The game switches it off with 1005.)
bool konami_disc_audio_on(const KonamiBoard *board);

// Draw the tiles as they look now: KONAMI_OVERLAY_W x KONAMI_OVERLAY_H
// pixels, as 0xAARRGGBB, alpha 0 where the picture shows through.
void konami_draw_overlay(const KonamiBoard *board, uint32_t *pixels);

#endif
