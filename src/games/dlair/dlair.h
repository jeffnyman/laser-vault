// dlair.h - the Dragon's Lair game board (US version, Pioneer LD-V1000).
//
// The board is small: a Z80 CPU at 4 MHz, 40 KB of ROM space (most sets
// use 32 KB), 2 KB of RAM, an AY-3-8910 sound chip (which also reads
// the DIP switches), the joystick and buttons, a 16-digit score display, and
// an 8-bit parallel link to the laserdisc player. A timer interrupts the CPU
// about 30.5 times a second.
//
// The game's whole world is in its address space:
//
//     0000-9FFF  ROM
//     A000-A7FF  RAM (repeated through BFFF)
//     C000-DFFF  inputs: read from C000 + (sound chip, joystick, coins/
//                start and player strobes, laserdisc data) * 8
//     E000-FFFF  outputs: sound chip, misc latch, laserdisc data, score
//                display digits
//
// Details come from the schematics as documented in MAME's dlair.cpp.

#ifndef LV_DLAIR_H
#define LV_DLAIR_H

#include "audio/ay8910.h"
#include "cpu/z80.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

#define DLAIR_CPU_HZ 4000000
#define DLAIR_SOUND_CHIP_HZ 2000000  // the AY-3-8910 runs at 16 MHz / 8
#define DLAIR_SAMPLE_RATE 44100      // rate of the sound chip's output samples

// DIP switch B:1, "Sound Every 8 Attracts". On (the default) keeps the
// attract mode quiet: the game switches the disc's sound off and only lets
// it through every few attract cycles. Off: the attract mode always has sound.
#define DLAIR_DSW2_QUIET_ATTRACT 0x01

// DIP switch B:3, "Unlimited Dirks". On, Dirk still dies but never loses a
// life, so a player can see the whole game. Off by default.
#define DLAIR_DSW2_UNLIMITED_LIVES 0x04

// How the board talks to the laserdisc player. The player drives two
// strobe lines that tell the game when to read status and when to send
// the next command byte. Every call passes the current time ("now", in CPU
// clock cycles since power-on) so the player can bring itself up to date.
typedef struct {
    void *ctx;
    uint8_t (*read_status)(void *ctx, uint64_t now);              // byte at C020
    void (*send_command)(void *ctx, uint64_t now, uint8_t byte);  // byte latched to the player
    bool (*status_strobe)(void *ctx, uint64_t now);  // true while the strobe is active
    bool (*command_strobe)(void *ctx, uint64_t now);
} DlairLaserdisc;

// Inputs are active low on the real board; here, true means pressed.
typedef struct {
    bool up, down, left, right, sword;
    bool start1, start2, coin1, coin2;
} DlairInputs;

typedef struct {
    Z80 cpu;
    uint8_t rom[0xA000];
    uint8_t ram[0x800];

    uint8_t dsw1, dsw2;      // DIP switches (read through the sound chip)
    DlairInputs inputs;
    DlairLaserdisc laserdisc;

    Ay8910 ay;               // the sound chip
    uint8_t ay_select;       // sound chip register selected by the CPU
    uint8_t ld_latch;        // byte waiting to be sent to the player
    uint8_t misc;            // last value written to the misc latch
    uint8_t digits[16];      // score display: 0-9, 10-14 = A b C d E, 15 = blank

    uint64_t cycles;         // CPU clock cycles since power-on
    uint64_t next_irq;
} Dlair;

// Load the ROMs from a zip file or a folder of .bin files. This works out
// which set they are (rev F2, another revision, or a fan-made set), checks
// each chip's checksum, and sets the DIP switches as that set expects.
// Returns false (after printing why) if it finds no set it can run.
bool dlair_load_roms(Dlair *board, const char *path);

// A laserdisc link to an LD-V1000 model, the player this board was built for.
DlairLaserdisc dlair_ldv1000_link(Ldv1000 *player);

// Power on: reset the CPU. (The DIP switches stay as they are.)
void dlair_reset(Dlair *board, DlairLaserdisc laserdisc);

// Run the board for (at least) the given number of CPU cycles.
void dlair_run(Dlair *board, uint64_t cycles);

#endif
