// ldp1000.h - Sony's LDP-1000 laserdisc player, as a game sees it.
//
// Data East's laserdisc board (Bega's Battle, its own Cobra Command, Road
// Blaster) drives this player over a serial line, through a 6850 on the
// game's board (see games/dataeast/board.c). Each command is one byte, and
// the player answers most of them with 0A ("acknowledged") a millisecond or
// a few later; the game waits for that before it sends the next.
//
//     3A  play           4F  still (pause)
//     30-39  the digits "0" to "9" of a number
//     43  search: a frame number follows, then 40 ("enter"); when the
//         player arrives at the frame it sends 01 ("completed")
//     44  repeat: the frame to play to, 40, how many times (none means
//         once, 0 means forever), 40; it plays from where it is to that
//         frame, sending 01 at the end of each pass, then holds the frame
//     56  clear ("C.L."): cancel the entry, or a search under way
//     60  report the frame: answered with five digits, "01234"
//     24, 25  audio muted, unmuted     46-49  audio channel 1 or 2 on, off
//
// An error, such as a search to a frame that isn't on the disc, is
// answered 02. Commands and timings from the Daphne emulator's ldp1000.cpp,
// which took the timings from Sony's LDP-1000A programming manual.

#ifndef LV_LDP1000_H
#define LV_LDP1000_H

#include "ldp/ldp.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    Ldp *ldp;
    double clock_hz;  // rate of the "now" timestamps
    uint64_t last;    // time the player was last brought up to date

    uint8_t replies[64];  // bytes waiting to go back to the game
    int reply_head, reply_count;
    uint64_t reply_at;    // when the next one may go: a command takes a while

    char digits[8];  // a number being entered
    int digit_count;
    int entering;    // 0, or the command (43, 44) that 40 will finish
    bool searching;  // a search is under way; 01 when it ends
    bool aborting;   // ... and was cleared, so no 01
    int queued_search;  // a search sent while one was being cleared (-1: none)

    bool repeating;  // a repeat is under way
    int repeat_start, repeat_end, repeat_times;  // repeat_times -1: forever

    bool muted;
    bool channel_on[2];
} Ldp1000;

// Attach to a player model (already set up with ldp_init) and park it.
void ldp1000_init(Ldp1000 *p, Ldp *ldp, double clock_hz);

// Bring the player up to time now.
void ldp1000_update(Ldp1000 *p, uint64_t now);

// A byte from the game.
void ldp1000_write(Ldp1000 *p, uint64_t now, uint8_t byte);

// Is a reply ready to go? Take it (0 if none).
bool ldp1000_has_reply(Ldp1000 *p, uint64_t now);
uint8_t ldp1000_read(Ldp1000 *p, uint64_t now);

// Can the disc's audio channel (0 or 1) be heard?
bool ldp1000_audio_on(const Ldp1000 *p, int channel);

#endif
