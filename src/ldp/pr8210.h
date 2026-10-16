// pr8210.h - the Pioneer PR-8210 laserdisc player, as a game sees it.
//
// The PR-8210 was a home player. Arcade boards like Stern's Cliff Hanger
// drive it through its remote-control socket: a single wire, on which the
// game sends the same codes the infrared remote would. The player never
// answers. To know where the disc is, the game reads the codes recorded in
// the picture itself (see pr8210_philips_code).
//
// A code is ten bits, sent as pulses on the wire: each bit is the gap until
// the next pulse, short (about 1 ms) for a 0 and long (about 2 ms) for a 1.
// The ten bits are 0, 0, 1, then the five bits of the command (the first one
// sent becomes the top bit), then 0, 0, and a last pulse closes the gap of
// the last bit. Games send each code several times, a field apart; the
// player acts on the first and ignores repeats that follow closely.
//
// Commands (from Hypseus, the Daphne emulator's successor, and MAME's
// pr8210 model by Aaron Giles): PLAY 14, PAUSE 0A, STEP forward 04, STEP
// back 12, SEEK 1A, the digits 0-9 (01, 11, 09, 19, 05, 15, 0D, 1D, 03, 13),
// AUDIO1 0E, AUDIO2 16 (each toggles a channel), REJECT 1E. A search is
// SEEK, the frame number's digits, SEEK. Others (scan, slow, 3x) are
// treated as PLAY.

#ifndef LV_PR8210_H
#define LV_PR8210_H

#include "ldp/ldp.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    Ldp *ldp;
    double clock_hz;       // rate of the "now" timestamps (the game's CPU clock)
    uint64_t last;         // time the player was last brought up to date

    bool line;             // the control wire's level
    uint64_t last_edge;    // time of the last rising edge
    uint64_t word_start;   // time of the first edge of the code being received
    uint16_t bits;         // the bits received so far, newest at the bottom
    uint8_t last_command;  // the last command acted on (for ignoring repeats)
    uint64_t last_command_time;

    bool seeking;          // SEEK received: digits follow
    int number;            // the digits so far
    int digit_count;
    bool audio[2];         // audio channel enables (AUDIO1, AUDIO2)
} Pr8210;

// Attach to a player model (already set up with ldp_init) and stop it.
// clock_hz is the rate of the timestamps passed to the other functions.
void pr8210_init(Pr8210 *ld, Ldp *ldp, double clock_hz);

// Bring the player up to time now.
void pr8210_update(Pr8210 *ld, uint64_t now);

// The game sets the control wire high or low.
void pr8210_control(Pr8210 *ld, uint64_t now, bool high);

// The 24-bit Philips code on line 17 or 18 of the current field, as the
// board's decoder reads it: 0xF80000 plus the frame number in five BCD
// digits while there's a picture, 0 while searching or stopped.
uint32_t pr8210_philips_code(Pr8210 *ld, uint64_t now);

// Can audio channel 0 or 1 be heard? (Only while playing at normal speed.)
bool pr8210_audio_on(const Pr8210 *ld, int channel);

// Name of a command code, for logging, or NULL.
const char *pr8210_command_name(uint8_t command);

#endif
