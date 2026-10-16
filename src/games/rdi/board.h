// board.h - RDI Video Systems' board, which runs Thayer's Quest (1984).
//
// A Z80 drives a Pioneer LD-V1000, with a COP421 microcontroller reading
// the keyboard and an SSI-263 speech chip for the narrator. Nothing is
// drawn over the picture; a row of LED digits on the cabinet shows the
// time and score.
//
//     Z80 at 4 MHz
//     0000-7FFF  ROM (U33)      8000-BFFF  RAM (16 KB)
//     C000-DFFF  ROM (U1, on a plug-in card)
//
// and the Z80's I/O ports:
//
//     00-07  the speech chip's registers (read: its status)
//     20     bits 5-7: a command for the COP421
//     40     read: interrupt sources (bit 2 speech chip ready, bit 4
//            timer, bit 5 key ready; low means active)
//     80     the latch to and from the COP421
//     A0, C0 acknowledge the COP421's timer and key interrupts
//     F0     read: laserdisc status
//     F1     read: coins (bits 4, 5), the player's status and command
//            strobes (bits 6, 7, low while active), DIP switches B (0-3)
//     F2     read: DIP switches A
//     F3     acknowledge the periodic interrupt (it comes 8.25 ms later)
//     F4     the laserdisc command byte; F5 bit 5 puts it on the link
//     F6, F7 the LED digits
//
// One interrupt line, raised by any of four sources: the COP421's timer
// and key-ready signals, the speech chip asking for its next phoneme, and
// a periodic one-shot the game re-arms itself. The game reads port 40 to
// see which.
//
// The keyboard sends its keys to the COP421 serially, a row of four keys
// at a time (see keyboard_clock), and the COP421 passes them on.
//
// Details from MAME's thayers.cpp.

#ifndef LV_RDI_BOARD_H
#define LV_RDI_BOARD_H

#include "audio/ssi263.h"
#include "cpu/cop421.h"
#include "cpu/z80.h"
#include "ldp/ldv1000.h"

#include <stdbool.h>
#include <stdint.h>

#define RDI_CPU_HZ 4000000
#define RDI_SAMPLE_RATE 44100

// The keyboard: ten rows of four keys. The key in row r, bit b is number
// r * 4 + b.
enum {
    RDI_KEY_YES, RDI_KEY_Q, RDI_KEY_1, RDI_KEY_2,
    RDI_KEY_ITEMS, RDI_KEY_W, RDI_KEY_A, RDI_KEY_Z,
    RDI_KEY_DROP, RDI_KEY_E, RDI_KEY_S, RDI_KEY_X,
    RDI_KEY_GIVE_SCORE, RDI_KEY_R, RDI_KEY_D, RDI_KEY_C,
    RDI_KEY_REPLAY, RDI_KEY_T, RDI_KEY_F, RDI_KEY_V,
    RDI_KEY_COMBINE, RDI_KEY_Y, RDI_KEY_G, RDI_KEY_B,
    RDI_KEY_SAVE, RDI_KEY_U, RDI_KEY_H, RDI_KEY_N,
    RDI_KEY_UPDATE, RDI_KEY_I, RDI_KEY_J, RDI_KEY_M,
    RDI_KEY_HINT, RDI_KEY_O, RDI_KEY_K, RDI_KEY_4,
    RDI_KEY_NO, RDI_KEY_P, RDI_KEY_L, RDI_KEY_3,
    RDI_KEY_COUNT
};

typedef struct {
    bool keys[RDI_KEY_COUNT];
    bool coin1, coin2;
} RdiInputs;

typedef enum {
    RDI_FREE_PLAY,
    RDI_EXTRA_LIVES,     // 5 lives instead of 3
    RDI_QUIET_ATTRACT,   // no sound in the attract mode ("demo sounds" off)
} RdiSetting;

typedef struct {
    Z80 cpu;
    Cop421 cop;
    Ssi263 speech;
    uint8_t rom_u33[0x8000];
    uint8_t rom_u1[0x2000];
    uint8_t rom_cop[0x400];    // the COP421's own program
    uint8_t ram[0x4000];

    RdiInputs inputs;
    uint8_t dswa, dswb;
    Ldv1000 *player;
    uint8_t ld_out;            // the byte on the laserdisc latch
    uint8_t ld_control;        // port F5

    // The COP421's connections.
    uint8_t cop_latch;         // written by the Z80 (port 80)
    bool cop_latch_out;        // COP G3: the latch drives the COP's L port
    uint8_t cop_l;             // what the COP put on L
    uint8_t cop_command;       // port 20 bits 5-7, the COP's G input

    // The keyboard's serial link (see keyboard_clock).
    int kb_clock, kb_data, kb_bit, kb_row;
    uint8_t kb_shift;

    // Interrupt sources, low = active, as on the board.
    bool timer_int, key_int, periodic_int;
    uint64_t periodic_due;     // when the periodic one-shot fires, or 0

    uint8_t digits[16];        // the LED digits: 0-9, A-F, 15 is blank

    uint64_t last_spoken;      // when the speech chip was last given a sound (not a pause)
    char subtitle[256];        // what rdi_subtitle shows
    char subtitle_next[256];   // the line as it is now, and since when (it's
    uint64_t subtitle_since;   // shown once it has held still for a moment)

    uint64_t cycles;           // Z80 clock cycles since power-on
    uint64_t cop_cycles;       // COP421 instruction cycles, in Z80 cycles
} RdiBoard;

bool rdi_recognizes(const char *path);
bool rdi_load_roms(RdiBoard *board, const char *path);
const char *rdi_game_name(const RdiBoard *board);

void rdi_set(RdiBoard *board, RdiSetting setting, bool on);
bool rdi_get(const RdiBoard *board, RdiSetting setting);

// Power on, connected to an LD-V1000.
void rdi_reset(RdiBoard *board, Ldv1000 *player);

// Run the board for (at least) the given number of Z80 cycles.
void rdi_run(RdiBoard *board, uint64_t cycles);

// What the narrator is saying, as text, or NULL if he's quiet. The game
// keeps each line as text in RAM (at A500, its length at A6D3), spelled the
// way it should sound, and turns it into phonemes for the speech chip; this
// tidies it up for reading ("/SP/ELL-YER /N/AME" becomes "SPELL YER NAME").
const char *rdi_subtitle(RdiBoard *board);

#endif
