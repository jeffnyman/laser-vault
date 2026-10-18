// board.h - Leland's laserdisc game board, which runs Dragon's Lair II:
// Time Warp (1991).
//
// It's built much like an IBM PC XT: an 8088 at 10 MHz, PC-style chips
// around it at the usual addresses, and a 64 KB ROM that holds a small
// BIOS and the game. Sony's LDP-1450 player is on the second serial port,
// and draws the game's text (score, lives, messages) over the picture
// itself (see ldp/ldp1000.h); the board has no graphics of its own.
//
//     00000-EFFFF  RAM (the game is copied to 10000 and runs there)
//     F0000-FFFFF  ROM
//
//     ports
//     020, 021  the interrupt controller (an 8259): the timer is IRQ 0
//               (interrupt 8), the serial port IRQ 3 (interrupt 0B)
//     040-043   the timer (an 8253): channel 0 for IRQ 0, channel 2 for
//               the speaker's tone
//     061       bit 0: the tone on, bit 1: the speaker on
//     201       read: the joystick, start buttons, sword and service
//               switch, active low
//     202       read: bit 0 the EEPROM's data, bits 2-5 the coin slots
//               write: bits 0-2 the EEPROM's data, clock and select;
//               bit 6 acknowledges coins
//     2F8-2FF   the serial port (an 8250), to the player at 9600 baud
//
// The game keeps its settings and bookkeeping in a serial EEPROM, 16-bit
// words with 8-bit addresses, as a 93C66 has. A small microcontroller
// counts coins: the game shows its count by writing 00 to 202, reads it,
// then writes 40 to take the coins it saw.
//
// Sound is the disc's soundtrack, and the PC speaker for beeps.
//
// The ports and how the game uses them, from Daphne's lair2.cpp and MAME's
// dlair2.cpp (a skeleton), checked against the game's code.

#ifndef LV_LELAND_BOARD_H
#define LV_LELAND_BOARD_H

#include "cpu/i8088.h"
#include "ldp/ldp1000.h"

#include <stdbool.h>
#include <stdint.h>

#define LELAND_CPU_HZ 10000000
#define LELAND_SAMPLE_RATE 44100

// Inputs are active low on the real board; here, true means pressed.
typedef struct {
    bool up, down, left, right;
    bool sword;
    bool start1, start2, coin1, coin2;
    bool service;
} LelandInputs;

typedef struct {
    I8088 cpu;
    uint8_t *ram;  // 00000-EFFFF
    uint8_t rom[0x10000];
    const char *title;

    LelandInputs inputs;
    bool moves_known;       // the version autoplay can read (see board.c)
    bool autoplay;          // the board plays each move itself

    // The interrupt controller.
    uint8_t pic_irr, pic_isr, pic_imr, pic_base, pic_lines;
    int pic_init_step;  // 0 when set up; else the next ICW expected
    bool pic_icw4, pic_single, pic_read_isr, pic_auto_eoi;

    // The timer's three channels.
    struct {
        uint16_t count;      // the reload value (0 means 65536)
        uint8_t mode, access;  // access: 1 low byte, 2 high byte, 3 both
        bool high_next;      // the next byte read or written is the high one
        bool latched;
        uint16_t latch;
        uint8_t low_written;
        uint64_t loaded;     // when counting began, in timer ticks
    } pit[3];
    uint64_t next_irq0;     // CPU cycle of the next timer interrupt (0: none)
    uint8_t port61;

    // The serial port, and the player at the other end.
    Ldp1000 *player;
    uint8_t ier, lcr, mcr, scr, dll, dlm;
    bool rx_ready;
    uint8_t rx_data;
    bool thr_full, tsr_busy, thre_pending;
    uint8_t thr, tsr;
    uint64_t tsr_done;      // when the byte being sent reaches the player
    uint64_t next_rx;       // when the line can bring the next byte

    // Coins, as the coin counter sees them.
    int coins_waiting[2], coins_seen[2];
    bool coin_was_in[2];
    uint8_t port202;

    // The EEPROM.
    uint16_t eeprom[256];
    bool eeprom_writable;
    int ee_bits;            // bits received since chip select
    uint32_t ee_shift;
    int ee_op, ee_addr;     // the command, once its bits are in (-1: not yet)
    int ee_out_bits;        // data bits left to send
    uint16_t ee_out;
    bool ee_do;             // the data output

    // The speaker.
    float *samples;
    int sample_capacity, sample_count;
    bool sound_on;
    double sample_clock;    // CPU cycles until the next sample
    double tone_phase;

    uint64_t cycles;        // CPU clock cycles since power-on
} LelandBoard;

bool leland_recognizes(const char *path);
bool leland_load_roms(LelandBoard *board, const char *path);
void leland_free(LelandBoard *board);
const char *leland_game_name(const LelandBoard *board);

// Can autoplay play this version? (3.19.) With it on, the board presses
// each move as its window opens, as well as the player's controls.
bool leland_can_autoplay(const LelandBoard *board);
void leland_set_autoplay(LelandBoard *board, bool on);

// Power on, connected to an LDP-1450.
void leland_reset(LelandBoard *board, Ldp1000 *player);

// Run the board for (at least) the given number of CPU cycles.
void leland_run(LelandBoard *board, uint64_t cycles);

void leland_start_sound(LelandBoard *board);
int leland_take_samples(LelandBoard *board, const float **samples);

#endif
